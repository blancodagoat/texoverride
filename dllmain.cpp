// texoverride — v8: claim early, then re-assert (last writer wins)
//
// v7 registered each override under its base slot name via registerRawStreamingFile and assumed
// the claim would stick. It never does: streaming slots are name -> id -> handle, and whoever
// writes the HANDLE last owns the slot. Vanilla DLC mounts re-point claimed slots when they load
// (that is why base luxe_02 props stayed vanilla), and FiveM's own loader, on seeing a slot whose
// handle is already set, overwrites the handle directly without ever calling the function we hook
// (that is why server clothes never redirected — redirects=0 forever). Server .ytd files bypass
// the hooked function entirely (FiveM routes them to its own raw streamer via RegisterObject).
//
// So v8 does what FiveM's own override path does, but keeps doing it:
//   1. claim: register our file under the slot name (creates the slot + a raw entry for our file,
//      and remember Entries[id].handle — a durable ticket to our data)
//   2. re-assert: once a second, if anything re-pointed Entries[id].handle, write ours back
//
// SAFETY: clothing folders may only name human freemode-ped collections (mp_m_freemode_01*,
// mp_f_freemode_01*); anything else — animal peds, story/ambient peds, vehicles, weapons, props,
// maps, scripts — is refused at load and skipped at runtime. Bare .ytd files at the root override
// one texture dictionary by exact name (see isAllowedKey), and placement .xml files only ever
// touch tattoo preset floats after a fingerprint match (see the placement section). It also logs
// every distinct collection the server streams (tagged), so you can see what is in reach.

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include "MinHook.h"

static HINSTANCE g_self;
static char g_logPath[MAX_PATH], g_overrideDir[MAX_PATH];
static bool g_off = false;

struct Ov {
    const char* slot; const char* file;   // both persistent, forward-slash, lowercased
    uint32_t id = 0xFFFFFFFF;             // global streaming index our claim landed on
    uint32_t handle = 0;                  // the handle value that points at OUR file
};
static std::vector<Ov> g_ovs;
static std::unordered_map<std::string, const char*> g_bySlot;   // slot -> file
static std::unordered_set<std::string> g_collSeen;   // distinct collections, for the map

// rage::strStreamingEngine::ms_info — the streaming info pool. Entries[id].handle is what the
// loader actually opens; layout from Cfx's gta-streaming-five/include/Streaming.h.
struct StrEntry { uint32_t handle, flags; };
struct StrMgr   { StrEntry* entries; char pad[16]; int numEntries; };
static StrMgr* g_mgr = nullptr;

static volatile LONG g_regTotal = 0, g_redirects = 0, g_idsReady = 0;
static long g_reclaims = 0, g_deferred = 0;   // written by the heartbeat thread only
static bool g_didRegister = false;
static bool g_b1 = true, g_b2 = false, g_captured = false;
static CRITICAL_SECTION g_cs;   // guards the one-time registration + the collection map (hook may run on >1 thread)

