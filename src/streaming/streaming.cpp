#include "streaming/streaming.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/pattern.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <set>

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
    HMODULE streamingDll = GetModuleHandleA("gta-streaming-five.dll");
    if (!streamingDll) return;
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

// Contains no C++ objects so the virtual lookup can be isolated behind SEH. XBRVirtual inserts
// six methods at build 2802, making FindSlot vtable entry 8 there and entry 2 on older builds.
uint32_t findModuleSlotSafe(void* module, const char* stem, int build)
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

const char* slotWhyText(int w)
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

uint32_t targetStreamingId(const char* slot, int* why)
{
    auto fail = [&](int w) { if (why) *why = w; return 0xFFFFFFFFu; };
    if (!slot) return fail(SLOT_BAD_NAME);
    if (!g_getStreamingManagerFn || !g_getStreamingModuleFn) return fail(SLOT_NO_EXPORTS);
    const char* file = strrchr(slot, '/');
    file = file ? file + 1 : slot;
    const char* dot = strrchr(file, '.');
    if (!dot || dot == file || !dot[1]) return fail(SLOT_BAD_NAME);
    try {
        std::string stem(file, (size_t)(dot - file));
        std::string extension(dot + 1);
        void* manager = g_getStreamingManagerFn();
        if (!manager) return fail(SLOT_NO_MANAGER);
        // streaming::Manager::moduleMgr is at 0x1B8 in FiveM's exported Streaming.h layout.
        void* module = g_getStreamingModuleFn((uint8_t*)manager + 0x1B8, extension.c_str());
        if (!module) return fail(SLOT_NO_MODULE);
        uint32_t id = findModuleSlotSafe(module, stem.c_str(), runningGameBuild());
        // Slotted keys (folder/file) are also tried as "folder/stem" and "folder\stem": audio
        // banks are addressed as pack/bank and nobody knows which spelling the module keys on.
        // The log says which one hit, once per extension, so the answer lands in a user log.
        if (id == 0xFFFFFFFF && file != slot) {
            std::string folder(slot, (size_t)(file - slot - 1));
            const char* seps[] = { "/", "\\" };
            for (const char* sep : seps) {
                std::string alt = folder + sep + stem;
                id = findModuleSlotSafe(module, alt.c_str(), runningGameBuild());
                if (id != 0xFFFFFFFF) {
                    static std::set<std::string> said;
                    if (said.insert(extension).second)
                        LOG_INFO(LogCategory::Claim, "Slot lookup for .%s: the module keys on \"%s\" (folder%sname), not the bare name", extension.c_str(), alt.c_str(), sep);
                    break;
                }
            }
        }
        if (id == 0xFFFFFFFF) return fail(SLOT_NO_NAME);
        if (why) *why = SLOT_OK;
        return id;
    }
    catch (...) { return fail(SLOT_NO_MANAGER); }
}

void noteSlotWhy(const char* slot, int why)
{
    const char* dot = strrchr(slot, '.');
    if (!dot || !dot[1]) return;
    std::string ext = lower(dot + 1);
    auto it = g_slotWhyByExt.find(ext);
    if (it != g_slotWhyByExt.end() && it->second == why) return;
    g_slotWhyByExt[ext] = why;
    LOG_INFO(LogCategory::Claim, "Slot lookup for .%s: %s", ext.c_str(), slotWhyText(why));
}

bool localRawHandle(const char* file, uint32_t* handle)
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

StrMgr* exportedManagerSafe()
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

int recoverOccupiedSlot(Ov& ov)
{
    resolveOccupiedSlotExports();
    if (!g_mgr) g_mgr = exportedManagerSafe();
    uint32_t rawHandle = 0;
    if (!localRawHandle(ov.gfile ? ov.gfile : ov.file, &rawHandle)) return OCCUPIED_FAILED;
    ov.handle = rawHandle;

    int why = SLOT_OK;
    uint32_t target = targetStreamingId(ov.slot, &why);
    noteSlotWhy(ov.slot, why);   // "no module for this type" vs "does not know that name" is the whole diagnosis
    if (!validStreamingId(target)) {
        ov.id = 0xFFFFFFFF;
        return OCCUPIED_WAITING;
    }
    ov.id = target;
    __try {
        StrEntry& entry = g_mgr->entries[target];
        if (entry.handle != ov.handle && (entry.flags & 3) < 2) entry.handle = ov.handle;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return OCCUPIED_FAILED; }
    return OCCUPIED_ATTACHED;
}
