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
//
// v0.5.0 adds live reload: a watcher thread reacts when tex_overrides changes (event-driven, no
// polling) — edited placement xml applies in-game within a second, new files register mid-session
// the same way Cfx does on resource restarts, overwritten files get their raw entry re-statted.
// Game-touching work runs on the game's MAIN thread (queued, drained via a PeekMessageW IAT
// shim), matching Cfx's own threading; a journal + quarantine (crash saver) makes sure a file
// that crashes the game is not loaded again on the next launch. See the live-reload section.

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <dxgi1_4.h>   // IDXGIAdapter3::QueryVideoMemoryInfo: Windows' own per-process VRAM budget
// no dxgi import lib: a static import resolves against the APPLICATION directory first, and a
// ReShade/ENB dxgi.dll in the FiveM folder then breaks the load of this whole plugin with
// "Couldn't load texoverride.asi". probeVram() loads the real one from System32 instead.
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <exception>
#include <new>
#include "MinHook.h"

static HINSTANCE g_self;
static char g_logPath[MAX_PATH], g_overrideDir[MAX_PATH];
static bool g_off = false;

struct Ov {
    const char* slot; const char* file;   // both persistent, forward-slash, lowercased
    uint32_t id = 0xFFFFFFFF;             // global streaming index our claim landed on
    uint32_t handle = 0;                  // the handle value that points at OUR file
    HANDLE lease = INVALID_HANDLE_VALUE;  // read-only process-lifetime lock on validated bytes
};
static std::vector<Ov> g_ovs;
static HANDLE g_scanDone = nullptr;   // set once g_ovs is final; the hook waits on it
static HANDLE g_registerDone = nullptr; // set after the one-time engine registration pass
static std::unordered_map<std::string, const char*> g_bySlot;   // slot -> file
static std::unordered_map<std::string, size_t> g_indexBySlot;   // slot -> stable g_ovs index
static std::unordered_set<std::string> g_collSeen;   // distinct collections, for the map
static std::unordered_set<std::string> g_quarantine; // crash saver: keys refused this session
static bool g_crashSaverRan = false;                  // gates journal deletion on orderly exit
static char g_inflightPath[MAX_PATH], g_quarantinePath[MAX_PATH];

// ---- streaming-cost audit ------------------------------------------------------------------
// A .ytd/.ydd on disk is an RSC7 resource: dwords 2 (virtual) and 3 (physical) of the 16-byte
// header encode the exact memory the streamer charges while the file is resident. Decode is
// CodeWalker's GetSizeFromFlags (RpfFile.cs). Physical is the texture-budget hit — "texture
// loss" on heavy servers is that budget running dry, so the scan totals what the pack costs
// and names the heavy files. Threshold: 8 MB catches any 4K texture and 2K uncompressed;
// vanilla clothing txds sit well under 2 MB.
static uint64_t rscSizeFromFlags(uint32_t f)
{
    // 64-bit throughout: with the max page shift the 32-bit product wraps at 4 GB, and a
    // corrupt header could then report a tiny size and slip under the TOO BIG gate
    uint64_t pages = ((f >> 27) & 0x1)
                   + (((f >> 26) & 0x1)  << 1)
                   + (((f >> 25) & 0x1)  << 2)
                   + (((f >> 24) & 0x1)  << 3)
                   + (((f >> 17) & 0x7F) << 4)
                   + (((f >> 11) & 0x3F) << 5)
                   + (((f >> 7)  & 0xF)  << 6)
                   + (((f >> 5)  & 0x3)  << 7)
                   + (((f >> 4)  & 0x1)  << 8);
    return (0x200ull << (f & 0xF)) * pages;
}
static void probeVram();   // defined with the budget raiser below; fills g_vramTotal/g_vramBudget
static uint64_t g_costVirt, g_costPhys;
static std::vector<std::pair<uint64_t, std::string>> g_costBig;   // files >= 8 MB in memory

static void logf(const char* fmt, ...);

// One crash gate for every load path (startup scan, live new file, live overwrite): refuse
// anything with more than 32 MB in EITHER resource segment. CONFIRMED cause of the
// summer-maine-steak crash twice over: five files with 64 MB graphics segments (removal test),
// then a player crashing on mesh files whose 77+ MB sat in the virtual segment while graphics
// stayed under the line — same crash address both times. 32 MB per segment is verified fine.
// A missing, unreadable or malformed header is rejected. The compressed on-disk size is not a
// safe substitute for decoded RSC memory and must never let an unknown resource through.
// Reading the header and judging it are separate so the startup scan can collect costs first
// and then judge them deterministically in file order.
enum class CostState : uint8_t { ValidRsc, Unreadable, InvalidHeader };
struct Cost { uint64_t cv = 0, cp = 0, disk = 0; CostState state = CostState::Unreadable; };
static Cost readCost(const char* path)
{
    Cost c;
    HANDLE fh = CreateFileA(path, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (fh == INVALID_HANDLE_VALUE) return c;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(fh, &size) && size.QuadPart >= 0) c.disk = (uint64_t)size.QuadPart;
    uint32_t header[4] = {};
    DWORD got = 0;
    bool read = ReadFile(fh, header, sizeof header, &got, nullptr) && got == sizeof header;
    CloseHandle(fh);
    if (!read || header[0] != 0x37435352 || c.disk <= sizeof header) { c.state = CostState::InvalidHeader; return c; }
    c.cv = rscSizeFromFlags(header[2]);
    c.cp = rscSizeFromFlags(header[3]);
    if (!c.cv && !c.cp) { c.state = CostState::InvalidHeader; return c; }
    c.state = CostState::ValidRsc;
    return c;
}
static bool tooBigJudge(const char* key, const Cost& c, bool quiet)
{
    if (c.state == CostState::Unreadable) {
        if (!quiet) logf("UNREADABLE %s — cannot open it, so it is not loaded this launch", key);
        return true;
    }
    if (c.state == CostState::InvalidHeader) {
        if (!quiet) logf("INVALID %s — not a complete RSC7 resource, so it is not loaded", key);
        return true;
    }
    uint64_t worst = (c.cv > c.cp) ? c.cv : c.cp;
    if (worst <= (32ull << 20)) return false;
    if (!quiet) logf("TOO BIG  %s — %.1f MB of %s data; files this big are the confirmed cause of game crashes, so it is NOT loaded. Shrink it (CodeWalker, Tools, Shrink Textures).",
                     key, worst / 1048576.0, (c.cv > c.cp) ? "mesh" : "texture");
    return true;
}
// Revalidate immediately before a game call and keep the file read-only for the duration of
// that call. This closes the validation/use window: an editor, sync tool or malicious junction
// cannot replace the resource after its RSC7 header passes the gate but before registration or
// re-stat consumes the path. The game may still open the file for reading while this lease lives.
static HANDLE acquireSafeResource(const char* path, const char* key, bool quiet, Cost* outCost = nullptr)
{
    HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        Cost c; c.state = CostState::Unreadable;
        tooBigJudge(key, c, quiet);
        return INVALID_HANDLE_VALUE;
    }
    Cost c;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(fh, &size) && size.QuadPart >= 0) c.disk = (uint64_t)size.QuadPart;
    uint32_t header[4] = {};
    DWORD got = 0;
    if (ReadFile(fh, header, sizeof header, &got, nullptr) && got == sizeof header &&
        header[0] == 0x37435352 && c.disk > sizeof header) {
        c.cv = rscSizeFromFlags(header[2]);
        c.cp = rscSizeFromFlags(header[3]);
        c.state = (c.cv || c.cp) ? CostState::ValidRsc : CostState::InvalidHeader;
    }
    else c.state = CostState::InvalidHeader;
    if (outCost) *outCost = c;
    if (tooBigJudge(key, c, quiet)) { CloseHandle(fh); return INVALID_HANDLE_VALUE; }
    return fh;
}

// rage::strStreamingEngine::ms_info — the streaming info pool. Entries[id].handle is what the
// loader actually opens; layout from Cfx's gta-streaming-five/include/Streaming.h.
struct StrEntry { uint32_t handle, flags; };
struct StrMgr   { StrEntry* entries; char pad[16]; int numEntries; };
static StrMgr* g_mgr = nullptr;

static volatile LONG g_regTotal = 0, g_redirects = 0, g_idsReady = 0;
static volatile LONG g_reclaims = 0, g_deferred = 0;
static volatile LONG g_reassertPending = 0, g_reassertBusy = 0, g_reassertFault = 0;
static size_t g_reassertCursor = 0;           // protected by g_reassertBusy + g_cs
static volatile LONGLONG g_lastPumpAt = 0, g_lastPumpWorkAt = 0;
static volatile LONG g_pumpUnavailable = 0;
enum : LONG { REG_NOT_STARTED = 0, REG_IN_PROGRESS = 1, REG_DONE = 2, REG_FAILED = 3 };
static volatile LONG g_reclaimBatches = 0, g_registrationState = REG_NOT_STARTED;
static bool g_b1 = true, g_b2 = false, g_captured = false;
static CRITICAL_SECTION g_cs;   // guards the one-time registration + the collection map (hook may run on >1 thread)
class CsGuard {
public:
    explicit CsGuard(CRITICAL_SECTION& cs) : m_cs(&cs) { EnterCriticalSection(m_cs); }
    ~CsGuard() { LeaveCriticalSection(m_cs); }
    CsGuard(const CsGuard&) = delete;
    CsGuard& operator=(const CsGuard&) = delete;
private:
    CRITICAL_SECTION* m_cs;
};
static SRWLOCK g_logLock = SRWLOCK_INIT;
static FILE* g_logFile = nullptr;

static LONG atomicRead(volatile LONG* value) { return InterlockedCompareExchange(value, 0, 0); }

