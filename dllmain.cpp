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
// SAFETY: only human freemode-ped collections (mp_m_freemode_01*, mp_f_freemode_01*) are ever
// touched. Anything else — animal peds, story/ambient peds, vehicles, weapons, props, maps,
// scripts — is refused at load and skipped at runtime. It also logs every distinct collection the
// server streams (tagged), so you can see exactly what is in reach and what is not.

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
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

#define TEXOVERRIDE_VERSION "0.2.0"

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
            if (ln.size() > 4 && (ln.compare(ln.size()-4,4,".ytd")==0 || ln.compare(ln.size()-4,4,".ydd")==0) && !rel.empty()) {
                std::string slotStr = lower(fwd(childRel));                    // "mp_m_freemode_01/teef_004_u.ydd"
                // SAFETY GATE: refuse any override folder that is not a human freemode-ped collection.
                if (!isFreemodePed(collectionOf(slotStr))) {
                    logf("SKIP (not a freemode-ped collection, left alone): %s", slotStr.c_str());
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
        std::string coll = collectionOf(lower(asName));
        if (g_collSeen.insert(coll).second && g_collSeen.size() <= 500)
            logf("collection: %-40s [%s]", coll.c_str(),
                 isFreemodePed(coll) ? "freemode-ped (overridable)" : "OTHER - never touched");

        LeaveCriticalSection(&g_cs);
    }

    // redirect only exact-slot matches, and only within freemode-ped collections (double guard).
    // g_bySlot is written once at startup (before the hook is live), so this read needs no lock.
    if (!g_off && asName)
    {
        std::string key = lower(asName);
        if (isFreemodePed(collectionOf(key)))
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

static DWORD WINAPI Init(LPVOID)
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
        }
        logf("alive (beat %d) — reg=%ld redirects=%ld reclaims=%ld deferred=%ld baseRegistered=%s",
             beat, (long)g_regTotal, (long)g_redirects, g_reclaims, g_deferred, g_didRegister ? "yes" : "no");
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h; DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    }
    return TRUE;
}
