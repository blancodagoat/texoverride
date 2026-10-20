#include "streaming/streaming.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/pattern.h"
#include "core/cfx.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <set>
#include <mutex>
#include <unordered_map>

// FiveM's view of pgRawStreamer's entries (fiCollectionWrapper.h): chunkyArray<RawEntry, 1024, 64>.
struct RawEntryView {
    uint64_t packedFileEntry;
    uint32_t virtFlags, physFlags;
    uint64_t timestamp;
    const char* fileName;
};
static_assert(sizeof(RawEntryView) == 32, "FiveM RawEntry layout changed");

struct RawEntriesView {
    RawEntryView* memory[64];
    uint32_t count;
};

typedef void* (*GetStreamingManager_t)();
typedef void* (*GetStreamingModule_t)(void*, const char*);
typedef const RawEntriesView* (*GetRawEntries_t)();
typedef uint32_t* (*FindModuleSlot_t)(void*, uint32_t*, const char*);

static GetStreamingManager_t g_getStreamingManagerFn = nullptr;
static GetStreamingModule_t  g_getStreamingModuleFn = nullptr;
static GetRawEntries_t       g_getRawEntriesFn = nullptr;

// Leaf lock for the say-it-once caches below (the `said` sets and s_slotWhyByExt). The hook and
// the beat thread reach them under g_cs, but live reload reaches them from the game's main thread
// without it, and two unlocked inserts into one std container corrupt the heap. Held only around
// the container itself: nothing is called and nothing is logged under it.
static std::mutex g_saidLock;
static std::unordered_map<std::string, int> s_slotWhyByExt;

static bool firstSaid(std::set<std::string>& said, const std::string& key)
{
    std::lock_guard<std::mutex> lk(g_saidLock);
    return said.insert(key).second;
}

// rage::strStreamingInfo packs three things into that second dword (Rockstar streamingdefs.h,
// cross-checked against Cfx's ReleaseObject(idx, 0xF1)):
//   bits 0-1   status: 0 NOTLOADED, 1 LOADED, 2 LOADREQUESTED, 3 LOADING
//   bits 2-15  dependent count
//   bits 16-31 STRFLAGs: DONTDELETE 1<<0, FORCE_LOAD 1<<1, PRIORITY_LOAD 1<<2, LOADSCENE 1<<3,
//              MISSION 1<<4, CUTSCENE 1<<5, INTERIOR 1<<6, ZONEDASSET 1<<7
// Read only. Never write a STRFLAG by hand: the setters also move the entry between the loaded
// and persistent lists and maintain m_numPriorityRequests, and a raw write desyncs both.
const char* strStatusText(uint32_t flags)
{
    switch (flags & 3) {
    case 0:  return "not loaded";
    case 1:  return "ALREADY LOADED";
    case 2:  return "load requested";
    default: return "loading";
    }
}

void resolveOccupiedSlotExports()
{
    if (g_getStreamingManagerFn && g_getStreamingModuleFn && g_getRawEntriesFn) return;
    // Retried by moduleFor while anything is missing: if FiveM loads its streaming DLL after
    // Setup, the lookups start working instead of answering "exports missing" all session.
    if (HMODULE streamingDll = GetModuleHandleA("gta-streaming-five.dll")) {
        if (!g_getStreamingManagerFn)
            g_getStreamingManagerFn = (GetStreamingManager_t)GetProcAddress(streamingDll,
                "?GetInstance@Manager@streaming@@SAPEAV12@XZ");
        if (!g_getStreamingModuleFn)
            g_getStreamingModuleFn = (GetStreamingModule_t)GetProcAddress(streamingDll,
                "?GetStreamingModule@strStreamingModuleMgr@streaming@@QEAAPEAVstrStreamingModule@2@PEBD@Z");
        if (!g_getRawEntriesFn)
            g_getRawEntriesFn = (GetRawEntries_t)GetProcAddress(streamingDll,
                "?GetPgRawStreamerEntries@rage@@YAAEBU?$chunkyArray@URawEntry@fiCollection@rage@@$0EAA@$0EA@@1@XZ");
    }
    static bool said = false;
    if (!said) {
        said = true;
        LOG_INFO(LogCategory::Core, "Slot lookup exports: manager=%s module=%s rawEntries=%s",
                 g_getStreamingManagerFn ? "ok" : "MISSING", g_getStreamingModuleFn ? "ok" : "MISSING",
                 g_getRawEntriesFn ? "ok" : "MISSING");
    }
}

