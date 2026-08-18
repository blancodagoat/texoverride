// texoverride — v7: proactive base override, with a freemode-ped safety gate
//
// Base freemode components live inside x64v.rpf (RPF-resident), so they never pass through
// registerRawStreamingFile — that function is only for loose/streamed files. To replace a base
// component we do what a server stream/ folder does: register OUR loose file UNDER the base slot
// name, so it overrides the x64v-resident one. The ASI calls registerRawStreamingFile itself for
// each override, using the same flags a real call uses (captured live).
//
//   override at  tex_overrides\mp_m_freemode_01\teef_004_u.ydd
//   -> registered as slot  "mp_m_freemode_01/teef_004_u.ydd"  reading from our file
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

struct Ov { const char* slot; const char* file; };   // both persistent, forward-slash, lowercased
static std::vector<Ov> g_ovs;
static std::unordered_map<std::string, const char*> g_bySlot;   // slot -> file
static std::unordered_set<std::string> g_collSeen;   // distinct collections, for the map

static volatile LONG g_regTotal = 0, g_redirects = 0;
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

#define TEXOVERRIDE_VERSION "0.1.0"

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

static uint8_t* scanModule(const uint8_t* pat, size_t len)
{
    HMODULE mod = GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)mod;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* b = (uint8_t*)mod + sec->VirtualAddress;
        size_t sz = sec->Misc.VirtualSize;
        for (size_t j = 0; j + len <= sz; ++j)
            if (memcmp(b + j, pat, len) == 0) return b + j;
    }
    return nullptr;
}

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
            int done = 0;
            for (auto& ov : g_ovs)
            {
                uint32_t id = 0;
                o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
                if (++done <= 60) logf("OVERRIDE-REG  %s  <-  tex_overrides/%s  (id=%u)", ov.slot, rel(ov.file), id);
            }
            logf("registered %d base-slot override(s)", done);
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

    const uint8_t PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT, sizeof PAT);
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
        Sleep(15000);
        logf("alive (beat %d) — reg=%ld redirects=%ld baseRegistered=%s",
             beat, (long)g_regTotal, (long)g_redirects, g_didRegister ? "yes" : "no");
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