static void logf(const char* fmt, ...)
{
    AcquireSRWLockExclusive(&g_logLock);
    if (!g_logFile && (fopen_s(&g_logFile, g_logPath, "a") != 0 || !g_logFile)) {
        ReleaseSRWLockExclusive(&g_logLock);
        return;
    }
    time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
    char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    fprintf(g_logFile, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(g_logFile, fmt, ap); va_end(ap);
    fputc('\n', g_logFile);
    fflush(g_logFile);   // crash diagnostics stay complete without reopening the file every line
    ReleaseSRWLockExclusive(&g_logLock);
}

#define TEXOVERRIDE_VERSION "0.7.3-emk.1"

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

// The walk itself opens nothing: it only decides which names are ours.
struct Cand { std::string slot, full; Cost c; };
static void walkDir(const std::string& base, const std::string& rel, std::vector<Cand>& out)
{
    std::string pattern = base + rel + "\\*";
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string childRel = rel.empty() ? name : rel + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                logf("SKIP directory junction/symlink: %s", fwd(childRel).c_str());
                continue;
            }
            walkDir(base, childRel, out); continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            logf("SKIP file junction/symlink: %s", fwd(childRel).c_str());
            continue;
        }
        std::string ln = lower(name);
        // .meta (shop_tattoo.meta etc.) is shop data, not looks — not applied yet, but the
        // pack-folder layout (tex_overrides/mplowrider/shop_tattoo.meta) is the reserved
        // convention for it, so acknowledge the file instead of silently skipping it
        if (ln.size() > 5 && ln.compare(ln.size()-5, 5, ".meta") == 0) {
            logf("IGNORED %s — .meta files hold shop data (prices/menus), not looks; see README", fwd(childRel).c_str());
            continue;
        }
        if (ln.size() <= 4 || (ln.compare(ln.size()-4,4,".ytd") != 0 && ln.compare(ln.size()-4,4,".ydd") != 0)) continue;
        std::string slotStr = lower(fwd(childRel));   // "mp_m_freemode_01/teef_004_u.ydd" or bare "mp_fm_skin_m_up_whi.ytd"
        // SAFETY GATE: folders must be freemode-ped collections; root files must be .ytd.
        if (!isAllowedKey(slotStr)) {
            logf("SKIP (folders must be freemode-ped collections, root files must be .ytd): %s", slotStr.c_str());
            continue;
        }
        if (g_quarantine.count(slotStr)) continue;   // crash saver; already logged loudly
        out.push_back({ slotStr, fwd(base + childRel), Cost() });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// A local balanced pack scans in about a tenth of a second. Keeping this sequential avoids child
// workers outliving stack-owned scan state after a device/file-system fault, while the whole scan
// still runs off the loader lock on the beat thread.
static void readCosts(std::vector<Cand>& v)
{
    for (auto& candidate : v) candidate.c = readCost(candidate.full.c_str());
}

static void costReport();   // defined with the budget code, which it compares the pack against
enum : LONG { SCAN_SCANNING = 0, SCAN_READY = 1, SCAN_FAILED = 2 };
static volatile LONG g_scanState = SCAN_SCANNING;