static void logf(const char* fmt, ...)
{
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") != 0 || !f) return;
    time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
    char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    fprintf(f, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

#define TEXOVERRIDE_VERSION "0.4.1"

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string fwd(std::string s)   { for (char& c : s) if (c=='\\') c='/'; return s; }

// The log must stay free of personal information (an absolute path carries the Windows user
// name), so override files are always logged relative to tex_overrides\.
static const char* rel(const char* file)
{
    size_t n = strlen(g_overrideDir);
    return strlen(file) > n ? file + n : file;
}

// SAFETY: only human freemode-ped collections may be touched. Everything else — animal peds
// (canine…), story/ambient peds (a_*, ig_*, cs_*), vehicles, weapons, props, maps, scripts — is
// left strictly alone. This is what stops a "dog head replaced by a human head" mistake.
static bool isFreemodePed(const std::string& coll)
{
    std::string c = lower(coll);
    return c.rfind("mp_m_freemode_01", 0) == 0 || c.rfind("mp_f_freemode_01", 0) == 0;
}
static std::string collectionOf(const std::string& key)   // "collection/file" -> "collection"
{
    size_t s = key.find('/');
    return (s == std::string::npos) ? key : key.substr(0, s);
}
// A key with a slash is a clothing slot ("collection/file") and must be a freemode-ped collection.
// A key without one is a bare-name texture dictionary: overlay txds (skin, tattoos, facepaint,
// hair, beards, shirt decals...) live loose in the overlay rpfs and stream by filename alone.
// A scan of every *overlay*.rpf in the game found ~100 unrelated naming families (mp_fm_skin_*,
// *_tat_*, mp_*_tee_*, hwskull_*, ...) plus arbitrarily-named server packs, so no name whitelist
// can work. The gate for bare names is type + exactness instead: .ytd only (a texture cannot
// cross-wire a model slot), and it only ever replaces the dictionary whose exact name it bears.
static bool isAllowedKey(const std::string& key)
{
    size_t s = key.find('/');
    if (s == std::string::npos)
        return key.size() > 4 && key.compare(key.size()-4, 4, ".ytd") == 0;
    return isFreemodePed(key.substr(0, s));
}

static int scanDir(const std::string& base, const std::string& rel)
{
    std::string pattern = base + rel + "\\*";
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string childRel = rel.empty() ? name : rel + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += scanDir(base, childRel);
        else {
            std::string ln = lower(name);
            // .meta (shop_tattoo.meta etc.) is shop data, not looks — not applied yet, but the
            // pack-folder layout (tex_overrides/mplowrider/shop_tattoo.meta) is the reserved
            // convention for it, so acknowledge the file instead of silently skipping it
            if (ln.size() > 5 && ln.compare(ln.size()-5, 5, ".meta") == 0) {
                logf("IGNORED %s — .meta files hold shop data (prices/menus), not looks; see README", fwd(childRel).c_str());
                continue;
            }
            if (ln.size() > 4 && (ln.compare(ln.size()-4,4,".ytd")==0 || ln.compare(ln.size()-4,4,".ydd")==0)) {
                std::string slotStr = lower(fwd(childRel));                    // "mp_m_freemode_01/teef_004_u.ydd" or bare "mp_fm_skin_m_up_whi.ytd"
                // SAFETY GATE: folders must be freemode-ped collections; root files must be .ytd.
                if (!isAllowedKey(slotStr)) {
                    logf("SKIP (folders must be freemode-ped collections, root files must be .ytd): %s", slotStr.c_str());
                    continue;
                }
                const char* slot = _strdup(slotStr.c_str());
                const char* file = _strdup(fwd(base + childRel).c_str());      // our absolute path
                g_ovs.push_back({ slot, file });
                g_bySlot[slot] = file;
                ++n;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

// pattern scan over the game module's executable sections; -1 in pat = wildcard byte
static uint8_t* scanModule(const short* pat, size_t len)
{
    HMODULE mod = GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)mod;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* b = (uint8_t*)mod + sec->VirtualAddress;
        size_t sz = sec->Misc.VirtualSize;
        for (size_t j = 0; j + len <= sz; ++j) {
            size_t k = 0;
            while (k < len && (pat[k] < 0 || b[j + k] == (uint8_t)pat[k])) ++k;
            if (k == len) return b + j;
        }
    }
    return nullptr;
}

// resolve a rip-relative disp32 (p points at the 4 displacement bytes)
static uint8_t* ripTarget(uint8_t* p) { return p + 4 + *(int32_t*)p; }

// ============================ tattoo placement (overlays.xml) ============================
// Tattoo POSITION lives in per-DLC overlays.xml files, loaded through the DLC content pipeline —
// never through streaming — so the .ytd path above can't reach it. Instead: the user drops an
// edited copy of the owning overlays.xml (game format, full collection) into tex_overrides/, and
// we patch the parsed values inside the game's PedDecorationManager. Data writes only, same class
// as the handle re-assert; no code is touched.
//
// The manager is located with the pattern Cfx itself publishes (PatchTattooSort.cpp), and a
// collection is a 0xA0-byte struct with its name hash at +0x10 ("has never changed" — Cfx). The
// preset array layout inside it is NOT hardcoded: it is solved at runtime by fingerprinting the
// user's own file against memory (preset name hashes give base+stride; the unedited uv/scale/rot
// values must match memory for >=70% of presets before a single byte is written). Wrong build,
// wrong file, wrong layout -> nothing matches -> nothing is written.
// ponytail: >=70% consensus means a file where nearly every preset was edited can't be verified;
// keep most values stock. Ship vanilla sidecar values if that ever becomes a real limit.

static uint32_t joaat(const char* s, size_t n)   // GTA's case-insensitive hash
{
    uint32_t h = 0;
    for (size_t i = 0; i < n; ++i) { h += (uint8_t)tolower((unsigned char)s[i]); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15; return h;
}

struct PlPreset { uint32_t hash; float v[5]; };   // v = uvX uvY scaleX scaleY rotation
struct PlColl {
    std::string name, src;                        // collection name, source xml (for the log)
    uint32_t hash = 0;
    std::vector<PlPreset> presets;                // in file order == in parse order in memory
    uint32_t arrOff = 0, nameOff = 0, stride = 0, uvOff = 0;   // solved layout
    bool solved = false, dead = false;
    long writes = 0;
};
static std::vector<PlColl> g_pl;
static uint8_t** g_decorMgr = nullptr;            // PedDecorationManager::ms_instance
static int32_t   g_decorCollOff = -1;             // offset of collections atArray in the manager
static bool g_plLocated = false;
static volatile LONG g_plFault = 0;

static void parsePlacementXml(const std::string& path, const char* fname)
{
    FILE* f; if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return;
    std::string x; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { x.resize((size_t)n); x.resize(fread(&x[0], 1, (size_t)n, f)); } fclose(f);

    auto text = [&](size_t from, size_t to, const char* tag, std::string& out) -> bool {
        std::string open = std::string("<") + tag + ">";
        size_t a = x.find(open, from); if (a == std::string::npos || a >= to) return false;
        a += open.size(); size_t b = x.find('<', a); if (b == std::string::npos) return false;
        out = x.substr(a, b - a); return true;
    };
    auto attrf = [&](size_t from, size_t to, const char* tag, const char* attr, float& out) -> bool {
        std::string open = std::string("<") + tag;
        size_t a = x.find(open, from); if (a == std::string::npos || a >= to) return false;
        size_t e = x.find('>', a); if (e == std::string::npos) return false;
        std::string key = std::string(attr) + "=\"";
        size_t v = x.find(key, a); if (v == std::string::npos || v >= e) return false;
        out = (float)atof(x.c_str() + v + key.size()); return true;
    };

    size_t ps = x.find("<presets>"), pe = x.find("</presets>");
    if (ps == std::string::npos || pe == std::string::npos) { logf("placement: %s has no <presets>, ignored", fname); return; }

    PlColl pc; pc.src = fname;
    if (!text(pe, x.size(), "nameHash", pc.name)) { logf("placement: %s has no collection nameHash after </presets>, ignored", fname); return; }
    pc.hash = joaat(pc.name.c_str(), pc.name.size());

    for (size_t pos = ps; (pos = x.find("<Item", pos)) != std::string::npos && pos < pe; ) {
        size_t end = x.find("</Item>", pos); if (end == std::string::npos || end > pe) break;
        PlPreset p{}; std::string nm;
        bool ok = text(pos, end, "nameHash", nm)
               && attrf(pos, end, "uvPos", "x", p.v[0]) && attrf(pos, end, "uvPos", "y", p.v[1])
               && attrf(pos, end, "scale", "x", p.v[2]) && attrf(pos, end, "scale", "y", p.v[3])
               && attrf(pos, end, "rotation", "value", p.v[4]);
        if (ok) { p.hash = joaat(nm.c_str(), nm.size()); pc.presets.push_back(p); }
        pos = end + 7;
    }
    if (pc.presets.size() < 3) { logf("placement: %s has %zu preset(s), need 3+ to fingerprint, ignored", fname, pc.presets.size()); return; }
    g_pl.push_back(std::move(pc));
}

static void placementLocate()
{
    // Cfx's own pattern for the tattoo-sort call site (PatchTattooSort.cpp); the comparator it
    // points at loads ms_instance and the collections-array offset.
    const short PAT[] = { 0x41,0x0F,0xB7,0xDE,0x4C,0x8D,0x0D,-1,-1,-1,-1,0x41,0xB8 };
    uint8_t* p = scanModule(PAT, 13);
    if (!p) { logf("placement: decoration pattern NOT FOUND — placement files inert"); return; }
    uint8_t* comparator = ripTarget(p + 7);
    g_decorMgr = (uint8_t**)ripTarget(comparator + 3);
    int32_t off = *(int32_t*)(comparator + 7 + 3);
    if (off <= 0 || off > 0x10000) { logf("placement: implausible collections offset 0x%x — placement disabled", off); g_decorMgr = nullptr; return; }
    g_decorCollOff = off;
    logf("placement: PedDecorationManager @ %p, collections at +0x%x", (void*)g_decorMgr, off);
}

static bool placementSolveImpl(PlColl& pc, uint8_t* coll)
{
    const size_t N = pc.presets.size();
    for (uint32_t o = 0; o + 10 <= 0xA0; o += 8) {                      // candidate atArray slots (ptr + count must fit in the 0xA0 struct)
        uint8_t* P = *(uint8_t**)(coll + o);
        if ((uintptr_t)P < 0x10000) continue;
        uint16_t cnt = *(uint16_t*)(coll + o + 8);
        if (cnt != (uint16_t)N) continue;                               // array must hold exactly our preset count
        for (uint32_t a = 0; a + 4 <= 0x80; a += 4) {                   // preset name-hash offset
            if (*(uint32_t*)(P + a) != pc.presets[0].hash) continue;
            for (uint32_t s = 4; s <= 0x100; s += 4) {                  // preset stride
                bool ok = true;
                for (size_t i = 1; i < N && i < 6; ++i)
                    if (*(uint32_t*)(P + a + i * s) != pc.presets[i].hash) { ok = false; break; }
                if (!ok) continue;
                for (uint32_t f = 0; f + 20 <= s; f += 4) {             // uvX offset; uv/scale/rot are contiguous
                    size_t hits = 0;
                    for (size_t i = 0; i < N; ++i) {
                        float* q = (float*)(P + i * s + f);
                        bool m = true;
                        for (int k = 0; k < 5; ++k) if (fabsf(q[k] - pc.presets[i].v[k]) > 1e-3f) { m = false; break; }
                        if (m) ++hits;
                    }
                    if (hits * 10 >= N * 7) {                           // >=70% stock values: layout confirmed
                        pc.arrOff = o; pc.nameOff = a; pc.stride = s; pc.uvOff = f; pc.solved = true;
                        logf("placement: %s layout solved (array@+0x%02x stride=0x%x name+0x%x uv+0x%x, %zu/%zu stock)",
                             pc.name.c_str(), o, s, a, hits, N);
                        return true;
                    }
                }
            }
        }
    }
    return false;   // collection not fully loaded yet, or layout unknown — retried next beat
}

// Solving probes candidate pointers found inside the collection struct, and a stale one can point
// at freed memory. A fault here must retire only THIS collection, not the whole feature — the
// outer SEH in placementBeatSafe stays as the last line of defense for the apply path.
// (POD locals only, so __try is legal in this frame.)
static bool placementSolve(PlColl& pc, uint8_t* coll)
{
    __try { return placementSolveImpl(pc, coll); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        pc.dead = true;
        logf("placement: %s hit unreadable memory while solving, skipped", pc.name.c_str());
        return false;
    }
}

static void placementBeat()
{
    if (!g_plLocated) { placementLocate(); g_plLocated = true; }
    if (!g_decorMgr) return;
    uint8_t* mgr = *g_decorMgr;
    if (!mgr) return;                                                    // manager not constructed yet
    uint8_t* arr = *(uint8_t**)(mgr + g_decorCollOff);
    uint16_t cnt = *(uint16_t*)(mgr + g_decorCollOff + 8);
    if (!arr || cnt == 0 || cnt > 1000) return;

    for (auto& pc : g_pl) {
        if (pc.dead) continue;
        uint8_t* coll = nullptr;
        for (uint16_t i = 0; i < cnt; ++i)
            if (*(uint32_t*)(arr + (size_t)i * 0xA0 + 0x10) == pc.hash) { coll = arr + (size_t)i * 0xA0; break; }
        if (!coll) continue;                                             // that DLC/pack not mounted (yet)
        if (!pc.solved && !placementSolve(pc, coll)) continue;

        uint8_t* P = *(uint8_t**)(coll + pc.arrOff);
        if ((uintptr_t)P < 0x10000 || *(uint32_t*)(P + pc.nameOff) != pc.presets[0].hash) { pc.solved = false; continue; }
        for (size_t i = 0; i < pc.presets.size(); ++i) {
            float* q = (float*)(P + i * pc.stride + pc.uvOff);
            for (int k = 0; k < 5; ++k) {
                if (fabsf(q[k] - pc.presets[i].v[k]) <= 1e-4f) continue;
                q[k] = pc.presets[i].v[k];
                if (++pc.writes <= 40) logf("PLACEMENT  %s[%zu] field %d -> %f", pc.name.c_str(), i, k, pc.presets[i].v[k]);
            }
        }
    }
}

// SEH shield: a bad pointer while walking foreign structs must never crash the game.
// (No C++ objects in this frame — required for __try. A fault permanently disables placement.)
static void placementBeatSafe()
{
    if (g_plFault || g_pl.empty()) return;
    __try { placementBeat(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_plFault, 1);
        logf("placement: memory fault — placement disabled for this session");
    }
}
// ========================================================================================

typedef uint32_t* (*RegRaw_t)(uint32_t*, const char*, bool, const char*, bool);
static RegRaw_t o_regRaw = nullptr;

static uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    InterlockedIncrement(&g_regTotal);

    if (asName)
    {
        EnterCriticalSection(&g_cs);

        // capture the flag values a real streamed call uses
        if (!g_captured) { g_b1 = b1; g_b2 = b2; g_captured = true; logf("captured flags: b1=%d b2=%d", (int)b1, (int)b2); }

        // once the stream system is live (first call), register our files as base slot overrides.
        // o_regRaw is the trampoline (original), so these calls do NOT re-enter this hook.
        if (!g_off && !g_didRegister)
        {
            g_didRegister = true;
            // the pool must exist by now (this very call registers into it); if it looks wrong,
            // the manager pattern matched the wrong code — better no re-assert than a wild write
            if (g_mgr && (!g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)) {
                logf("streaming pool looks wrong (entries=%p num=%d) — re-assert disabled", (void*)g_mgr->entries, g_mgr->numEntries);
                g_mgr = nullptr;
            }
            int done = 0;
            for (auto& ov : g_ovs)
            {
                uint32_t id = 0xFFFFFFFF;
                o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
                ov.id = id;
                if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
                    ov.handle = g_mgr->entries[id].handle;
                if (++done <= 60) logf("OVERRIDE-REG  %s  <-  tex_overrides/%s  (id=%u handle=%08x)", ov.slot, rel(ov.file), id, ov.handle);
            }
            logf("registered %d base-slot override(s)", done);
            if (g_mgr) logf("streaming pool: entries=%p numEntries=%d", (void*)g_mgr->entries, g_mgr->numEntries);
            InterlockedExchange(&g_idsReady, 1);
        }

        // MAP: record each distinct collection the server streams, tagged with whether we'd ever touch it
        std::string keyLower = lower(asName);
        std::string coll = collectionOf(keyLower);
        if (g_collSeen.insert(coll).second && g_collSeen.size() <= 500)
            logf("collection: %-40s [%s]", coll.c_str(),
                 isAllowedKey(keyLower) ? "overridable" : "OTHER - never touched");

        LeaveCriticalSection(&g_cs);
    }

    // redirect only exact-slot matches, and only for keys the gate allows (double guard):
    // freemode-ped collection slots, or bare-name .ytd dictionaries.
    // g_bySlot is written once at startup (before the hook is live), so this read needs no lock.
    if (!g_off && asName)
    {
        std::string key = lower(asName);
        if (isAllowedKey(key))
        {
            auto it = g_bySlot.find(key);
            if (it != g_bySlot.end())
            {
                InterlockedIncrement(&g_redirects);
                if (g_redirects <= 100) logf("REDIRECT  %s  ->  tex_overrides/%s", asName, rel(it->second));
                return o_regRaw(fileId, it->second, b1, asName, b2);
            }
        }
    }
    return o_regRaw(fileId, name, b1, asName, b2);
}

static DWORD WINAPI BeatLoop(LPVOID);

// Runs synchronously inside asi-five's LoadLibrary call. FiveM loads plugins in
// LauncherInterface::PostLoadGame, which returns the game's entry point to the launcher — the
// entry point has NOT run yet, so no thread anywhere is executing game code. That makes the hook
// patch race-free by construction, with no thread freezing: the same guarantee FiveM relies on
// for its own HookFunction patches in that window. (MinHook's freeze never worked under FiveM
// anyway — CreateToolhelp32Snapshot is blocked — so before this it was safe only by luck.)
static void Setup()
{
    char self[MAX_PATH]; GetModuleFileNameA(g_self, self, MAX_PATH);
    std::string dir = self; size_t slash = dir.find_last_of('\\');
    std::string plug = (slash==std::string::npos) ? dir : dir.substr(0, slash);
    _snprintf_s(g_logPath, MAX_PATH, _TRUNCATE, "%s\\texoverride.log", plug.c_str());
    _snprintf_s(g_overrideDir, MAX_PATH, _TRUNCATE, "%s\\tex_overrides\\", plug.c_str());
    { std::string off = std::string(g_overrideDir) + "_OFF";
      g_off = (GetFileAttributesA(off.c_str()) != INVALID_FILE_ATTRIBUTES); }

    InitializeCriticalSection(&g_cs);   // must exist before the hook can fire

    DeleteFileA(g_logPath);   // fresh log every launch, so a bug report is small and current
    { time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
      char d[32]; strftime(d, sizeof d, "%Y-%m-%d", &tm);
      logf("================ texoverride " TEXOVERRIDE_VERSION " loaded (%s) ================", d); }
    int n = scanDir(std::string(g_overrideDir), "");
    logf("loaded %d override(s); mode %s", n, g_off ? "OFF" : "ON");
    { std::unordered_map<std::string, int> per;   // per-collection tally, the first thing to check in a report
      for (auto& ov : g_ovs) ++per[collectionOf(ov.slot)];
      for (auto& kv : per) logf("  %-40s %d file(s)", kv.first.c_str(), kv.second); }

    // tattoo placement files: *.xml at the root of tex_overrides (edited overlays.xml copies)
    { WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((std::string(g_overrideDir) + "*.xml").c_str(), &fd);
      if (h != INVALID_HANDLE_VALUE) {
          do { parsePlacementXml(std::string(g_overrideDir) + fd.cFileName, fd.cFileName); } while (FindNextFileA(h, &fd));
          FindClose(h);
      } }
    for (auto& pc : g_pl) logf("placement: collection %s — %zu preset(s) from %s", pc.name.c_str(), pc.presets.size(), pc.src.c_str());

    // rage::strStreamingEngine::ms_info, via the lea in Cfx's g_storeMgr pattern (Streaming.cpp)
    const short PAT_MGR[] = { 0x74,0x1A,0x8B,0x15,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0x41 };
    if (uint8_t* q = scanModule(PAT_MGR, 16)) {
        g_mgr = (StrMgr*)ripTarget(q + 11);
        logf("streaming manager @ %p", (void*)g_mgr);
    }
    else logf("manager pattern NOT FOUND — claims still register, but nothing can re-assert them");

    const short PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT, sizeof PAT / sizeof *PAT);
    if (!p) { logf("pattern NOT FOUND"); }
    else {
        void* target = (void*)(p - 0x25);
        logf("registerRawStreamingFile @ %p", target);
        MH_STATUS s = MH_Initialize();                                    logf("MH_Initialize: %s", MH_StatusToString(s));
        s = MH_CreateHook(target, (void*)&h_regRaw, (void**)&o_regRaw);   logf("MH_CreateHook: %s", MH_StatusToString(s));
        s = MH_EnableHook(MH_ALL_HOOKS);                                  logf("MH_EnableHook: %s", MH_StatusToString(s));
        logf(g_off ? "hooked, disabled" : "LIVE — will register base overrides on first stream call");
    }
}

// ---- update check: one HTTPS ask at startup, "what is the newest release tag?" ----
// Sends nothing except the request itself. Fails silently when offline. Skipped when _OFF or
// _NO_UPDATE_CHECK exists in tex_overrides. Runs on its own thread so a shown popup never
// blocks the re-assert loop.
static int verCmp(const char* a, const char* b)   // >0 when a is newer than b
{
    int A[3] = {}, B[3] = {};
    sscanf_s(a, "%d.%d.%d", &A[0], &A[1], &A[2]);
    sscanf_s(b, "%d.%d.%d", &B[0], &B[1], &B[2]);
    for (int i = 0; i < 3; ++i) if (A[i] != B[i]) return A[i] - B[i];
    return 0;
}

static DWORD WINAPI UpdateCheck(LPVOID)
{
    if (g_off) return 0;
    if (GetFileAttributesA((std::string(g_overrideDir) + "_NO_UPDATE_CHECK").c_str()) != INVALID_FILE_ATTRIBUTES) return 0;

    std::string body;
    HINTERNET s = WinHttpOpen(L"texoverride", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/blancodagoat/texoverride/releases/latest",
                                         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE) : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
          && WinHttpReceiveResponse(r, nullptr)) {
        char buf[4096]; DWORD got = 0;
        while (WinHttpReadData(r, buf, sizeof buf, &got) && got) body.append(buf, got);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);

    // pull the version out of "tag_name":"v0.3.0" without a JSON library
    std::string latest;
    size_t k = body.find("\"tag_name\"");
    if (k != std::string::npos) {
        size_t q1 = body.find('"', body.find(':', k) + 1);
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
        if (q2 != std::string::npos) latest = body.substr(q1 + 1, q2 - q1 - 1);
    }
    if (latest.empty()) { logf("update check: could not reach GitHub (offline?)"); return 0; }
    const char* lv = (latest[0] == 'v' || latest[0] == 'V') ? latest.c_str() + 1 : latest.c_str();

    if (verCmp(lv, TEXOVERRIDE_VERSION) > 0) {
        logf("update check: %s is out (you have " TEXOVERRIDE_VERSION ")", latest.c_str());
        char msg[256];
        _snprintf_s(msg, sizeof msg, _TRUNCATE,
            "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\n"
            "Get it at:\ngithub.com/blancodagoat/texoverride/releases\n\n"
            "To turn this check off, create a file named _NO_UPDATE_CHECK in tex_overrides.",
            latest.c_str());
        MessageBoxA(nullptr, msg, "texoverride update", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
    }
    else logf("update check: up to date (latest %s)", latest.c_str());
    return 0;
}

static DWORD WINAPI BeatLoop(LPVOID)
{
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            // re-assert: DLC mounts and FiveM's loader re-point claimed slots after us; whoever
            // writes the handle last wins, so write ours back. Same mechanism Cfx's own override
            // path uses (LoadStreamingFile.cpp writes Entries[].handle directly).
            if (!g_off && g_idsReady && g_mgr && g_mgr->entries) {
                for (auto& ov : g_ovs) {
                    if (!ov.handle || ov.id >= (uint32_t)g_mgr->numEntries) continue;
                    StrEntry& e = g_mgr->entries[ov.id];
                    if (e.handle == ov.handle) continue;
                    if ((e.flags & 3) >= 2) { ++g_deferred; continue; }   // being requested/loaded right now; retry next tick
                    uint32_t old = e.handle;
                    e.handle = ov.handle;
                    if (++g_reclaims <= 60) logf("RECLAIM  %s  (%08x -> %08x)", ov.slot, old, ov.handle);
                }
            }
            if (!g_off) placementBeatSafe();   // apply/re-assert tattoo placement edits
        }
        logf("alive (beat %d) — reg=%ld redirects=%ld reclaims=%ld deferred=%ld baseRegistered=%s",
             beat, (long)g_regTotal, (long)g_redirects, g_reclaims, g_deferred, g_didRegister ? "yes" : "no");
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h; DisableThreadLibraryCalls(h);
        Setup();   // synchronous: must finish before the game's entry point runs (see Setup)
        CreateThread(nullptr, 0, BeatLoop, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
    }
    return TRUE;
}