static StrMgr* exportedManagerSafe()
{
    if (!g_getStreamingManagerFn) return nullptr;
    __try {
        StrMgr* manager = (StrMgr*)g_getStreamingManagerFn();
        if (!manager || !manager->entries || manager->numEntries <= 0 || manager->numEntries > 10000000)
            return nullptr;
        return manager;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// What a loaded slot really costs. The cost audit at scan time can only read the RSC7 page flags
// out of the file on disk, which is the file's own size and nothing more; a clothing .ydd that
// drags three shared texture dictionaries in with it is charged for none of them there. FiveM's
// devtools exports a walk of the dependency tree, so once a slot is resident a better figure is
// one call away.
//
// It only walks HALF of it, and that is not obvious from the name. Disassembled at
// devtools-five+0x332A0: CountDependencyMemory seeds its accumulator from
// gta-streaming-five!StreamingDataEntry::ComputeVirtualSize and then recurses over
// GetDependencies adding more of the same. Virtual is system memory. Texture pixels live in
// PHYSICAL memory, so every texture dictionary came back as ~0 and the HEAVY IN MEMORY warning
// below could never once fire for a .ytd, which is the file type every texture-loss report is
// actually about: mp_fm_skin_f_up_whi.ytd audits at 16.0 MB from its page flags and reported
// 0.0 MB loaded. ComputePhysicalSize is exported beside it, same this-is-the-entry calling
// shape, so ask for both and report them apart the way the scan audit already does.
//
// ponytail: physical is the entry's OWN pages; the dependency recursion is virtual-only because
// GetDependencies sits behind a build-dependent vtable offset we do not have a pattern for. A
// .ytd is exact (it has no dependencies), a drawable is charged for its own vertex buffers but
// not for the pixels of texture dictionaries it shares. Walk it properly if a shared-texture
// report ever needs the difference.
//
// Read-only. SEH because it walks game-owned arrays that a mid-eviction slot can leave half valid.
typedef uint64_t (*CountDepMem_t)(void* mgr, uint32_t idx);
typedef uint64_t (*ComputePhysSize_t)(void* entry, uint32_t idx);   // __thiscall: entry is `this`
static CountDepMem_t g_countDepMemFn = nullptr;
static ComputePhysSize_t g_physSizeFn = nullptr;
static bool g_countDepTried = false;

void slotMemoryCost(uint32_t id, uint64_t* virt, uint64_t* phys)
{
    if (virt) *virt = 0;
    if (phys) *phys = 0;
    if (!g_countDepTried) {
        g_countDepTried = true;
        g_countDepMemFn = (CountDepMem_t)cfxSymbol("devtools-five.dll",
            "?CountDependencyMemory@@YA_KPEAVManager@streaming@@I@Z");
        g_physSizeFn = (ComputePhysSize_t)cfxSymbol("gta-streaming-five.dll",
            "?ComputePhysicalSize@StreamingDataEntry@@QEAA_KI@Z");
    }
    if (!validStreamingId(id)) return;
    __try {
        if (virt && g_countDepMemFn && g_getStreamingManagerFn) {
            void* mgr = g_getStreamingManagerFn();
            if (mgr) *virt = g_countDepMemFn(mgr, id);
        }
        if (phys && g_physSizeFn) {
            StrMgr* manager = exportedManagerSafe();
            if (manager && id < (uint32_t)manager->numEntries)
                *phys = g_physSizeFn(&manager->entries[id], id);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (virt) *virt = 0;
        if (phys) *phys = 0;
    }
}

// Contains no C++ objects so the virtual lookup can be isolated behind SEH. XBRVirtual inserts
// six methods at build 2802, making FindSlot vtable entry 8 there and entry 2 on older builds.
static uint32_t findModuleSlotSafe(void* module, const char* stem, int build)
{
    __try {
        if (!module || !stem || !build) return 0xFFFFFFFF;
        void** vtable = *(void***)module;
        if (!vtable) return 0xFFFFFFFF;
        FindModuleSlot_t findSlot = (FindModuleSlot_t)vtable[build >= 2802 ? 8 : 2];
        if (!findSlot) return 0xFFFFFFFF;
        uint32_t local = 0xFFFFFFFF;
        findSlot(module, &local, stem);
        if (local == 0xFFFFFFFF) return local;
        uint32_t base = *(uint32_t*)((uint8_t*)module + 8);
        if (base > 0xFFFFFFFFu - local) return 0xFFFFFFFF;
        return base + local;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFF; }
}

static const char* slotWhyText(int w)
{
    switch (w) {
    case SLOT_OK:         return "ok";
    case SLOT_NO_EXPORTS: return "FiveM's streaming exports are missing, so no name can be looked up";
    case SLOT_BAD_NAME:   return "the key has no usable extension";
    case SLOT_NO_MANAGER: return "the streaming manager came back null";
    case SLOT_NO_MODULE:  return "the game has no streaming module for this file type";
    default:              return "the module does not know that name";
    }
}

// The streaming module for a file type, or null. *why says which step failed.
static void* moduleFor(const std::string& extension, int* why)
{
    if (!g_getStreamingManagerFn || !g_getStreamingModuleFn) resolveOccupiedSlotExports();
    if (!g_getStreamingManagerFn || !g_getStreamingModuleFn) { *why = SLOT_NO_EXPORTS; return nullptr; }
    void* manager = g_getStreamingManagerFn();
    if (!manager) { *why = SLOT_NO_MANAGER; return nullptr; }
    // streaming::Manager::moduleMgr is at 0x1B8 in FiveM's exported Streaming.h layout.
    void* module = g_getStreamingModuleFn((uint8_t*)manager + 0x1B8, extension.c_str());
    if (!module) { *why = SLOT_NO_MODULE; return nullptr; }
    return module;
}

uint32_t targetStreamingId(const char* slot, int* why)
{
    int w = SLOT_OK;
    uint32_t id = 0xFFFFFFFF;
    try {
        const char* file = slot ? strrchr(slot, '/') : nullptr;
        file = file ? file + 1 : slot;
        const char* dot = file ? strrchr(file, '.') : nullptr;
        if (!dot || dot == file || !dot[1]) w = SLOT_BAD_NAME;
        else if (void* module = moduleFor(dot + 1, &w)) {
            std::string stem(file, (size_t)(dot - file));
            id = findModuleSlotSafe(module, stem.c_str(), runningGameBuild());
            // Slotted keys (folder/file) are also tried as "folder/stem" and "folder\stem": audio
            // banks are addressed as pack/bank and nobody knows which spelling the module keys on.
            // The log says which one hit, once per extension, so the answer lands in a user log.
            if (id == 0xFFFFFFFF && file != slot) {
                std::string folder(slot, (size_t)(file - slot - 1));
                for (const char* sep : { "/", "\\" }) {
                    std::string alt = folder + sep + stem;
                    id = findModuleSlotSafe(module, alt.c_str(), runningGameBuild());
                    if (id != 0xFFFFFFFF) {
                        static std::set<std::string> said;
                        if (firstSaid(said, dot + 1))
                            LOG_INFO(LogCategory::Claim, "Slot lookup for .%s: the module keys on \"%s\" (folder%sname), not the bare name", dot + 1, alt.c_str(), sep);
                        break;
                    }
                }
            }
            if (id == 0xFFFFFFFF) w = SLOT_NO_NAME;
        }
    }
    catch (...) { w = SLOT_NO_MANAGER; id = 0xFFFFFFFF; }
    if (why) *why = w;
    return w == SLOT_OK ? id : 0xFFFFFFFF;
}

static void noteSlotWhy(const char* slot, int why)
{
    std::string ext = extOf(slot);
    if (ext.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_saidLock);
        auto it = s_slotWhyByExt.find(ext);
        if (it != s_slotWhyByExt.end() && it->second == why) return;
        s_slotWhyByExt[ext] = why;
    }
    LOG_INFO(LogCategory::Claim, "Slot lookup for .%s: %s", ext.c_str(), slotWhyText(why));
}

static bool localRawHandle(const char* file, uint32_t* handle)
{
    if (!file || !handle || !g_getRawEntriesFn) return false;
    __try {
        const RawEntriesView* entries = g_getRawEntriesFn();
        if (!entries || entries->count > 65535) return false;
        for (uint32_t i = entries->count; i > 0; --i) {
            uint32_t index = i - 1;
            RawEntryView* chunk = entries->memory[index / 1024];
            if (!chunk) continue;
            const char* candidate = chunk[index % 1024].fileName;
            if (!candidate || _stricmp(candidate, file) != 0) continue;
            // Entry zero is the documented pgRawStreamer dummy and also encodes the manager's
            // empty-handle sentinel, so it can never be attached as an asset.
            if (index == 0) return false;
            *handle = index;       // original registerRawStreamingFile uses collection zero
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool validStreamingId(uint32_t id)
{
    // GetStreamingIndex-style APIs use zero for a miss; never write entry zero.
    return g_mgr && g_mgr->entries && id != 0 && id != 0xFFFFFFFF &&
           id < (uint32_t)g_mgr->numEntries;
}

// Look up ONE exact key, no fallbacks. targetStreamingId cannot answer this question, because its
// folder fallback only runs when the bare name MISSES, and for these files the bare name hits.
static uint32_t slotForExactKey(const std::string& extension, const char* key)
{
    try {
        int why = SLOT_OK;
        void* module = moduleFor(extension, &why);
        return module ? findModuleSlotSafe(module, key, runningGameBuild()) : 0xFFFFFFFF;
    }
    catch (...) { return 0xFFFFFFFF; }
}

// One NAME can own more than one slot. Vanilla ships mp_fm_skin_f_up_whi.ytd twice inside x64v.rpf,
// in ped_mp_overlay_txds.rpf and in strm_peds_mp_overlay_txds.rpf, and the two are DIFFERENT images
// (diffed straight out of the archives: the first carries Rockstar blue nipple markers, the second
// is a flat wash). FindSlot returns one of them. Taking that one over and holding it leaves the ped
// reading the other, which is exactly the shape of "registered, loaded from your file, still draws
// vanilla". Root keys carry no folder, so nothing on the normal path ever probes the container form.
// Returns a second, different index for this name, or 0xFFFFFFFF.
static uint32_t containerAliasId(const char* slot, uint32_t primary)
{
    static const char* kContainers[] = {
        "ped_mp_overlay_txds", "strm_peds_mp_overlay_txds",
        "streamedpeds_mp", "streamedpeds_players",
    };
    if (!slot || strchr(slot, '/')) return 0xFFFFFFFF;   // root keys only
    const char* dot = strrchr(slot, '.');
    if (!dot || dot == slot || !dot[1]) return 0xFFFFFFFF;
    std::string stem(slot, (size_t)(dot - slot));
    std::string extension(dot + 1);
    for (const char* container : kContainers) {
        for (const char* sep : { "/", "\\" }) {
            std::string key = std::string(container) + sep + stem;
            uint32_t id = slotForExactKey(extension, key.c_str());
            if (validStreamingId(id) && id != primary) {
                static std::set<std::string> said;
                if (firstSaid(said, extension))
                    LOG_INFO(LogCategory::Claim, "Slot lookup for .%s: this name also owns the slot \"%s\" (id=%u), which is NOT the one the bare name resolves to; pinning both", extension.c_str(), key.c_str(), id);
                return id;
            }
        }
    }
    return 0xFFFFFFFF;
}

// registerRawStreamingFile refused the name because the slot already holds a handle. Find the
// raw entry for our own file and attach it to the slot the name resolves to.
static ClaimResult recoverOccupiedSlot(Ov& ov)
{
    resolveOccupiedSlotExports();
    if (!g_mgr) g_mgr = exportedManagerSafe();
    uint32_t rawHandle = 0;
    if (!localRawHandle(ov.file, &rawHandle)) return CLAIM_REJECTED;
    ov.handle = rawHandle;

    int why = SLOT_OK;
    uint32_t target = targetStreamingId(ov.slot, &why);
    noteSlotWhy(ov.slot, why);   // "no module for this type" vs "does not know that name" is the whole diagnosis
    if (!validStreamingId(target)) {
        ov.id = 0xFFFFFFFF;
        return CLAIM_WAITING;
    }
    ov.id = target;
    if (ov.altId == 0xFFFFFFFF) ov.altId = containerAliasId(ov.slot, target);
    __try {
        StrEntry& entry = g_mgr->entries[target];
        if (entry.handle != ov.handle && (entry.flags & 3) < 2) entry.handle = ov.handle;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return CLAIM_REJECTED; }
    return CLAIM_TAKEOVER;
}

// Ask the store what this name resolves to BEFORE claiming it. o_regRaw mints a fresh index and
// re-points the name at it, so the same question asked afterwards can only ever hand back the id
// we were just given. That is what it did: on a 335 file pack every claim came back "unaliased"
// while the ids themselves were 335 consecutive integers in registration order, which scattered
// vanilla names could not possibly own.
//
// And a claim that SUCCEEDS is not proof we took the slot over. For a name the game already owns,
// registerRawStreamingFile can mint a brand new index and hand that back while the store keeps
// resolving the name to its original one. The handle then lands in a slot nothing looks up and
// the log reads perfectly while nothing changes in game (seen on .ycd: seven scattered vanilla
// dictionaries came back with seven CONSECUTIVE ids). So the entry the store resolved the name to
// before we touched it is pinned as well, as altId.
//
// No SEH around o_regRaw, on purpose: it inserts into the game's name tables, and a fault caught
// half way through leaves them broken for the rest of the session, to die later looking like
// somebody else's bug. Both callers journal the key first, so a crash here costs one launch.
ClaimResult claimOne(Ov& ov)
{
    int why = SLOT_OK;
    uint32_t before = targetStreamingId(ov.slot, &why);
    uint32_t id = 0xFFFFFFFF;
    o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
    ov.id = id;
    if (id == 0xFFFFFFFF) return recoverOccupiedSlot(ov);
    noteSlotWhy(ov.slot, why);
    if (validStreamingId(before) && before != id) ov.altId = before;
    if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
        ov.handle = g_mgr->entries[id].handle;
    return ov.handle ? CLAIM_DIRECT : CLAIM_NO_HANDLE;
}