// Runs on the beat thread, NOT in DllMain. Everything here opens files, and Setup() sits in
// front of the game's entry point: doing it there meant a big pack held the loading screen
// before the game had even started. The hook waits on g_scanDone before it registers anything,
// so the game gets on with its own startup while this runs.
static bool scanFinish()
{
    ULONGLONG t0 = GetTickCount64();
    std::vector<Cand> cands;
    walkDir(std::string(g_overrideDir), "", cands);
    logf("found %zu file(s); reading their headers in the background while the game starts", cands.size());
    ULONGLONG tw = GetTickCount64();
    readCosts(cands);
    ULONGLONG tr = GetTickCount64();

    // Build the complete registry privately. If allocation or parsing fails, the hook never sees
    // a half-published pack and cannot deadlock on a critical section abandoned by SEH.
    std::vector<Ov> ovs;
    std::unordered_map<std::string, const char*> bySlot;
    std::unordered_map<std::string, size_t> byIndex;
    ovs.reserve(cands.size()); bySlot.reserve(cands.size()); byIndex.reserve(cands.size());
    int n = 0;
    for (auto& cd : cands) {
        if (tooBigJudge(cd.slot.c_str(), cd.c, false)) continue;
        const char* slot = _strdup(cd.slot.c_str());
        const char* file = _strdup(cd.full.c_str());   // our absolute path
        if (!slot || !file) { free((void*)slot); free((void*)file); throw std::bad_alloc(); }
        ovs.push_back({ slot, file });
        bySlot[slot] = file;
        byIndex[slot] = ovs.size() - 1;
        ++n;
        if (cd.c.state == CostState::ValidRsc) {
            g_costVirt += cd.c.cv; g_costPhys += cd.c.cp;
            if (cd.c.cv + cd.c.cp >= (8u << 20)) g_costBig.push_back({ cd.c.cv + cd.c.cp, cd.slot });
        }
    }

    bool published = false;
    {
        CsGuard lock(g_cs);
        g_ovs.swap(ovs); g_bySlot.swap(bySlot); g_indexBySlot.swap(byIndex);
        // Publish the complete registry before READY becomes observable. A game thread that sees
        // READY must then acquire g_cs, so keeping the lock through the transition guarantees it
        // cannot register an empty or partially published pack. If startup already timed out, put
        // the old registry back and discard this private result.
        published = InterlockedCompareExchange(&g_scanState, SCAN_READY, SCAN_SCANNING) == SCAN_SCANNING;
        if (!published) {
            g_ovs.swap(ovs); g_bySlot.swap(bySlot); g_indexBySlot.swap(byIndex);
        }
    }
    if (!published) {
        for (auto& ov : ovs) { free((void*)ov.slot); free((void*)ov.file); }
        logf("file scan finished after startup had already abandoned it; overrides stay disabled this session");
        return false;
    }
    if (g_scanDone && !SetEvent(g_scanDone))
        logf("file scan is ready but signaling its event failed (err %lu); state polling remains available", GetLastError());
    logf("loaded %d override(s) in %.1fs (walk %.1fs, headers %.1fs); mode %s", n,
         (GetTickCount64() - t0) / 1000.0, (tw - t0) / 1000.0, (tr - tw) / 1000.0,
         g_off ? "OFF" : "ON");
    { std::unordered_map<std::string, int> per;   // per-collection tally, the first thing to check in a report
      for (auto& ov : g_ovs) ++per[collectionOf(ov.slot)];
      for (auto& kv : per) logf("  %-40s %d file(s)", kv.first.c_str(), kv.second); }
    costReport();
    return true;
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

static void parsePlacementXml(const std::string& path, const char* fname, std::vector<PlColl>& out)
{
    FILE* f; if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return;
    std::string x; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (16 << 20)) {
        fclose(f);
        logf("placement: %s is empty or over the 16 MB XML limit, ignored", fname);
        return;
    }
    x.resize((size_t)n); x.resize(fread(&x[0], 1, (size_t)n, f)); fclose(f);

    auto text = [&](size_t from, size_t to, const char* tag, std::string& out) -> bool {
        std::string open = std::string("<") + tag + ">";
        size_t a = x.find(open, from); if (a == std::string::npos || a >= to) return false;
        a += open.size(); size_t b = x.find('<', a); if (b == std::string::npos || b >= to) return false;
        out = x.substr(a, b - a); return true;
    };
    auto attrf = [&](size_t from, size_t to, const char* tag, const char* attr, float& out) -> bool {
        std::string open = std::string("<") + tag;
        size_t a = x.find(open, from); if (a == std::string::npos || a >= to) return false;
        size_t e = x.find('>', a); if (e == std::string::npos || e >= to) return false;
        std::string key = std::string(attr) + "=\"";
        size_t v = x.find(key, a); if (v == std::string::npos || v >= e) return false;
        size_t q = x.find('"', v + key.size());
        if (q == std::string::npos || q > e) return false;
        const char* begin = x.c_str() + v + key.size(); char* end = nullptr;
        float parsed = strtof(begin, &end);
        if (end == begin || end != x.c_str() + q || !std::isfinite(parsed) || fabsf(parsed) > 1000.0f) return false;
        out = parsed; return true;
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
        if (ok) {
            if (pc.presets.size() >= 65535) { logf("placement: %s exceeds 65535 presets, ignored", fname); return; }
            p.hash = joaat(nm.c_str(), nm.size()); pc.presets.push_back(p);
        }
        pos = end + 7;
    }
    if (pc.presets.size() < 3) { logf("placement: %s has %zu preset(s), need 3+ to fingerprint, ignored", fname, pc.presets.size()); return; }
    out.push_back(std::move(pc));
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

// ============================ live reload (watch tex_overrides) ============================
// A watcher thread sits on FindFirstChangeNotification: fully event-driven, no polling. When the
// folder changes it waits half a second for the writes to go quiet, then rescans once.
//
//   - edited placement .xml  -> re-parsed, layout carried over, applied by the next beat
//   - overwritten .ytd/.ydd  -> raw-streamer entry invalidated so the next load re-reads the file
//                               (Cfx's own trick: timestamp = 0, then GetEntry re-stats — see
//                               LoadStreamingFile.cpp; the game shows it when the item reloads)
//   - brand-new file         -> registered mid-session, exactly what Cfx does on every resource
//                               restart (CfxCollection_AddStreamingFileByTag -> LoadStreamingFiles)
//
// THREADING — the two game-touching operations (register, re-stat) never run on the watcher
// thread. RAGE's registration path (strStreamingInfoManager::RegisterObject -> module->Register)
// inserts into name tables with no lock, and the game's main thread reads those tables all the
// time; that is why Cfx runs its own mid-session registrations on the main thread
// (OnMainGameFrame). We reach the same thread without another code hook: the game's main loop
// pumps messages through its user32 PeekMessageW import every frame (Cfx's ZOffThreadWindowing
// IAT-hooks that exact import), so the watcher queues work and our own IAT shim on that import
// drains the queue on the main thread. An IAT swap is a pointer write in the exe's import table,
// the same mechanism every overlay uses.
//
// CRASH SAVER — before a batch is handed to the game thread it is journaled to
// tex_overrides\_inflight.txt, and the journal stays for 30 seconds after it applies (a broken
// texture usually crashes within moments of streaming in). If the game dies inside that window,
// the next launch moves those names into _quarantine.txt, refuses to load them, and says so in
// the log. Deleting _quarantine.txt lifts it. A bad file can crash at most one session.

typedef void* (*GetRawStreamer_t)();          // returns the game's pgRawStreamer instance
typedef uint8_t* (*RawGetEntry_t)(void*, uint16_t);   // pgRawStreamer::GetEntry(index)
static GetRawStreamer_t g_getRawStreamerFn = nullptr;
static RawGetEntry_t    g_rawGetEntryFn = nullptr;
static bool g_watcherStarted = false;

struct LiveOp { int kind; Ov ov; uint32_t handle; Cost cost; }; // kind: 0 = register, 1 = re-stat
static std::deque<LiveOp> g_opQ;                                // guarded by g_cs
static volatile LONG g_opsPending = 0;                          // batch queued, not yet drained
static ULONGLONG g_journalClearAt = 0;                          // watcher thread only

// Overwritten file: zero the raw entry's timestamp, then GetEntry re-stats it (new size picked
// up). Without this an overwritten file keeps its old cached size — short or wild reads.
// RawEntry layout from Cfx fiCollectionWrapper.h: fe 16 bytes, timestamp at +16.
static bool rawInvalidate(uint32_t handle)
{
    __try {
        void* rs = g_getRawStreamerFn();
        if (!rs) return false;
        uint16_t idx = (uint16_t)(handle & 0xFFFF);
        uint8_t* e = g_rawGetEntryFn(rs, idx);
        if (!e) return false;
        *(uint64_t*)(e + 16) = 0;
        g_rawGetEntryFn(rs, idx);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Register one new file mid-session, same call and flags as the startup pass. SEH: a fault here
// must not take the game down; worst case the file just is not picked up.
// Returns why it failed, because the three reasons need three different answers:
//   0 ok  1 slot already owned  2 registered but no handle came back  3 fault
static int liveRegister(Ov& ov)
{
    __try {
        uint32_t id = 0xFFFFFFFF;
        o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
        ov.id = id;
        if (id == 0xFFFFFFFF) return 1;   // the game refuses a slot that already holds a handle
        if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
            ov.handle = g_mgr->entries[id].handle;
        return ov.handle ? 0 : 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

// ---- main-thread pump: IAT shim on the game exe's PeekMessageW import ----
typedef BOOL (WINAPI* PeekMsg_t)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMsg_t g_origPeek = nullptr;
static PVOID volatile* g_peekSlot = nullptr;
static DWORD g_gameTid = 0;   // DllMain caller continues into the game entry point (see Setup)

// Inspect and update one engine slot behind an SEH boundary containing only POD locals. A bad
// signature or stale manager pointer disables reassertion instead of crashing FiveM. The 32-bit
// handle exchange is aligned and atomic; the request/loading flag is checked immediately first.
static int reassertOne(const Ov& ov, uint32_t* old)
{
    __try {
        if (!g_mgr || !g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)
            return -1;
        if (!ov.handle || ov.id >= (uint32_t)g_mgr->numEntries) return 0;
        StrEntry& e = g_mgr->entries[ov.id];
        if (e.handle == ov.handle) return 0;
        if ((e.flags & 3) >= 2) return 2;
        *old = e.handle;
        InterlockedExchange((volatile LONG*)&e.handle, (LONG)ov.handle);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Commit a successful live registration as one transaction. Allocation failures roll every
// container back while the RAII guard guarantees the global lock is never abandoned.
static bool commitLiveAdd(const Ov& ov)
{
    try {
        CsGuard lock(g_cs);
        size_t index = g_ovs.size();
        g_ovs.push_back(ov);
        try {
            if (!g_bySlot.emplace(ov.slot, ov.file).second) { g_ovs.pop_back(); return false; }
            if (!g_indexBySlot.emplace(ov.slot, index).second) {
                g_bySlot.erase(ov.slot); g_ovs.pop_back(); return false;
            }
        }
        catch (...) {
            g_bySlot.erase(ov.slot);
            g_indexBySlot.erase(ov.slot);
            g_ovs.pop_back();
            throw;
        }
        return true;
    }
    catch (...) {
        logf("live reload: out of memory while committing %s; restart to pick it up", ov.slot);
        return false;
    }
}

static void reassertShard(size_t limit)
{
    if (!InterlockedCompareExchange(&g_reassertPending, 0, 0) || InterlockedCompareExchange(&g_reassertFault, 0, 0) ||
        InterlockedCompareExchange(&g_reassertBusy, 1, 0) != 0) return;

    const char* firstSlot = nullptr; uint32_t firstOld = 0, firstNew = 0;
    size_t reclaimed = 0, deferred = 0;
    {
        CsGuard lock(g_cs);
        size_t done = 0;
        while (g_reassertCursor < g_ovs.size() && done++ < limit) {
            Ov& ov = g_ovs[g_reassertCursor++]; uint32_t old = 0;
            int result = reassertOne(ov, &old);
            if (result < 0) { InterlockedExchange(&g_reassertFault, 1); break; }
            if (result == 2) { ++deferred; continue; }
            if (result == 1) {
                if (!firstSlot) { firstSlot = ov.slot; firstOld = old; firstNew = ov.handle; }
                ++reclaimed;
            }
        }
        if (g_reassertCursor >= g_ovs.size() || InterlockedCompareExchange(&g_reassertFault, 0, 0)) {
            g_reassertCursor = 0;
            InterlockedExchange(&g_reassertPending, 0);
        }
    }
    InterlockedExchange(&g_reassertBusy, 0);

    InterlockedExchangeAdd(&g_reclaims, (LONG)reclaimed);
    InterlockedExchangeAdd(&g_deferred, (LONG)deferred);
    if (reclaimed && InterlockedIncrement(&g_reclaimBatches) <= 20)
        logf("RECLAIM %zu slot(s); first %s (%08x -> %08x)", reclaimed, firstSlot, firstOld, firstNew);
    if (InterlockedCompareExchange(&g_reassertFault, 0, 0)) logf("re-assert: streaming pool became unreadable — disabled for this session");
}

static void drainOps()   // runs on the game's main thread
{
    // A large folder copy must not turn into one giant game-thread frame. Pull a small bounded
    // shard; PeekMessageW will be called again on the next frame for anything left behind.
    std::vector<LiveOp> ops; ops.reserve(8);
    {
        CsGuard lock(g_cs);
        for (int i = 0; i < 8 && !g_opQ.empty(); ++i) {
            ops.push_back(std::move(g_opQ.front()));
            g_opQ.pop_front();
        }
    }
    for (auto& op : ops) {
        if (op.kind == 0) {
            Cost current;
            HANDLE lease = acquireSafeResource(op.ov.file, op.ov.slot, false, &current);
            if (lease == INVALID_HANDLE_VALUE) {
                logf("live reload: %s changed or disappeared before registration; skipped", op.ov.slot);
                free((void*)op.ov.slot); free((void*)op.ov.file);
                continue;
            }
            int why = liveRegister(op.ov);
            op.ov.lease = lease; // once game code sees the path, keep its validated bytes immutable
            if (why == 0) {
                if (commitLiveAdd(op.ov)) {
                    logf("LIVE-ADD  %s  <-  tex_overrides/%s  (id=%u handle=%08x)", op.ov.slot, rel(op.ov.file), op.ov.id, op.ov.handle);
                    if (current.cv + current.cp >= (8u << 20))
                        logf("  HEAVY %.1f MB in memory — likely 4K or uncompressed; shrink it to fight texture loss", (current.cv + current.cp) / 1048576.0);
                    op.ov.slot = nullptr; op.ov.file = nullptr; op.ov.lease = INVALID_HANDLE_VALUE;
                }
                // The engine may retain either string after a successful call. On the extremely
                // rare bookkeeping-allocation failure, retain them and the safety lease until exit.
                else { op.ov.slot = nullptr; op.ov.file = nullptr; }
            } else if (why == 1) {
                // registerRawStreamingFile refuses a slot that already holds a handle, so a
                // name the connected server or a DLC already streams cannot be claimed this way.
                // Cfx hits the same wall and answers it by writing pgRawStreamer handles straight
                // into the entry (LoadStreamingFile.cpp: handle == 0 -> register, otherwise
                // overwrite + handle stack). We have no RegisterFile pattern to mint a handle
                // with, so a restart, which claims the slot before any of that mounts, is the
                // honest answer. Only brand-new NAMES are affected: editing a file the plugin
                // already owns goes down the re-stat path and still applies live.
                logf("live reload: %s is already loaded from the server or a DLC, and the game will not hand a name over while it runs. Restart FiveM and the plugin claims it at startup, before those mount. Editing files it already owns still applies live.", op.ov.slot);
                // The call reached game code even when it returned no usable ID. Keep both
                // process-lifetime strings and the lease because the engine may have retained them.
                op.ov.slot = nullptr; op.ov.file = nullptr;
            } else {
                logf("live reload: could not register %s (%s), restart to pick it up", op.ov.slot,
                     why == 2 ? "no handle came back" : "fault inside the game's register call");
                op.ov.slot = nullptr; op.ov.file = nullptr;
            }
        } else {
            const char* currentPath = nullptr;
            {
                CsGuard lock(g_cs);
                auto known = g_bySlot.find(op.ov.slot);
                if (known != g_bySlot.end()) currentPath = known->second;
            }
            if (!currentPath) { free((void*)op.ov.slot); continue; }
            HANDLE lease = acquireSafeResource(currentPath, op.ov.slot, false);
            bool refreshed = lease != INVALID_HANDLE_VALUE && g_getRawStreamerFn && g_rawGetEntryFn && rawInvalidate(op.handle);
            if (lease != INVALID_HANDLE_VALUE) CloseHandle(lease);
            if (refreshed)
                logf("LIVE-UPDATE  %s reread from disk; reapply the outfit or tattoo to see it", op.ov.slot);
            else
                logf("live reload: %s changed, could not refresh it, restart to apply", op.ov.slot);
            free((void*)op.ov.slot);
        }
    }
    { CsGuard lock(g_cs); if (g_opQ.empty()) InterlockedExchange(&g_opsPending, 0); }
}

static BOOL WINAPI h_peekMsg(LPMSG m, HWND w, UINT a, UINT b, UINT r)
{
    // Plugins load synchronously on the launcher thread that continues into the GTA entry point.
    // Capture that thread in DllMain instead of letting an arbitrary first PeekMessage caller
    // claim the pump.
    if (GetCurrentThreadId() == g_gameTid) {
        LONGLONG now = (LONGLONG)GetTickCount64();
        InterlockedExchange64(&g_lastPumpAt, now);
        // Message loops may call PeekMessageW many times in one rendered frame. One shared work
        // window every 10 ms prevents "eight per call" from becoming hundreds in one frame.
        LONGLONG last = InterlockedCompareExchange64(&g_lastPumpWorkAt, 0, 0);
        if (now - last >= 10 && InterlockedCompareExchange64(&g_lastPumpWorkAt, now, last) == last) {
            if (atomicRead(&g_opsPending)) drainOps();
            if (atomicRead(&g_reassertPending)) reassertShard(64);
        }
    }
    PeekMsg_t orig = (PeekMsg_t)InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr);
    return orig(m, w, a, b, r);
}

static bool installPump()
{
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)mod;
    auto nt  = (IMAGE_NT_HEADERS*)(mod + dos->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(mod + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((const char*)(mod + imp->Name), "user32.dll") != 0) continue;
        auto thunk = (IMAGE_THUNK_DATA*)(mod + imp->FirstThunk);
        auto orig  = (IMAGE_THUNK_DATA*)(mod + imp->OriginalFirstThunk);
        for (; orig->u1.AddressOfData; ++thunk, ++orig) {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto byName = (IMAGE_IMPORT_BY_NAME*)(mod + orig->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, "PeekMessageW") != 0) continue;
            DWORD old;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            InterlockedExchangePointer((PVOID volatile*)&g_origPeek, (PVOID)thunk->u1.Function);
            g_peekSlot = (PVOID volatile*)&thunk->u1.Function;
            InterlockedExchangePointer((PVOID volatile*)&thunk->u1.Function, (PVOID)h_peekMsg);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// ---- crash saver ----
static bool journalAppend(const std::vector<LiveOp>& batch)
{
    FILE* f; if (fopen_s(&f, g_inflightPath, "a") != 0 || !f) return false;
    bool ok = true;
    for (auto& op : batch) if (fprintf(f, "%s\n", op.ov.slot) < 0) { ok = false; break; }
    if (ferror(f) || fflush(f) != 0) ok = false;
    if (fclose(f) == EOF) ok = false;
    return ok;
}

// Called at startup, before the folder scan. A leftover _inflight.txt means the game died while
// (or moments after) those files were being applied live — quarantine them.
static void crashSaverStartup()
{
    FILE* f; bool deleteInflight = false;
    if (fopen_s(&f, g_inflightPath, "r") == 0 && f) {
        char line[512]; bool any = false, persisted = true;
        FILE* q = nullptr; fopen_s(&q, g_quarantinePath, "a");
        if (!q) persisted = false;
        while (fgets(line, sizeof line, f)) {
            std::string k = line;
            while (!k.empty() && (k.back() == '\n' || k.back() == '\r')) k.pop_back();
            if (k.empty()) continue;
            if (g_quarantine.insert(k).second && (!q || fprintf(q, "%s\n", k.c_str()) < 0)) persisted = false;
            any = true;
        }
        if (ferror(f)) persisted = false;
        fclose(f);
        if (q) {
            if (ferror(q) || fflush(q) != 0) persisted = false;
            if (fclose(q) == EOF) persisted = false;
        }
        if (any) logf("CRASH SAVER: the game did not shut down cleanly right after live changes; the files involved are quarantined");
        deleteInflight = !any || persisted;
        if (any && !persisted) logf("CRASH SAVER: could not persist quarantine; keeping _inflight.txt and refusing those files this session");
    }
    if (deleteInflight) DeleteFileA(g_inflightPath);
    if (fopen_s(&f, g_quarantinePath, "r") == 0 && f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            std::string k = line;
            while (!k.empty() && (k.back() == '\n' || k.back() == '\r')) k.pop_back();
            if (!k.empty()) g_quarantine.insert(k);
        }
        fclose(f);
        for (auto& k : g_quarantine)
            logf("QUARANTINED %s — not loaded; delete _quarantine.txt in tex_overrides to try it again", k.c_str());
    }
}

struct Snap { uint64_t wt; uint64_t size; };
static std::unordered_map<std::string, Snap> g_snap;   // full path -> stamp; watcher thread only

// A registered raw entry can reread its pathname later. If an overwrite fails the RSC gate,
// merely declining the re-stat is not enough: the old handle could consume the new bad bytes.
// Stop all local routing and recoverably rename the exact changed file so that stale engine
// metadata cannot open it. The user can remove the suffix after fixing the resource.
static void rejectActiveOverwrite(const std::string& full, const std::string& key)
{
    std::string rejected = full + ".texoverride-rejected-" + std::to_string(GetTickCount64());
    bool wasActive = false;
    {
        CsGuard lock(g_cs);
        for (auto itq = g_opQ.begin(); itq != g_opQ.end(); ) {
            if (strcmp(itq->ov.slot, key.c_str()) != 0) { ++itq; continue; }
            free((void*)itq->ov.slot); free((void*)itq->ov.file);
            itq = g_opQ.erase(itq);
        }
        if (g_opQ.empty()) InterlockedExchange(&g_opsPending, 0);
        auto it = g_indexBySlot.find(key);
        if (it != g_indexBySlot.end() && it->second < g_ovs.size()) {
            Ov& ov = g_ovs[it->second];
            ov.handle = 0;
            if (ov.lease != INVALID_HANDLE_VALUE) { CloseHandle(ov.lease); ov.lease = INVALID_HANDLE_VALUE; }
            g_indexBySlot.erase(it); g_bySlot.erase(key);
            wasActive = true;
        }
    }
    bool moved = MoveFileExA(full.c_str(), rejected.c_str(), 0) != FALSE;
    logf("live reload: rejected unsafe overwrite %s; local routing stopped%s. Restart FiveM. %s",
         key.c_str(), wasActive ? "" : " (slot was not active)",
         moved ? "The file was renamed with a .texoverride-rejected suffix; remove that suffix only after fixing it."
               : "The file could not be renamed, so close FiveM immediately before restoring a safe copy.");
}

static void mergePlacement(std::vector<PlColl>& fresh)
{
    {
        CsGuard lock(g_cs);
        for (auto& npc : fresh) {
            PlColl* old = nullptr;
            for (auto& pc : g_pl) if (pc.src == npc.src) { old = &pc; break; }
            // carry a solved layout over when the file still describes the same presets, so a tuning
            // edit applies without a fresh fingerprint (which edited values would keep failing)
            if (old && old->solved && old->hash == npc.hash && old->presets.size() == npc.presets.size()) {
                bool same = true;
                for (size_t i = 0; i < npc.presets.size(); ++i)
                    if (old->presets[i].hash != npc.presets[i].hash) { same = false; break; }
                if (same) {
                    npc.arrOff = old->arrOff; npc.nameOff = old->nameOff;
                    npc.stride = old->stride; npc.uvOff = old->uvOff; npc.solved = true;
                }
            }
            logf("placement: %s reloaded from %s (%zu presets%s)", npc.name.c_str(), npc.src.c_str(),
                 npc.presets.size(), npc.solved ? ", layout kept" : "");
            if (old) *old = std::move(npc); else g_pl.push_back(std::move(npc));
        }
    }
    if (g_plFault) logf("placement: NOTE — placement is disabled for this session (earlier fault), edits will apply after a restart");
}

// Detects what changed and queues work; the game-touching half runs later in drainOps on the
// main thread. quiet = the baseline pass right after the watcher starts: it seeds the stamp map
// (and still queues files that appeared during loading) without re-logging SKIP/IGNORED lines
// the startup scan already wrote.
static bool rescanTree(const std::string& base, const std::string& sub, bool quiet,
                       std::vector<std::string>& xmls, std::vector<LiveOp>& batch,
                       std::unordered_set<std::string>& seen)
{
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((base + sub + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND; // an empty directory is complete
    bool complete = true;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string childRel = sub.empty() ? name : sub + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (!quiet) logf("SKIP directory junction/symlink: %s", fwd(childRel).c_str());
                continue;
            }
            if (!rescanTree(base, childRel, quiet, xmls, batch, seen)) complete = false;
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            if (!quiet) logf("SKIP file junction/symlink: %s", fwd(childRel).c_str());
            continue;
        }

        std::string full = base + childRel;
        seen.insert(full);
        Snap now{ ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime,
                  ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow };
        auto it = g_snap.find(full);
        bool isNew = (it == g_snap.end()), isChanged = !isNew && (it->second.wt != now.wt || it->second.size != now.size);
        g_snap[full] = now;
        if (!isNew && !isChanged) continue;

        std::string ln = lower(name);
        if (ln.size() > 5 && ln.compare(ln.size()-5, 5, ".meta") == 0) {
            if (isNew && !quiet) logf("IGNORED %s — .meta files hold shop data (prices/menus), not looks; see README", fwd(childRel).c_str());
            continue;
        }
        if (sub.empty() && ln.size() > 4 && ln.compare(ln.size()-4, 4, ".xml") == 0) { if (!quiet) xmls.push_back(name); continue; }
        if (ln.size() <= 4 || (ln.compare(ln.size()-4,4,".ytd") != 0 && ln.compare(ln.size()-4,4,".ydd") != 0)) continue;

        std::string key = lower(fwd(childRel));
        if (g_quarantine.count(key)) continue;   // crash saver: refused until _quarantine.txt is deleted
        if (!isAllowedKey(key)) {
            if (isNew && !quiet) logf("SKIP (folders must be freemode-ped collections, root files must be .ytd): %s", key.c_str());
            continue;
        }

        uint32_t handle = 0; bool known = false;
        {
            CsGuard lock(g_cs);
            auto oi = g_indexBySlot.find(key);
            if (oi != g_indexBySlot.end() && oi->second < g_ovs.size()) {
                known = true; handle = g_ovs[oi->second].handle;
            }
        }

        if (!known) {
            bool hasPump = InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr) != nullptr;
            if (!hasPump) { logf("live reload: new file %s needs a game restart", key.c_str()); continue; }
            Cost cost = readCost(fwd(full).c_str());
            if (tooBigJudge(key.c_str(), cost, quiet)) continue;   // crash gate; quiet skips the baseline re-log
            char* slotCopy = _strdup(key.c_str()); char* fileCopy = _strdup(fwd(full).c_str());
            if (!slotCopy || !fileCopy) {
                free(slotCopy); free(fileCopy); logf("live reload: out of memory while queuing %s", key.c_str()); continue;
            }
            batch.push_back({ 0, { slotCopy, fileCopy }, 0, cost });
        }
        else if (known && isChanged) {
            Cost changed = readCost(fwd(full).c_str());
            if (tooBigJudge(key.c_str(), changed, false))
                rejectActiveOverwrite(fwd(full), key);
            else if (!handle)
                logf("live reload: %s changed but has no active raw handle; restart to apply", key.c_str());
            else if ((handle >> 16) != 0)   // not in the game's own raw streamer; cannot re-stat it
                logf("live reload: %s changed, restart to apply (handle %08x not raw)", key.c_str(), handle);
            else if (!InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr))
                logf("live reload: %s changed, restart to apply", key.c_str());
            else
            {
                char* slotCopy = _strdup(key.c_str());
                if (!slotCopy) { logf("live reload: out of memory while queuing %s", key.c_str()); continue; }
                batch.push_back({ 1, { slotCopy, nullptr }, handle, Cost() });
            }
        }
    } while (FindNextFileA(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES) complete = false;
    FindClose(h);
    return complete;
}

static void pruneDeleted(const std::unordered_set<std::string>& seen, bool quiet)
{
    std::vector<std::string> gone;
    for (auto& kv : g_snap) if (!seen.count(kv.first)) gone.push_back(kv.first);
    for (auto& full : gone) {
        g_snap.erase(full);
        if (full.size() <= strlen(g_overrideDir)) continue;
        std::string key = lower(fwd(full.substr(strlen(g_overrideDir))));
        if (key.size() > 4 && (key.compare(key.size()-4, 4, ".ytd") == 0 || key.compare(key.size()-4, 4, ".ydd") == 0)) {
            bool wasActive = false;
            {
                CsGuard lock(g_cs);
                for (auto itq = g_opQ.begin(); itq != g_opQ.end(); ) {
                    if (strcmp(itq->ov.slot, key.c_str()) != 0) { ++itq; continue; }
                    free((void*)itq->ov.slot); free((void*)itq->ov.file);
                    itq = g_opQ.erase(itq);
                }
                if (g_opQ.empty()) InterlockedExchange(&g_opsPending, 0);
                auto it = g_indexBySlot.find(key);
                if (it != g_indexBySlot.end() && it->second < g_ovs.size()) {
                    Ov& ov = g_ovs[it->second];
                    ov.handle = 0; wasActive = true;
                    if (ov.lease != INVALID_HANDLE_VALUE) { CloseHandle(ov.lease); ov.lease = INVALID_HANDLE_VALUE; }
                    g_indexBySlot.erase(it); g_bySlot.erase(key);
                }
            }
            if (wasActive && !quiet)
                logf("LIVE-REMOVE %s — stopped redirecting/re-asserting it; restart FiveM to restore the server or vanilla asset", key.c_str());
        }
        else if (key.find('/') == std::string::npos && key.size() > 4 && key.compare(key.size()-4, 4, ".xml") == 0) {
            { CsGuard lock(g_cs); g_pl.erase(std::remove_if(g_pl.begin(), g_pl.end(), [&](const PlColl& pc) { return lower(pc.src) == key; }), g_pl.end()); }
            if (!quiet) logf("placement: removed %s; prior in-memory writes remain until FiveM restarts", key.c_str());
        }
    }
}

// Hand a batch to the game thread: journal first (crash saver), one batch in flight at a time.
static void submitBatch(std::vector<LiveOp>& batch)
{
    // Keep every accepted change queued and let the game-thread pump consume a bounded shard per
    // frame. The journal is append-only during the risk window, so a newer batch cannot erase an
    // older batch that may still be responsible for a crash.
    if (!journalAppend(batch)) {
        logf("live reload: could not write the crash-saver journal, so this batch was not applied");
        for (auto& op : batch) { free((void*)op.ov.slot); free((void*)op.ov.file); }
        return;
    }
    size_t dropped = 0;
    try {
        CsGuard lock(g_cs);
        for (auto& op : batch) {
            auto same = std::find_if(g_opQ.begin(), g_opQ.end(), [&](const LiveOp& queued) {
                return strcmp(queued.ov.slot, op.ov.slot) == 0;
            });
            if (same != g_opQ.end()) {
                free((void*)same->ov.slot); free((void*)same->ov.file);
                *same = op;   // pointer ownership transfers only after this non-allocating copy
                op.ov.slot = nullptr; op.ov.file = nullptr;
            }
            else if (g_opQ.size() < 2048) {
                g_opQ.push_back(op);
                op.ov.slot = nullptr; op.ov.file = nullptr;
            }
            else ++dropped;
        }
    }
    catch (...) { logf("live reload: queue allocation failed; remaining changes need a FiveM restart"); }
    for (auto& op : batch) { free((void*)op.ov.slot); free((void*)op.ov.file); op.ov.slot = nullptr; op.ov.file = nullptr; }
    if (dropped) logf("live reload: queue limit reached; %zu change(s) need a FiveM restart", dropped);
    InterlockedExchange(&g_opsPending, 1);
    g_journalClearAt = GetTickCount64() + 30000;   // journal outlives the apply; see CRASH SAVER above
}

static DWORD WINAPI WatchLoop(LPVOID)
{
    std::string dir = g_overrideDir; if (!dir.empty() && dir.back() == '\\') dir.pop_back();
    HANDLE h = FindFirstChangeNotificationA(dir.c_str(), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    if (h == INVALID_HANDLE_VALUE) { logf("live reload: cannot watch tex_overrides (err %lu) — restart to apply changes", GetLastError()); return 0; }

    bool pump = InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr) != nullptr;
    if (!pump) pump = installPump();
    { std::vector<std::string> ignore; std::vector<LiveOp> batch; std::unordered_set<std::string> seen;
      bool complete = rescanTree(g_overrideDir, "", true, ignore, batch, seen);   // baseline stamps; also catches files added during loading
      if (complete) pruneDeleted(seen, true); else logf("live reload: baseline enumeration was interrupted; deletion checks postponed");
      if (!batch.empty()) submitBatch(batch); }
    logf("live reload: watching tex_overrides (%s)", pump ? "full: edits and new files" : "edits only: no main-thread pump, new files need a restart");

    for (;;) {
        DWORD w = WaitForSingleObject(h, g_journalClearAt ? 1000 : INFINITE);
        if (g_journalClearAt && GetTickCount64() >= g_journalClearAt && !atomicRead(&g_opsPending)) {
            DeleteFileA(g_inflightPath);   // survived the risky window; nothing to quarantine
            g_journalClearAt = 0;
        }
        if (w == WAIT_TIMEOUT) continue;
        if (w != WAIT_OBJECT_0) break;
        do { FindNextChangeNotification(h); } while (WaitForSingleObject(h, 500) == WAIT_OBJECT_0);   // debounce until quiet
        std::vector<std::string> xmls; std::vector<LiveOp> batch; std::unordered_set<std::string> seen;
        bool complete = rescanTree(g_overrideDir, "", false, xmls, batch, seen);
        if (complete) pruneDeleted(seen, false); else logf("live reload: folder enumeration was interrupted; deletion checks postponed");
        if (!batch.empty()) submitBatch(batch);
        if (!xmls.empty()) {
            std::vector<PlColl> fresh;
            for (auto& f : xmls) parsePlacementXml(std::string(g_overrideDir) + f, f.c_str(), fresh);
            if (!fresh.empty()) mergePlacement(fresh);
        }
    }
    FindCloseChangeNotification(h);
    return 0;
}
// ========================================================================================

// Register one startup override while holding a read-only lease on the exact resource that
// passed the safety gate. The SEH frame contains only POD locals and game-memory writes.
// 0 = registered, 1 = rejected at point of use, 2 = fault in the game's registration path.
static int registerBaseOne(Ov& ov)
{
    HANDLE lease = acquireSafeResource(ov.file, ov.slot, false);
    if (lease == INVALID_HANDLE_VALUE) return 1;
    bool fault = false;
    __try {
        uint32_t id = 0xFFFFFFFF;
        o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
        ov.id = id;
        if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
            ov.handle = g_mgr->entries[id].handle;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; }
    if (fault) CloseHandle(lease);
    else ov.lease = lease; // prevents overwrite/delete races for as long as FiveM can stream it
    return fault ? 2 : 0;
}

static uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    InterlockedIncrement(&g_regTotal);
    try {
      if (asName)
      {
        // Outside the lock, deliberately: scanFinish takes g_cs to publish its results, so a
        // wait with the lock held would deadlock. This is the only place the game's own thread
        // ever waits on us, and it only ever waits once.
        // Bounded, and skipped entirely when the plugin is off. scanFinish publishes all-or-
        // nothing, so a timeout must fail closed instead of permanently registering an empty
        // pack and claiming that partial results exist.
        if (!g_off) {
            LONG state = InterlockedCompareExchange(&g_scanState, 0, 0);
            if (state == SCAN_SCANNING && g_scanDone) WaitForSingleObject(g_scanDone, 15000);
            state = InterlockedCompareExchange(&g_scanState, 0, 0);
            if (state != SCAN_READY) {
                if (InterlockedCompareExchange(&g_scanState, SCAN_FAILED, SCAN_SCANNING) == SCAN_SCANNING) {
                    logf("file scan did not become ready within 15 seconds — overrides disabled for this session so FiveM can continue");
                    if (g_scanDone && !SetEvent(g_scanDone)) logf("scan timeout event signal failed (err %lu)", GetLastError());
                }
                return o_regRaw(fileId, name, b1, asName, b2);
            }
        }
        {
            CsGuard lock(g_cs);
            if (!g_captured) { g_b1 = b1; g_b2 = b2; g_captured = true; logf("captured flags: b1=%d b2=%d", (int)b1, (int)b2); }
        }

        // Exactly one hook caller owns registration. Concurrent streaming callbacks wait for its
        // terminal state before they redirect or register overlapping slots.
        if (!g_off) {
            LONG registration = InterlockedCompareExchange(&g_registrationState, REG_IN_PROGRESS, REG_NOT_STARTED);
            if (registration == REG_NOT_STARTED) {
                if (g_mgr && (!g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)) {
                    logf("streaming pool looks wrong (entries=%p num=%d) — re-assert disabled", (void*)g_mgr->entries, g_mgr->numEntries);
                    g_mgr = nullptr;
                }
                int done = 0, rejected = 0; bool registrationFault = false;
                for (auto& ov : g_ovs) {
                    int result = registerBaseOne(ov);
                    if (result == 1) {
                        // It has no lifetime lease and must not remain redirectable. If a valid
                        // file later appears at this path, the watcher treats it as a new add.
                        { CsGuard lock(g_cs); g_bySlot.erase(ov.slot); g_indexBySlot.erase(ov.slot); }
                        ++rejected; continue;
                    }
                    if (result == 2) { registrationFault = true; break; }
                    if (++done <= 60) logf("OVERRIDE-REG  %s  <-  tex_overrides/%s  (id=%u handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
                }
                if (registrationFault) { g_off = true; logf("FAULT during base registration — plugin disabled for this session"); }
                logf("registered %d base-slot override(s); %d changed file(s) rejected at point of use", done, rejected);
                if (g_mgr) logf("streaming pool: entries=%p numEntries=%d", (void*)g_mgr->entries, g_mgr->numEntries);
                if (!registrationFault) InterlockedExchange(&g_idsReady, 1);
                InterlockedExchange(&g_registrationState, registrationFault ? REG_FAILED : REG_DONE);
                if (g_registerDone && !SetEvent(g_registerDone)) logf("registration event signal failed (err %lu)", GetLastError());
            }
            else {
                if (registration == REG_IN_PROGRESS && g_registerDone) WaitForSingleObject(g_registerDone, INFINITE);
                registration = InterlockedCompareExchange(&g_registrationState, 0, 0);
                if (registration != REG_DONE) return o_regRaw(fileId, name, b1, asName, b2);
            }
        }

        // MAP: record each distinct collection the server streams, tagged with whether we'd ever touch it
        std::string keyLower = lower(asName);
        std::string coll = collectionOf(keyLower);
        {
            CsGuard lock(g_cs);
            if (g_collSeen.insert(coll).second && g_collSeen.size() <= 500)
                logf("collection: %-40s [%s]", coll.c_str(),
                     isAllowedKey(keyLower) ? "overridable" : "OTHER - never touched");
        }
      }

    // redirect only exact-slot matches, and only for keys the gate allows (double guard):
    // freemode-ped collection slots, or bare-name .ytd dictionaries.
    // g_bySlot can gain entries at runtime now (live reload), so the lookup takes the lock.
      if (!g_off && asName)
      {
        std::string key = lower(asName);
        if (isAllowedKey(key))
        {
            const char* redirect = nullptr;
            { CsGuard lock(g_cs); auto it = g_bySlot.find(key); if (it != g_bySlot.end()) redirect = it->second; }
            if (redirect)
            {
                HANDLE lease = acquireSafeResource(redirect, key.c_str(), false);
                if (lease != INVALID_HANDLE_VALUE) {
                    LONG redirects = InterlockedIncrement(&g_redirects);
                    if (redirects <= 100) logf("REDIRECT  %s  ->  tex_overrides/%s", asName, rel(redirect));
                    uint32_t* result = o_regRaw(fileId, redirect, b1, asName, b2);
                    CloseHandle(lease);
                    return result;
                }
            }
        }
      }
      return o_regRaw(fileId, name, b1, asName, b2);
    }
    catch (const std::exception& e) { logf("hook bookkeeping failed: %s; using the original stream path", e.what()); }
    catch (...) { logf("hook bookkeeping failed; using the original stream path"); }
    return o_regRaw(fileId, name, b1, asName, b2);
}

static DWORD WINAPI BeatLoop(LPVOID);
static void uninstallPump();

// Runs synchronously inside asi-five's LoadLibrary call. FiveM loads plugins in
// LauncherInterface::PostLoadGame, which returns the game's entry point to the launcher — the
// entry point has NOT run yet, so no thread anywhere is executing game code. That makes the hook
// patch race-free by construction, with no thread freezing: the same guarantee FiveM relies on
// for its own HookFunction patches in that window. (MinHook's freeze never worked under FiveM
// anyway — CreateToolhelp32Snapshot is blocked — so before this it was safe only by luck.)
// ---- texture budget raiser (opt-in: _budget.txt holds a number of GB) -----------------------
// The game's texture budget is a plain data table in GTA5.exe: 20 rows of 4 uint64 budgets, one
// column per texture-quality level. FiveM fills it at boot (PatchExtendedBudgeting.cpp) with
// 3 * GB x the Extended Texture Budget slider's multiplier, and rewrites it whenever the slider
// moves. GB there is 1000 * 1024 * 1024, so the default is 2.93 GiB and a maxed slider is 7.81
// GiB. The card is not in that sum at any setting, which is why texture loss hits a 24 GB build
// exactly as hard as an 8 GB one. "Texture loss" (stuck low detail, black walls, restart needed) is this budget running
// dry — the eviction algorithm inside GTA5.exe only frees memory under pressure (cfx issue
// #3874), so a bigger ceiling means more headroom before the cliff. Data writes only, same
// class as the handle re-assert; re-asserted each beat because the settings screen rewrites it.
// Clamped to the card's real dedicated VRAM: past that, D3D11 demotes textures to system RAM
// and the game stutters hard, which is why this is opt-in and never a silent default.
static uint64_t* g_vramTable = nullptr;
static uint64_t  g_budget = 0;              // decided bytes; 0 = leave the game's budget alone
static double    g_budgetWant = -1.0;       // from _budget.txt: -1 auto, 0 off, else GB
static uint64_t  g_budgetCurr = 0;          // what the game set, read at startup
static volatile LONG g_budgetFault = 0;
static long g_budgetWrites = 0;

static bool vramTableSane(uint64_t* t, uint64_t* cur)   // refuse to write unless it looks like Cfx filled it
{
    __try {
        for (int i = 0; i < 80; i += 4) {
            uint64_t full = t[i + 3];       // rows are half / 1.5th / full / full of the budget
            if (full != t[i + 2] || full < (1ull << 30) || full > (48ull << 30) || t[i] >= full)
                return false;
        }
        *cur = t[3];                        // what FiveM set: 3e9 x (vid_budgetScale/12 + 1)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// One DXGI probe, two numbers, both 0 when unknowable:
//   g_vramTotal  — the card's physical VRAM (biggest hardware adapter)
//   g_vramBudget — what Windows is willing to give THIS process right now. WDDM works it out
//                  itself, so it already subtracts the desktop, the browser, and anything else
//                  on the GPU. Microsoft is blunt about the other side of it: a process that
//                  runs past its budget "will likely experience stuttering, as they are
//                  intermittently frozen and paged-out". That is the exact failure the old
//                  opt-in rule was guarding against, so this is the number to size against
//                  rather than a fraction guessed from the card's sticker capacity.
static uint64_t g_vramTotal = 0, g_vramBudget = 0;
static void probeVram()
{
    // dxgi loaded by FULL System32 path: a static import (or a bare-name load) binds to any
    // already-loaded ReShade/ENB proxy dxgi.dll, and a proxy missing CreateDXGIFactory1 used
    // to fail the load of this whole plugin. The full path always gets Windows' own copy.
    typedef HRESULT (WINAPI* CreateFactory_t)(REFIID, void**);
    char dxPath[MAX_PATH + 16];
    UINT sl = GetSystemDirectoryA(dxPath, MAX_PATH);
    if (sl == 0 || sl >= MAX_PATH) { logf("budget: cannot locate System32"); return; }
    strcat_s(dxPath, "\\dxgi.dll");
    HMODULE dx = LoadLibraryA(dxPath);
    if (!dx) { logf("budget: cannot load %s (err %lu)", dxPath, GetLastError()); return; }
    auto createFactory = (CreateFactory_t)GetProcAddress(dx, "CreateDXGIFactory1");
    if (!createFactory) { logf("budget: dxgi.dll has no CreateDXGIFactory1"); FreeLibrary(dx); return; }

    IDXGIFactory1* f = nullptr;
    HRESULT hr = createFactory(__uuidof(IDXGIFactory1), (void**)&f);
    if (FAILED(hr) || !f) { logf("budget: CreateDXGIFactory1 failed (hr 0x%08lX)", (unsigned long)hr); FreeLibrary(dx); return; }
    IDXGIAdapter1* a = nullptr; IDXGIAdapter1* best = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > g_vramTotal) {
            g_vramTotal = d.DedicatedVideoMemory;
            if (best) best->Release();
            best = a; continue;
        }
        a->Release();
    }
    if (!best) logf("budget: DXGI listed no hardware adapter");
    if (best) {
        IDXGIAdapter3* a3 = nullptr;   // DXGI 1.4, so Windows 10 and up; older just keeps 0 here
        if (SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO vm = {};
            if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm)))
                g_vramBudget = vm.Budget;
            a3->Release();
        }
        best->Release();
    }
    f->Release();
    FreeLibrary(dx);
}

// Size the texture budget to this PC. FiveM hands every machine the same ceiling —
// SetGamePhysicalBudget(3 * 1000000000) times (vid_budgetScale / 12 + 1), and that slider is
// console-locked in production — so a 24 GB card saturates at the same ~2.8 GB an 8 GB card does.
// That is why the "textures gone, restart needed" bug shows up on high-end machines too.
// Hold back an eighth of what Windows offers for render targets, shadow maps and the UI, or 2 GB,
// whichever is more. An eighth and not a quarter because the number Windows hands back is ALREADY
// this process's share, with the desktop and everything else on the GPU subtracted, so a second
// large percentage on top just wastes the card: on a real 11.7 GB machine a quarter produced
// 8.0 GB against the 7.8 GB the game had set, a raise worth nothing. Returns 0 when there is
// nothing to gain over what FiveM set.
// ponytail: probed once at startup, not re-queried per beat. The offered budget does move when
// the player alt-tabs into something GPU-hungry; if that turns into stutter reports, call
// probeVram() from budgetBeat and re-target on a change bigger than the 256 MB step.
static constexpr uint64_t budgetFor(uint64_t cap, uint64_t current)
{
    if (!cap) return 0;
    uint64_t reserve = (cap / 8 > (2ull << 30)) ? cap / 8 : (2ull << 30);
    if (cap <= reserve) return 0;
    uint64_t want = (cap - reserve) & ~((256ull << 20) - 1);             // 256 MB steps
    return (want > current) ? want : 0;
}
// FiveM's ceiling, for reference. GB there is 1000 * 1024 * 1024 (not 1e9, not 1 << 30), the
// budget is 3 * GB * ((vid_budgetScale / 12) + 1), and the slider defaults to 0:
//   slider  0 (default) -> 3145728000 = 2.93 GiB
//   slider 20 (maxed)   -> 8388608000 = 7.81 GiB   <- confirmed against a real log
// Neither number involves the graphics card, which is the whole reason this exists.
static_assert(budgetFor(24ull << 30, 3145728000ull) == 21ull << 30, "24 GB card holds an eighth back");
static_assert(budgetFor( 8ull << 30, 3145728000ull) ==  6ull << 30, "8 GB card holds the 2 GB floor back");
static_assert(budgetFor( 6ull << 30, 3145728000ull) ==  4ull << 30, "6 GB card holds the 2 GB floor back");
static_assert(budgetFor( 4ull << 30, 3145728000ull) == 0, "no gain over the default, leave it alone");
static_assert(budgetFor(          0, 3145728000ull) == 0, "card unreadable, leave it alone");
// with the slider already maxed at 7.81 GiB there is a much higher bar to clear:
static_assert(budgetFor( 8ull << 30, 8388608000ull) == 0, "8 GB card, slider maxed, leave it alone");
static_assert(budgetFor(11ull << 30, 8388608000ull) ==  9ull << 30, "the real 11.7 GB machine, 7.8 -> 9.0");
static_assert(budgetFor(24ull << 30, 8388608000ull) == 21ull << 30, "24 GB card, slider maxed, plenty to gain");

static uint64_t autoBudget(uint64_t current)
{
    uint64_t cap = g_vramBudget;
    if (!cap || (g_vramTotal && cap > g_vramTotal)) cap = g_vramTotal;   // Budget can span shared RAM
    return budgetFor(cap, current);
}

static void budgetBeatImpl()
{
    uint64_t old = g_vramTable[3];
    bool raised = false;
    const uint64_t desired[4] = { g_budget / 2, (uint64_t)(g_budget / 1.5), g_budget, g_budget };
    for (int i = 0; i < 80; i += 4) {         // same rows, same ratios as Cfx's own writer
        for (int j = 0; j < 4; ++j) {
            if (g_vramTable[i + j] >= desired[j]) continue;
            g_vramTable[i + j] = desired[j]; raised = true;
        }
    }
    if (raised && ++g_budgetWrites <= 10)
        logf("texture budget: %.1f -> %.1f GB%s", old / 1073741824.0, g_budget / 1073741824.0,
             g_budgetWrites == 1 ? "" : " (re-asserted; the settings screen rewrote it)");
}
// Runs once, on the beat thread, because DXGI does not work under DllMain's loader lock. A
// 0.7.0 crash report proved the stronger version of that: calling it from Setup() did not merely
// come back empty on one player's machine, it took an access violation and disabled the whole
// plugin. Out here a fault would kill the game instead of the plugin, so decideBudget is wrapped
// the same way the placement and budget writes are.
static void decideBudgetImpl()
{
    if (!g_vramTable) return;              // nothing to write into; Setup already said so
    if (g_budgetWant == 0.0) return;       // _budget.txt said leave it alone
    probeVram();
    if (g_budgetWant > 0.0) {
        g_budget = (uint64_t)(g_budgetWant * 1073741824.0);
        if (g_vramTotal && g_budget > g_vramTotal) {
            logf("budget: your card has %.1f GB of VRAM; clamping the requested %.1f GB to that",
                 g_vramTotal / 1073741824.0, g_budget / 1073741824.0);
            g_budget = g_vramTotal;
        }
        if (g_budget <= g_budgetCurr) {    // lowering it would only make texture loss worse
            logf("budget: _budget.txt asks for %.1f GB, which is no more than the %.1f GB the game already gives, so it is left alone (put 0 in that file to turn this off)",
                 g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
            g_budget = 0;
        }
        else logf("budget: _budget.txt asked for %.1f GB, raising the texture budget from the %.1f GB the game set",
                  g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
        return;
    }
    g_budget = autoBudget(g_budgetCurr);
    if (g_budget)
        logf("budget: sized to this PC — %.1f GB, up from the %.1f GB the game set (card %.1f GB, Windows is offering this process %.1f GB right now). Put a number of GB in _budget.txt to pick your own, or 0 to leave it alone.",
             g_budget / 1073741824.0, g_budgetCurr / 1073741824.0,
             g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
    else if (!g_vramTotal && !g_vramBudget)
        logf("budget: could not read this card's memory (see the line above), so the texture budget is left as the game set it (%.1f GB). Put a number of GB in _budget.txt to raise it by hand.", g_budgetCurr / 1073741824.0);
    else
        logf("budget: the %.1f GB the game already gives is as much as this card can spare, leaving it alone (card %.1f GB, Windows is offering %.1f GB)",
             g_budgetCurr / 1073741824.0, g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
}
static void decideBudget()
{
    __try { decideBudgetImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_budget = 0;
        logf("budget: FAULT reading this card (code %08X) — texture budget left as the game set it, everything else still works",
             (unsigned)GetExceptionCode());
    }
}

static void budgetBeat()
{
    if (!g_budget || g_budgetFault || !g_vramTable) return;
    __try { budgetBeatImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_budgetFault, 1);
        logf("budget: FAULT writing the table — raised budget disabled for this session");
    }
}

// What the pack costs the streamer once every file in it is resident, and how that compares
// with what the game is handing textures right now. Called from scanFinish.
static void costReport()
{
    if (g_costVirt + g_costPhys) {
        logf("pack cost when fully loaded: %.1f MB of texture memory + %.1f MB other. Vanilla clothing files stay well under 2 MB each; a heavy pack feeds the \"stuck on low detail / textures gone\" bug on busy servers.",
             g_costPhys / 1048576.0, g_costVirt / 1048576.0);
        std::sort(g_costBig.rbegin(), g_costBig.rend());
        for (size_t i = 0; i < g_costBig.size() && i < 20; ++i)
            logf("  HEAVY %6.1f MB  %s — likely 4K or uncompressed; shrink it to fight texture loss", g_costBig[i].first / 1048576.0, g_costBig[i].second.c_str());
        if (g_costBig.size() > 20) logf("  ...and %zu more file(s) over 8 MB", g_costBig.size() - 20);
        // Past ~1 GB the pack no longer fits the budget, and eviction inside GTA5.exe is
        // passive-only (cfx #3874), so the pool saturates and the whole world drops to low LOD.
        // That is the "textures not loading" report from players who never crash. The ceiling is
        // a fixed table FiveM fills at boot (PatchExtendedBudgeting.cpp: 3 GB x the console-locked
        // vid_budgetScale) with no VRAM term in it, which is why a 24 GB card saturates at exactly
        // the same point an 8 GB one does. Say that here: without it, high-end players read the
        // HEAVY list as a low-end problem and assume their hardware already covers it.
        // compare against what the game is giving RIGHT NOW; the budget line on the first beat
        // reports separately whether that ceiling then got raised
        if (g_costPhys >= (1024ull << 20) && g_budgetCurr) {
            logf("  the game is currently giving textures %.1f GB in total, and that has to cover the whole world, not just your pack. %s",
                 g_budgetCurr / 1073741824.0,
                 g_costPhys < g_budgetCurr / 2
                   ? "Your pack fits with room to spare."
                   : "Your pack takes a large share of that, which is what makes textures drop out. Shrink the HEAVY files above (CodeWalker, Tools, Shrink Textures).");
        }
    }
}

// User-controlled files are intentionally read after DllMain returns. The hook itself still
// installs synchronously before the game entry point, but a large folder, journal or XML can no
// longer hold Windows' loader lock while FiveM is starting.
static void loadPostStartupConfig()
{
    char bp[MAX_PATH]; _snprintf_s(bp, _TRUNCATE, "%s_budget.txt", g_overrideDir);
    FILE* bf = nullptr;
    if (!fopen_s(&bf, bp, "rb") && bf) {
        char buf[32] = {}; fread(buf, 1, 31, bf); fclose(bf);
        double gb = atof(buf);
        if (gb >= 1.0 && gb <= 48.0) g_budgetWant = gb;
        else { g_budgetWant = 0.0; logf("budget: _budget.txt does not hold a number of GB between 1 and 48, so the texture budget is left exactly as the game set it"); }
    }

    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((std::string(g_overrideDir) + "*.xml").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { parsePlacementXml(std::string(g_overrideDir) + fd.cFileName, fd.cFileName, g_pl); } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (auto& pc : g_pl) logf("placement: collection %s — %zu preset(s) from %s", pc.name.c_str(), pc.presets.size(), pc.src.c_str());
}

// Optional runtime plumbing is discovered on the beat thread, outside Windows' loader lock.
// This runs before the scan can publish READY, so the streaming manager is still available for
// the very first base registration. Missing optional patterns degrade individual features only.
static void locateRuntimePatterns()
{
    { const short PAT_VRAM[] = { 0x4C,0x63,0xC0,0x48,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x14 };
      uint8_t* q = scanModule(PAT_VRAM, 13);
      uint64_t* t = q ? (uint64_t*)ripTarget(q + 6) : nullptr;
      if (t && vramTableSane(t, &g_budgetCurr)) g_vramTable = t;
      else logf("budget: vram table %s — texture budget left alone, everything else still works",
                q ? "failed the sanity check" : "pattern NOT FOUND"); }

    const short PAT_MGR[] = { 0x74,0x1A,0x8B,0x15,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0x41 };
    if (uint8_t* q = scanModule(PAT_MGR, 16)) {
        g_mgr = (StrMgr*)ripTarget(q + 11);
        logf("streaming manager @ %p", (void*)g_mgr);
    }
    else logf("manager pattern NOT FOUND — claims still register, but nothing can re-assert them");

    { const short PAT_GRS[] = { 0x48,0x8B,0xD3,0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x41,0xFF,0x90,-1,0x01,0x00,0x00,0x8B,0xD8,0xE8 };
      if (uint8_t* q = scanModule(PAT_GRS, 19)) { if (q[-5] == 0xE8) g_getRawStreamerFn = (GetRawStreamer_t)ripTarget(q - 4); }
      const short PAT_GE[] = { 0x0F,0xB7,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x8B,0xD0,0x25,0xFF };
      if (uint8_t* q = scanModule(PAT_GE, 12)) g_rawGetEntryFn = (RawGetEntry_t)(q - 0x14);
      logf("live reload: rawStreamer=%s getEntry=%s",
           g_getRawStreamerFn ? "ok" : "MISSING", g_rawGetEntryFn ? "ok" : "MISSING"); }
}

static void locateRuntimePatternsSafe()
{
    __try { locateRuntimePatterns(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_mgr = nullptr; g_vramTable = nullptr; g_getRawStreamerFn = nullptr; g_rawGetEntryFn = nullptr;
        logf("FAULT while locating optional runtime features — reassertion, live refresh and budget changes disabled");
    }
}

static void Setup()
{
    char self[MAX_PATH]; GetModuleFileNameA(g_self, self, MAX_PATH);
    std::string dir = self; size_t slash = dir.find_last_of('\\');
    std::string plug = (slash==std::string::npos) ? dir : dir.substr(0, slash);
    _snprintf_s(g_logPath, MAX_PATH, _TRUNCATE, "%s\\texoverride.log", plug.c_str());
    _snprintf_s(g_overrideDir, MAX_PATH, _TRUNCATE, "%s\\tex_overrides\\", plug.c_str());
    { std::string off = std::string(g_overrideDir) + "_OFF";
      g_off = (GetFileAttributesA(off.c_str()) != INVALID_FILE_ATTRIBUTES); }
    _snprintf_s(g_inflightPath,   MAX_PATH, _TRUNCATE, "%s_inflight.txt",   g_overrideDir);
    _snprintf_s(g_quarantinePath, MAX_PATH, _TRUNCATE, "%s_quarantine.txt", g_overrideDir);

    InitializeCriticalSection(&g_cs);   // must exist before the hook can fire

    // fresh log every launch, but keep one previous generation: after a crash the next launch
    // used to destroy the exact log that showed what the crashed session was doing
    { char oldLog[MAX_PATH + 8];
      _snprintf_s(oldLog, _TRUNCATE, "%s.old", g_logPath);
      if (!MoveFileExA(g_logPath, oldLog, MOVEFILE_REPLACE_EXISTING))
          DeleteFileA(g_logPath);   // rotation blocked (file held open): keep "fresh log" true
    }
    { time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
      char d[32]; strftime(d, sizeof d, "%Y-%m-%d", &tm);
      logf("================ texoverride " TEXOVERRIDE_VERSION " loaded (%s) ================", d); }
    // Install the existing main-thread pump before the game entry point runs. This removes the
    // runtime IAT-patch race; h_peekMsg accepts only the entry-point thread captured in DllMain.
    // Live application remains disabled if this optional shim fails.
    if (!g_off) logf("main-thread pump pre-entry install: %s", installPump() ? "ok" : "MISSING");
    g_scanDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_registerDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!g_scanDone || !g_registerDone) {
        g_off = true;
        logf("could not create startup synchronization events (err %lu) — plugin disabled", GetLastError());
    }
    const short PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT, sizeof PAT / sizeof *PAT);
    if (!p) { logf("pattern NOT FOUND — plugin disabled for this game build"); uninstallPump(); g_off = true; }
    else {
        void* target = (void*)(p - 0x25);
        logf("registerRawStreamingFile @ %p", target);
        MH_STATUS s = MH_Initialize(); logf("MH_Initialize: %s", MH_StatusToString(s));
        bool ownMh = s == MH_OK;
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { uninstallPump(); g_off = true; return; }
        s = MH_CreateHook(target, (void*)&h_regRaw, (void**)&o_regRaw); logf("MH_CreateHook: %s", MH_StatusToString(s));
        if (s != MH_OK) {
            if (ownMh) MH_Uninitialize();
            uninstallPump();
            g_off = true; return;
        }
        s = MH_EnableHook(target); logf("MH_EnableHook: %s", MH_StatusToString(s));
        if (s != MH_OK) {
            MH_STATUS rm = MH_RemoveHook(target); logf("MH_RemoveHook after enable failure: %s", MH_StatusToString(rm));
            if (ownMh) MH_Uninitialize();
            uninstallPump();
            g_off = true; return;
        }
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
        while (WinHttpReadData(r, buf, sizeof buf, &got) && got) {
            if (body.size() + got > (1u << 20)) { body.clear(); break; }
            body.append(buf, got);
        }
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
            "Open the download page now?\n\n"
            "To turn this check off, create a file named _NO_UPDATE_CHECK in tex_overrides.",
            latest.c_str());
        if (MessageBoxA(nullptr, msg, "texoverride update",
                        MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
            ShellExecuteA(nullptr, "open", "https://github.com/blancodagoat/texoverride/releases",
                          nullptr, nullptr, SW_SHOWNORMAL);
    }
    else logf("update check: up to date (latest %s)", latest.c_str());
    return 0;
}

// SEH so a fault in the scan cannot leave the hook waiting on an event nobody will ever set.
// (Own function: SEH inside BeatLoop's infinite loop confuses MSVC's return analysis.)
static bool scanFinishCppSafe()
{
    try { return scanFinish(); }
    catch (const std::exception& e) { logf("file scan failed: %s", e.what()); }
    catch (...) { logf("file scan failed with an unknown C++ exception"); }
    return false;
}

static void uninstallPump()
{
    PVOID volatile* slot = g_peekSlot;
    PeekMsg_t orig = (PeekMsg_t)InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr);
    if (!slot || !orig) return;
    DWORD old;
    if (!VirtualProtect((void*)slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    InterlockedCompareExchangePointer(slot, (PVOID)orig, (PVOID)h_peekMsg);
    VirtualProtect((void*)slot, sizeof(void*), old, &old);
    g_peekSlot = nullptr;
}
static void scanFinishSafe()
{
    bool ok = false;
    __try { ok = scanFinishCppSafe(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("FAULT during the file scan (code %08X) — overrides disabled for this session", GetExceptionCode());
    }
    if (!ok) {
        InterlockedCompareExchange(&g_scanState, SCAN_FAILED, SCAN_SCANNING);
        if (g_scanDone) SetEvent(g_scanDone);
    }
}

static DWORD WINAPI BeatLoop(LPVOID)
{
    crashSaverStartup();   // file I/O stays off the loader lock and precedes the pack scan
    g_crashSaverRan = true;
    if (g_off) {
        InterlockedExchange(&g_scanState, SCAN_FAILED);
        if (g_scanDone) SetEvent(g_scanDone);
        logf("disabled; pack scan, watcher and budget changes skipped");
        return 0;
    }
    locateRuntimePatternsSafe(); // optional scans run after DllMain but before READY is published
    scanFinishSafe();      // publishes the registry and releases the streaming hook
    loadPostStartupConfig();
    decideBudget();        // DXGI is off the loader lock and no longer delays override readiness
    int pumpMisses = 0;
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            // once streaming is live, start the live-reload watcher — before that there is
            // nothing a change could apply to anyway
            if (!g_watcherStarted && !g_off && atomicRead(&g_idsReady)) {
                HANDLE wt = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
                if (wt) { g_watcherStarted = true; CloseHandle(wt); }
                else logf("live reload: watcher thread could not start (err %lu), retrying", GetLastError());
            }
            // Re-assert one complete pass per second. The main-thread PeekMessageW pump consumes
            // it in bounded shards, avoiding a thousand-entry background write burst.
            if (!g_off && atomicRead(&g_idsReady) && g_mgr && g_mgr->entries) {
                LONGLONG lastPump = InterlockedCompareExchange64(&g_lastPumpAt, 0, 0);
                bool pumpInstalled = InterlockedCompareExchangePointer((PVOID volatile*)&g_origPeek, nullptr, nullptr) != nullptr;
                bool pumpHealthy = pumpInstalled && lastPump &&
                                   (LONGLONG)GetTickCount64() - lastPump <= 5000;
                if (pumpHealthy) {
                    pumpMisses = 0;
                    if (InterlockedExchange(&g_pumpUnavailable, 0))
                        logf("re-assert: main-thread pump resumed");
                    if (!atomicRead(&g_reassertFault)) InterlockedExchange(&g_reassertPending, 1);
                }
                // A slow loading screen can legitimately run for a long time before the first
                // PeekMessageW. Only judge liveness after this process has observed the pump.
                else if (lastPump && ++pumpMisses >= 5 && InterlockedCompareExchange(&g_pumpUnavailable, 1, 0) == 0) {
                    InterlockedExchange(&g_reassertPending, 0);
                    logf("re-assert: main-thread pump paused; reassertion will resume automatically when it returns");
                }
                else if (!lastPump) pumpMisses = 0;
            }
            if (!g_off) {
                { CsGuard lock(g_cs); placementBeatSafe(); }   // apply/re-assert tattoo placement edits
                budgetBeat();          // re-assert the raised texture budget (aligned data writes)
            }
        }
        logf("alive (beat %d) — reg=%ld redirects=%ld reclaims=%ld deferred=%ld baseRegistered=%s",
             beat, atomicRead(&g_regTotal), atomicRead(&g_redirects),
            atomicRead(&g_reclaims), atomicRead(&g_deferred),
             atomicRead(&g_registrationState) == REG_DONE ? "yes" : "no");
    }
}

// Setup runs inside LoadLibrary: if it faults, FiveM's asi loader shows "Couldn't load
// texoverride.asi" as a FATAL error and the game refuses to start until the file is deleted.
// A broken plugin must degrade to a do-nothing plugin, never brick the launch, so the fault is
// swallowed here and the beat/update threads are only started on success.
static bool SetupSafe()
{
    // stack overflow is NOT swallowed: continuing on this thread with an unarmed guard page
    // would convert a clean load failure into an unattributable crash later in game code
    __try { Setup(); return true; }
    __except ((GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {
        logf("FAULT during startup (code %08X) — plugin disabled for this session", GetExceptionCode());
        g_off = true;
        return false;
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h; g_gameTid = GetCurrentThreadId(); DisableThreadLibraryCalls(h);
        if (SetupSafe()) {   // synchronous: must finish before the game's entry point runs (see Setup)
            HANDLE beat = CreateThread(nullptr, 0, BeatLoop, nullptr, 0, nullptr);
            if (beat) CloseHandle(beat);
            else {
                g_off = true;
                InterlockedExchange(&g_scanState, SCAN_FAILED);
                if (g_scanDone) SetEvent(g_scanDone);
                logf("beat thread could not start (err %lu) — plugin disabled", GetLastError());
            }
            HANDLE update = CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
            if (update) CloseHandle(update);
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // orderly exit = no crash: the live-change journal must not quarantine anything.
        // A real crash never reaches this line, which is the whole point.
        // only after crashSaverStartup has processed any leftover journal: if Setup faulted
        // before that point, deleting here would erase the previous crash's evidence
        if (g_crashSaverRan && g_inflightPath[0]) DeleteFileA(g_inflightPath);
    }
    return TRUE;
}
