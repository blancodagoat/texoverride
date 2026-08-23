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
// one texture dictionary by exact name, bare .ycd files replace one animation dictionary, bare
// .ydr files named w_* replace one weapon drawable, and placement .xml files only ever touch
// tattoo preset floats after a fingerprint match (see the placement section). It also logs every
// distinct collection the server streams (tagged), so you can see what is in reach.
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
#include <set>
#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <algorithm>
#include "MinHook.h"

static HINSTANCE g_self;
static char g_logPath[MAX_PATH], g_overrideDir[MAX_PATH];
static bool g_off = false;

struct Ov {
    const char* slot; const char* file;   // both persistent, forward-slash, lowercased
    const char* gfile = nullptr;          // `file` as UTF-8: the only form the game may be handed
    uint32_t id = 0xFFFFFFFF;             // global streaming index our claim landed on
    uint32_t altId = 0xFFFFFFFF;          // the index the STORE resolves this name to, when
                                          // the claim landed somewhere else (see below)
    uint32_t handle = 0;                  // the handle value that points at OUR file
};

// FiveM reads EVERY narrow path it is handed as UTF-8: Cfx's ToWide (client/shared/Utils.cpp)
// runs utf8::replace_invalid over it before touching the disk. Our scan builds paths with the
// ANSI Win32 APIs instead. For an ASCII path those are the same bytes, which is why this went
// unnoticed. Put one non-ASCII letter in the Windows username and the ANSI bytes are invalid
// UTF-8, get swapped for U+FFFD, the path no longer exists, and every single claim comes back
// 0xFFFFFFFF with nothing on the ped and nothing in the log to say why. Reported on a Turkish
// username (issue #2) and proven there: same files, same build, ASCII account, worked at once.
// ANSI stays for our own file I/O, which is correct on the user's own code page.
static const char* toUtf8(const char* ansi)
{
    for (const unsigned char* p = (const unsigned char*)ansi; ; ++p) {
        if (!*p) return ansi;             // pure ASCII: identical in both, share the string
        if (*p > 0x7F) break;
    }
    int wn = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (wn <= 0) return ansi;
    std::wstring w((size_t)wn, L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, &w[0], wn)) return ansi;
    int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (un <= 0) return ansi;
    char* out = (char*)malloc((size_t)un);
    if (!out) return ansi;
    if (!WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out, un, nullptr, nullptr)) { free(out); return ansi; }
    return out;                           // leaked on purpose, like every other path here
}
static std::vector<Ov> g_ovs;
static HANDLE g_scanDone = nullptr;   // set once g_ovs is final; the hook waits on it
static volatile LONG g_waitLogged = 0;
static std::unordered_map<std::string, const char*> g_bySlot;   // slot -> file
static std::unordered_set<std::string> g_collSeen;   // distinct collections, for the map
static std::unordered_set<std::string> g_quarantine; // crash saver: keys refused this session
static bool g_crashSaverRan = false;                  // gates journal deletion on orderly exit
static volatile LONG g_journalHot = 0;                // startup registration is mid-flight in the journal
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
    // corrupt header could then report a tiny size and go unreported as huge
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
static bool rscCost(const char* path, uint64_t* virt, uint64_t* phys)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") || !fp) return false;
    uint32_t h[4] = {};
    size_t got = fread(h, sizeof(uint32_t), 4, fp);
    fclose(fp);
    if (got != 4 || h[0] != 0x37435352) return false;   // 'RSC7'
    *virt = rscSizeFromFlags(h[2]);
    *phys = rscSizeFromFlags(h[3]);
    return true;
}
static void probeVram();   // defined with the budget raiser below; fills g_vramTotal/g_vramBudget
static uint64_t g_costVirt, g_costPhys;
static std::vector<std::pair<uint64_t, std::string>> g_costBig;   // files >= 8 MB in memory

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

enum class LogCategory {
    Core,
    Scan,
    Collection,
    Audit,
    Claim,
    Verify,
    Live,
    Tattoo,
    Update
};

static LogLevel g_minLogLevel = LogLevel::Info;
static CRITICAL_SECTION g_logCs;
static bool g_logCsInit = false;

static inline const char* levelToString(LogLevel lvl)
{
    switch (lvl) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    default:              return "INFO";
    }
}

static inline const char* categoryToString(LogCategory cat)
{
    switch (cat) {
    case LogCategory::Core:       return "[CORE]";
    case LogCategory::Scan:       return "[SCAN]";
    case LogCategory::Collection: return "[COLLECTION]";
    case LogCategory::Audit:      return "[AUDIT]";
    case LogCategory::Claim:      return "[CLAIM]";
    case LogCategory::Verify:     return "[VERIFY]";
    case LogCategory::Live:       return "[LIVE]";
    case LogCategory::Tattoo:     return "[TATTOO]";
    case LogCategory::Update:     return "[UPDATE]";
    default:                      return "[CORE]";
    }
}

static void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...)
{
    if (level < g_minLogLevel) return;
    if (!g_logPath[0]) return;

    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);

    if (g_logCsInit) EnterCriticalSection(&g_logCs);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fprintf(f, "[%s] [%s] %s %s\n", ts, levelToString(level), categoryToString(cat), buf);
        if (level >= LogLevel::Warn) fflush(f);
        fclose(f);
    }
    if (g_logCsInit) LeaveCriticalSection(&g_logCs);
}

#define LOG_DEBUG(cat, fmt, ...) logMessage(LogLevel::Debug, cat, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  logMessage(LogLevel::Info,  cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  logMessage(LogLevel::Warn,  cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) logMessage(LogLevel::Error, cat, fmt, ##__VA_ARGS__)

// The size check every load path shares (startup scan, live new file, live overwrite).
// Over 32 MB in EITHER resource segment it says so and loads the file anyway. Until 0.8.1 it
// refused instead, because 32 MB is the line the summer-maine-steak crash sat on twice over:
// five files with 64 MB graphics segments (removal test), then a player crashing on mesh files
// whose 77+ MB sat in the virtual segment while graphics stayed under the line, same crash
// address both times. Refusing turned that into a silently half-applied pack, which reads as
// "the plugin is broken" and costs more support than the crash did, so the warning stands alone
// now. If oversized-file crashes come back this is the first thing to put back.
// The ONE thing still refused is a file that cannot be opened at all, since there is nothing to
// load; when the header will not read, the on-disk size stands in for the warning.
// Reading the header and judging it are separate so the startup scan can do the reads on
// several threads and then judge them in file order.
struct Cost { uint64_t cv, cp; bool readable, rsc; };
static Cost readCost(const char* path)
{
    Cost c = { 0, 0, true, true };
    if (rscCost(path, &c.cv, &c.cp)) return c;
    c.rsc = false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) { c.readable = false; return c; }
    c.cp = (((uint64_t)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;   // on-disk stand-in
    return c;
}
static bool cannotLoad(const char* key, const Cost& c, bool quiet)
{
    if (!c.readable) {
        if (!quiet) LOG_WARN(LogCategory::Scan, "UNREADABLE %s - cannot open it; not loaded this launch", key);
        return true;
    }
    uint64_t worst = (c.cv > c.cp) ? c.cv : c.cp;
    if (worst > (32ull << 20) && !quiet)
        LOG_WARN(LogCategory::Scan, "HUGE  %s - %.1f MB of %s data (loaded; if crashing, shrink with CodeWalker)",
                 key, worst / 1048576.0, (c.cv > c.cp) ? "mesh" : "texture");
    return false;
}
static bool cannotLoadPath(const char* path, const char* key, bool quiet)
{
    return cannotLoad(key, readCost(path), quiet);
}

// rage::strStreamingEngine::ms_info — the streaming info pool. Entries[id].handle is what the
// loader actually opens; layout from Cfx's gta-streaming-five/include/Streaming.h.
struct StrEntry { uint32_t handle, flags; };
// rage::strStreamingInfo packs three things into that second dword (Rockstar streamingdefs.h,
// cross-checked against Cfx's ReleaseObject(idx, 0xF1)):
//   bits 0-1   status: 0 NOTLOADED, 1 LOADED, 2 LOADREQUESTED, 3 LOADING
//   bits 2-15  dependent count
//   bits 16-31 STRFLAGs: DONTDELETE 1<<0, FORCE_LOAD 1<<1, PRIORITY_LOAD 1<<2, LOADSCENE 1<<3,
//              MISSION 1<<4, CUTSCENE 1<<5, INTERIOR 1<<6, ZONEDASSET 1<<7
// Read only. Never write a STRFLAG by hand: the setters also move the entry between the loaded
// and persistent lists and maintain m_numPriorityRequests, and a raw write desyncs both.
static const char* strStatusText(uint32_t flags)
{
    switch (flags & 3) {
    case 0:  return "not loaded";
    case 1:  return "ALREADY LOADED";
    case 2:  return "load requested";
    default: return "loading";
    }
}
struct StrMgr   { StrEntry* entries; char pad[16]; int numEntries; };
static StrMgr* g_mgr = nullptr;

static volatile LONG g_regTotal = 0, g_redirects = 0, g_idsReady = 0;
static long g_reclaims = 0, g_deferred = 0, g_lateBinds = 0;   // heartbeat thread only
static bool g_didRegister = false;
static bool g_b1 = true, g_b2 = false, g_captured = false;
static CRITICAL_SECTION g_cs;   // guards the one-time registration + the collection map (hook may run on >1 thread)

#define TEXOVERRIDE_VERSION "0.8.8"

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string fwd(std::string s)   { for (char& c : s) if (c=='\\') c='/'; return s; }

// The log must stay free of personal information (an absolute path carries the Windows user
// name), so override files are always logged relative to tex_overrides\.
static const char* rel(const char* file)
{
    size_t n = strlen(g_overrideDir);
    return strlen(file) > n ? file + n : file;
}

static bool isBlockedCollection(const std::string& coll);   // defined below, used by the classifier
static const char* classifyCollection(const std::string& coll)
{
    std::string c = lower(coll);
    if (c == "mp_m_freemode_01") return "Freemode Male";
    if (c.rfind("mp_m_freemode_01", 0) == 0) return "Freemode Male DLC";
    if (c == "mp_f_freemode_01") return "Freemode Female";
    if (c.rfind("mp_f_freemode_01", 0) == 0) return "Freemode Female DLC";
    if (c.rfind("a_c_", 0) == 0) return "Animal Ped";
    // must come after the freemode tests: mp_m_freemode_01 also starts with the blocked mp_m_
    if (isBlockedCollection(c)) return "Story/Ambient Ped";
    return "Custom Server Ped";
}

// SAFETY: only human freemode-ped collections may be touched. Everything else — animal peds
// (canine…), story/ambient peds (a_*, ig_*, cs_*), vehicles, weapons, props, maps, scripts — is
// left strictly alone. This is what stops a "dog head replaced by a human head" mistake.
// SAFETY: only ped collections may be touched, and only two families of them.
//   mp_?_freemode_01*  the player's own ped
//   a_c_*              animal peds. The eight dogs and cats that are "streamed peds" (chop,
//                      husky, mtlion, panther, retriever, rottweiler, sharktiger, shepherd) are
//                      built exactly like a freemode ped: a collection folder of head_/uppr_/
//                      lowr_/accs_/teef_ drawables and their txds. Verified against
//                      x64e.rpf, models/cdimages/streamedpeds_a_c.rpf.
// Everything else (story and ambient peds, vehicles, weapons, props, maps, scripts) is left
// strictly alone.
static bool isPedCollection(const std::string& coll)
{
    std::string c = lower(coll);
    return c.rfind("mp_m_freemode_01", 0) == 0 || c.rfind("mp_f_freemode_01", 0) == 0
        || c.rfind("a_c_", 0) == 0;
}

// Servers ship their own peds and name the collection whatever the author felt like: canine,
// caninesd, caninefd, blackcat, browncat on one real server, none of which any prefix rule could
// guess. So the folder name is no longer what decides. The FILE name is, and it is the better
// test anyway: ped parts are named from a closed vocabulary that Rockstar has used since launch,
// twelve component slots and eleven prop anchors, always followed by a three digit number. A
// vehicle txd, a prop drawable, a map file, none of them look like this, so they still cannot get
// in no matter what folder somebody puts them in. That is the guarantee the folder whitelist was
// really buying, kept, while server peds stop being collateral damage.
// The promise the README makes is that story and cutscene characters are never touched, and that
// survives the change above: these are the prefixes every collection-based non-freemode ped in
// b3751 uses (147 cs_, 69 ig_, plus the stragglers), read out of docs/ped_collections.tsv. They
// are refused by name whatever their files are called. Ambient peds (s_m_*, a_f_* and friends)
// need no entry: the scan found none of them are collection-based, so there is no slot to take.
static bool isBlockedCollection(const std::string& coll)
{
    static const char* kBlocked[] = {
        "cs_", "csb_", "ig_", "hc_", "player_", "p_michael", "p_franklin", "mp_headtargets",
        "mp_m_", "mp_f_", "mp_s_",          // any mp_ ped that is not freemode_01, checked after it
        "s_m_", "s_f_", "a_m_", "a_f_", "u_m_", "u_f_", "g_m_", "g_f_",
    };
    std::string c = lower(coll);
    for (const char* b : kBlocked) if (c.rfind(b, 0) == 0) return true;
    return false;
}
static bool isPedComponentFile(const std::string& file)
{
    static const char* kComp[] = { "head", "berd", "hair", "uppr", "lowr", "hand",
                                   "feet", "teef", "accs", "task", "decl", "jbib" };
    static const char* kAnchor[] = { "head", "eyes", "ears", "mouth", "lhand", "rhand",
                                     "lwrist", "rwrist", "hip", "lfoot", "rfoot" };
    std::string f = lower(file);
    size_t at = std::string::npos;
    if (f.rfind("p_", 0) == 0) {                       // a prop: p_<anchor>_...
        for (const char* a : kAnchor) {
            std::string pre = std::string("p_") + a + "_";
            if (f.rfind(pre, 0) == 0) { at = pre.size(); break; }
        }
    } else {
        for (const char* c : kComp) {
            std::string pre = std::string(c) + "_";
            if (f.rfind(pre, 0) == 0) { at = pre.size(); break; }
        }
    }
    if (at == std::string::npos) return false;
    if (f.compare(at, 5, "diff_") == 0) at += 5;       // <comp>_diff_<nnn>_<letter>_<race>.ytd
    return f.size() >= at + 3 && isdigit((unsigned char)f[at])
        && isdigit((unsigned char)f[at+1]) && isdigit((unsigned char)f[at+2]);
}
static std::string collectionOf(const std::string& key)   // "collection/file" -> "collection"
{
    size_t s = key.find('/');
    return (s == std::string::npos) ? key : key.substr(0, s);
}
static bool hasExt(const std::string& k, const char* e)
{
    return k.size() > 4 && k.compare(k.size()-4, 4, e) == 0;
}
// Every type the plugin will even consider. The policy gate is isAllowedKey; this exists so the
// folder walk can stop early on readmes, archives and loose textures.
static bool isOverrideExt(const std::string& ln)
{
    return hasExt(ln, ".ytd") || hasExt(ln, ".ydd") || hasExt(ln, ".yft")
        || hasExt(ln, ".ymt") || hasExt(ln, ".ycd") || hasExt(ln, ".ydr");
}
// A key with a slash is a component slot ("collection/file") and its collection must be one of
// the two families above.
//
// A key without one is a bare-name asset. Overlay txds (skin, tattoos, facepaint, hair, beards,
// shirt decals...) live loose in the overlay rpfs and stream by filename alone, and a scan of
// every *overlay*.rpf found ~100 unrelated naming families plus arbitrarily-named server packs,
// so no name whitelist can work there. The gate is type + exactness instead: .ytd only, matching
// the dictionary whose exact name it bears.
//
// Animal peds are the exception. Most of them (pug, poodle, westy, cat, coyote, deer...) are not
// collection-based at all: model, fragment and variation metadata are bare a_c_<name>.ydd/.yft/
// .ymt files. The eight collection-based ones still keep their .yft and .ymt at the root beside
// the folder, and the .ymt (a CPedVariationInfo) is what makes any drawable or texture a mod ADDS
// on top of vanilla selectable at all. So those three types are allowed at the root, for a_c_
// names only. One player's .ymt took the game down at registration on 0.8.0; that is now the
// startup crash saver's job to contain rather than a reason to refuse the whole type.
// A .ymt registers cleanly when its name is one the game has never seen: a connected server does
// exactly that a dozen times a session for its own clothing packs, through this same call, with
// no trouble at all. Hand the call a .ymt name the game ALREADY owns and it faults three frames
// in and takes the game with it (ink-island-iowa, GTA5_b3751.exe+16765E0). The .yft beside it
// survives the same treatment because a fragment and a metadata file land in different stores,
// and only the metadata store dies this way.
//
// Every animal ships its own .ymt, checked against b3751 with CodeWalker (docs/ped_collections.tsv),
// so a user's animal .ymt always collides and can never win. Timing does not rescue it either:
// 0.8.4 handed them over a minute into the session instead and it died just the same. These eight
// names are refused. Every other .ymt name is allowed, which is what a clothing pack needs.
static bool isVanillaAnimalYmt(const std::string& key)
{
    static const char* kVanilla[] = {
        "a_c_chop.ymt", "a_c_husky.ymt", "a_c_mtlion.ymt", "a_c_panther.ymt",
        "a_c_retriever.ymt", "a_c_rottweiler.ymt", "a_c_sharktiger.ymt", "a_c_shepherd.ymt",
    };
    std::string k = lower(key);
    for (const char* v : kVanilla) if (k == v) return true;
    return false;
}
static bool isAllowedKey(const std::string& key)
{
    size_t s = key.find('/');
    if (s != std::string::npos) {
        if (!hasExt(key, ".ydd") && !hasExt(key, ".ytd")) return false;
        std::string coll = key.substr(0, s);
        // the two known families keep working exactly as they did, odd filenames included
        if (isPedCollection(coll)) return true;
        if (isBlockedCollection(coll)) return false;
        return isPedComponentFile(key.substr(s + 1));
    }
    if (hasExt(key, ".ytd")) return true;
    // .ycd: a clip dictionary, the file an animation lives in. Allowed at the root by
    // exact name, same rule as a .ytd. Clips inside are looked up by joaat hash, not by
    // the label CodeWalker prints, so a replacement dictionary only has to carry the
    // right hashes. Worth having because a server streams its own anim dictionaries
    // through this very call, and the re-assert loop is the only thing on the client
    // that can take a slot back off one.
    if (hasExt(key, ".ycd")) return true;
    // .ydr: a weapon drawable. Every GTA V weapon — base game and DLC — uses the w_
    // prefix (w_pi_pistol, w_ar_assaultrifle, w_sg_pumpshotgun, etc.), and server-added
    // weapons follow the same convention. That prefix is what separates weapon drawables
    // from vehicle parts, props, building pieces and every other .ydr in the game.
    if (hasExt(key, ".ydr")) return lower(key).rfind("w_", 0) == 0;
    if (hasExt(key, ".ymt")) return !isVanillaAnimalYmt(key);
    return lower(key).rfind("a_c_", 0) == 0
        && (hasExt(key, ".ydd") || hasExt(key, ".yft"));
}

// Types we walk past on purpose. Announcing beats a silent skip: a .meta is a file mods really do
// ship, so a user who sees nothing assumes the plugin broke.
static bool isIgnoredType(const std::string& ln, const std::string& rel, bool announce)
{
    if (rel.find('/') == std::string::npos && isVanillaAnimalYmt(ln)) {
        if (announce) LOG_INFO(LogCategory::Scan, "IGNORED %s - vanilla animal .ymt collides with game metadata; other mod files still load", rel.c_str());
        return true;
    }
    if (ln.size() > 5 && ln.compare(ln.size()-5, 5, ".meta") == 0) {
        if (announce) LOG_INFO(LogCategory::Scan, "IGNORED %s - .meta files hold shop/menu data, not looks; see README", rel.c_str());
        return true;
    }
    return false;
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
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { walkDir(base, childRel, out); continue; }
        std::string ln = lower(name);
        if (isIgnoredType(ln, fwd(childRel), true)) continue;
        if (!isOverrideExt(ln)) continue;
        std::string slotStr = lower(fwd(childRel));   // "mp_m_freemode_01/teef_004_u.ydd" or bare "mp_fm_skin_m_up_whi.ytd"
        // SAFETY GATE: folders must be a freemode or animal ped collection (see isAllowedKey).
        if (!isAllowedKey(slotStr)) {
            LOG_WARN(LogCategory::Scan, "SKIP %s - folder contents must use GTA ped part naming (e.g. head_000_r.ydd, uppr_diff_001_a_uni.ytd)", slotStr.c_str());
            continue;
        }
        if (g_quarantine.count(slotStr)) continue;   // crash saver; already logged loudly
        out.push_back({ slotStr, fwd(base + childRel), Cost() });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// The header reads ARE the cost of a big pack: one file open each, every one of them a disk
// seek and an antivirus look. They do not touch each other, so spread them over a few threads.
// This may only ever run OFF the loader lock (see scanFinish): a thread created inside DllMain
// cannot start until that lock is free, so joining one there deadlocks the launch. 0.7.3 did
// exactly that and hung on "Launching FiveM".
struct CostJob { std::vector<Cand>* v; volatile LONG next; };
static DWORD WINAPI costWorker(LPVOID p)
{
    CostJob* j = (CostJob*)p;
    for (;;) {
        LONG i = InterlockedIncrement(&j->next) - 1;
        if (i < 0 || (size_t)i >= j->v->size()) return 0;
        (*j->v)[i].c = readCost((*j->v)[i].full.c_str());
    }
}
static void readCosts(std::vector<Cand>& v)
{
    if (v.empty()) return;
    SYSTEM_INFO si; GetSystemInfo(&si);
    unsigned want = si.dwNumberOfProcessors;
    if (want < 1) want = 1;
    if (want > 8) want = 8;
    if (want > v.size()) want = (unsigned)v.size();
    CostJob job = { &v, 0 };
    HANDLE th[8]; unsigned made = 0;
    for (unsigned i = 0; i + 1 < want; ++i) {   // this thread is one of the workers
        HANDLE t = CreateThread(nullptr, 0, costWorker, &job, 0, nullptr);
        if (!t) break;
        th[made++] = t;
    }
    costWorker(&job);
    for (unsigned i = 0; i < made; ++i) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
}

static std::vector<Cand> g_cands;   // filled and consumed by the background startup pass
static void costReport();   // defined with the budget code, which it compares the pack against

// Runs on the beat thread, NOT in DllMain. Everything here opens files, and Setup() sits in
// front of the game's entry point: doing it there meant a big pack held the loading screen
// before the game had even started. The hook waits on g_scanDone before it registers anything,
// so the game gets on with its own startup while this runs.
static void scanFinish()
{
    ULONGLONG t0 = GetTickCount64();
    readCosts(g_cands);
    EnterCriticalSection(&g_cs);   // the watcher can be appending to g_ovs by now
    int n = 0;
    for (auto& cd : g_cands) {
        if (cannotLoad(cd.slot.c_str(), cd.c, false)) continue;
        const char* slot = _strdup(cd.slot.c_str());
        const char* file = _strdup(cd.full.c_str());   // our absolute path
        g_ovs.push_back({ slot, file, toUtf8(file) });
        g_bySlot[slot] = file;
        ++n;
        if (cd.c.rsc) {
            g_costVirt += cd.c.cv; g_costPhys += cd.c.cp;
            if (cd.c.cv + cd.c.cp >= (8u << 20)) g_costBig.push_back({ cd.c.cv + cd.c.cp, cd.slot });
        }
    }
    LeaveCriticalSection(&g_cs);
    LOG_INFO(LogCategory::Scan, "Loaded %d override(s) in %.1fs (mode %s)", n, (GetTickCount64() - t0) / 1000.0, g_off ? "OFF" : "ON");
    // say it out loud when the two path forms differ, so the next report of this arrives already
    // diagnosed instead of looking like "nothing works and the log looks fine"
    if (!g_ovs.empty() && g_ovs[0].gfile && g_ovs[0].gfile != g_ovs[0].file)
        LOG_INFO(LogCategory::Scan, "Folder path contains non-English characters; using UTF-8 representation");

    // Separate collection folders from loose root assets
    std::unordered_map<std::string, int> collCounts;
    std::unordered_map<std::string, int> rootExtCounts;
    for (auto& ov : g_ovs) {
        size_t s = std::string(ov.slot).find('/');
        if (s != std::string::npos) {
            ++collCounts[std::string(ov.slot).substr(0, s)];
        } else {
            const char* dot = strrchr(ov.slot, '.');
            std::string ext = (dot && dot[1]) ? lower(dot + 1) : "(none)";
            ++rootExtCounts[ext];
        }
    }

    if (!collCounts.empty()) {
        LOG_INFO(LogCategory::Scan, "  Collections (%zu):", collCounts.size());
        for (auto& kv : collCounts) {
            LOG_INFO(LogCategory::Scan, "    %-38s %3d file(s)  [%s]", kv.first.c_str(), kv.second, classifyCollection(kv.first));
        }
    }
    if (!rootExtCounts.empty()) {
        LOG_INFO(LogCategory::Scan, "  Root Assets:");
        for (auto& kv : rootExtCounts) {
            const char* typeDesc = "Other";
            if (kv.first == "ytd") typeDesc = "Overlays (.ytd)";
            else if (kv.first == "ydr") typeDesc = "Weapon models (.ydr)";
            else if (kv.first == "ycd") typeDesc = "Clip animations (.ycd)";
            else if (kv.first == "ymt" || kv.first == "yft" || kv.first == "ydd") typeDesc = "Animal models/meta";
            LOG_INFO(LogCategory::Scan, "    %-38s %3d file(s)", typeDesc, kv.second);
        }
    }

    costReport();
    g_cands.clear(); g_cands.shrink_to_fit();
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
    if (ps == std::string::npos || pe == std::string::npos) { LOG_WARN(LogCategory::Tattoo, "Placement: %s has no <presets>, ignored", fname); return; }

    PlColl pc; pc.src = fname;
    if (!text(pe, x.size(), "nameHash", pc.name)) { LOG_WARN(LogCategory::Tattoo, "Placement: %s has no collection nameHash, ignored", fname); return; }
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
    if (pc.presets.size() < 3) { LOG_WARN(LogCategory::Tattoo, "Placement: %s has %zu preset(s) (need 3+ to fingerprint), ignored", fname, pc.presets.size()); return; }
    out.push_back(std::move(pc));
}

static void placementLocate()
{
    // Cfx's own pattern for the tattoo-sort call site (PatchTattooSort.cpp); the comparator it
    // points at loads ms_instance and the collections-array offset.
    const short PAT[] = { 0x41,0x0F,0xB7,0xDE,0x4C,0x8D,0x0D,-1,-1,-1,-1,0x41,0xB8 };
    uint8_t* p = scanModule(PAT, 13);
    if (!p) { LOG_WARN(LogCategory::Tattoo, "PedDecorationManager pattern NOT FOUND — placement files inert"); return; }
    uint8_t* comparator = ripTarget(p + 7);
    g_decorMgr = (uint8_t**)ripTarget(comparator + 3);
    int32_t off = *(int32_t*)(comparator + 7 + 3);
    if (off <= 0 || off > 0x10000) { LOG_WARN(LogCategory::Tattoo, "Implausible collections offset 0x%x — placement disabled", off); g_decorMgr = nullptr; return; }
    g_decorCollOff = off;
    LOG_INFO(LogCategory::Tattoo, "PedDecorationManager @ %p, collections at +0x%x", (void*)g_decorMgr, off);
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
                        LOG_INFO(LogCategory::Tattoo, "%s layout solved (array@+0x%02x stride=0x%x uv+0x%x, %zu/%zu stock matched)",
                                 pc.name.c_str(), o, s, f, hits, N);
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
        LOG_WARN(LogCategory::Tattoo, "%s hit unreadable memory while solving, skipped", pc.name.c_str());
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
                if (++pc.writes <= 40) LOG_INFO(LogCategory::Tattoo, "PLACEMENT: %s[%zu] field %d -> %f", pc.name.c_str(), i, k, pc.presets[i].v[k]);
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
        LOG_ERROR(LogCategory::Tattoo, "Memory fault — tattoo placement disabled for this session");
    }
}
// ========================================================================================

typedef uint32_t* (*RegRaw_t)(uint32_t*, const char*, bool, const char*, bool);
static RegRaw_t o_regRaw = nullptr;

// FiveM's name map only covers files registered by FiveM itself, so it cannot resolve many base
// RPF slots. For an occupied-slot takeover, ask the game's actual streaming module for the slot
// instead. These Cfx exports use only pointers/C strings; no STL object crosses a DLL boundary.
typedef void* (*GetStreamingManager_t)();
typedef void* (*GetStreamingModule_t)(void*, const char*);
static GetStreamingManager_t g_getStreamingManagerFn = nullptr;
static GetStreamingModule_t g_getStreamingModuleFn = nullptr;

// ABI-compatible views of rage::fiCollection::RawEntry and its public pgRawStreamer array.
struct RawEntryView {
    uint64_t packedFileEntry;
    uint32_t virtFlags, physFlags;
    uint64_t timestamp;
    const char* fileName;
};
static_assert(sizeof(RawEntryView) == 32, "FiveM RawEntry layout changed");
struct RawEntriesView { RawEntryView* memory[64]; uint32_t count; };
typedef const RawEntriesView* (*GetRawEntries_t)();
static GetRawEntries_t g_getRawEntriesFn = nullptr;

static void resolveOccupiedSlotExports()
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

static int runningGameBuild()
{
    char path[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return 0;
    for (const char* p = path; (p = strstr(p, "_b")) != nullptr; p += 2) {
        int build = atoi(p + 2);
        if (build >= 1000 && build <= 9999) return build;
    }
    return 0;
}

typedef uint32_t* (*FindModuleSlot_t)(void*, uint32_t*, const char*);

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

// Why a name lookup did not produce a slot. A whole file type failing looks identical to a single
// missing name unless the reason is carried out, and that cost a session of guessing once.
enum SlotWhy { SLOT_OK = 0, SLOT_NO_EXPORTS, SLOT_BAD_NAME, SLOT_NO_MANAGER, SLOT_NO_MODULE, SLOT_NO_NAME };
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

// Reported once per file type, so "every .ycd failed" is one line instead of silence.
static std::unordered_map<std::string, int> g_slotWhyByExt;

static uint32_t targetStreamingId(const char* slot, int* why = nullptr)
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
        if (id == 0xFFFFFFFF) return fail(SLOT_NO_NAME);
        if (why) *why = SLOT_OK;
        return id;
    }
    catch (...) { return fail(SLOT_NO_MANAGER); }
}

// Record the outcome for this file type, and say it out loud the first time it is seen.
static void noteSlotWhy(const char* slot, int why)
{
    const char* dot = strrchr(slot, '.');
    if (!dot || !dot[1]) return;
    std::string ext = lower(dot + 1);
    auto it = g_slotWhyByExt.find(ext);
    if (it != g_slotWhyByExt.end() && it->second == why) return;
    g_slotWhyByExt[ext] = why;
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

enum OccupiedResult { OCCUPIED_FAILED = 0, OCCUPIED_ATTACHED = 1, OCCUPIED_WAITING = 2 };

static bool validStreamingId(uint32_t id)
{
    // GetStreamingIndex-style APIs use zero for a miss; never write entry zero.
    return g_mgr && g_mgr->entries && id != 0 && id != 0xFFFFFFFF &&
           id < (uint32_t)g_mgr->numEntries;
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

static int recoverOccupiedSlot(Ov& ov)
{
    resolveOccupiedSlotExports();
    if (!g_mgr) g_mgr = exportedManagerSafe();
    uint32_t rawHandle = 0;
    if (!localRawHandle(ov.gfile ? ov.gfile : ov.file, &rawHandle)) return OCCUPIED_FAILED;
    ov.handle = rawHandle;

    uint32_t target = targetStreamingId(ov.slot);
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

struct LiveOp { int kind; Ov ov; uint32_t handle; };            // kind: 0 = register, 1 = re-stat
// every call site freed slot and file and forgot gfile, which 0.8.3 added beside them
static void freeLiveOp(LiveOp& op)
{
    free((void*)op.ov.slot); free((void*)op.ov.file);
    if (op.ov.gfile && op.ov.gfile != op.ov.file) free((void*)op.ov.gfile);
    op.ov.slot = op.ov.file = op.ov.gfile = nullptr;
}
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
// Returns the confirmed outcome:
//   0 direct  1 rejected  2 no handle  3 fault  4 occupied takeover  5 waiting for target
static int liveRegister(Ov& ov)
{
    __try {
        uint32_t id = 0xFFFFFFFF;
        o_regRaw(&id, ov.gfile ? ov.gfile : ov.file, g_b1, ov.slot, g_b2);
        ov.id = id;
        if (id == 0xFFFFFFFF) {
            int occupied = recoverOccupiedSlot(ov);
            return occupied == OCCUPIED_ATTACHED ? 4 :
                   occupied == OCCUPIED_WAITING ? 5 : 1;
        }
        if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
            ov.handle = g_mgr->entries[id].handle;
        return ov.handle ? 0 : 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

// ---- main-thread pump: IAT shim on the game exe's PeekMessageW import ----
typedef BOOL (WINAPI* PeekMsg_t)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMsg_t g_origPeek = nullptr;
static DWORD g_pumpTid = 0;

static void drainOps()   // runs on the game's main thread
{
    EnterCriticalSection(&g_cs);
    // A folder copy can queue hundreds at once. Take a small bounded shard: PeekMessageW runs
    // every frame, so the rest lands on the next few instead of one giant stall on the game
    // thread that a player experiences as a freeze.
    std::vector<LiveOp> ops;
    for (int i = 0; i < 8 && !g_opQ.empty(); ++i) { ops.push_back(g_opQ.front()); g_opQ.pop_front(); }
    LeaveCriticalSection(&g_cs);
    for (auto& op : ops) {
        if (op.kind == 0) {
            int why = liveRegister(op.ov);
            if (why == 0 || why == 4 || why == 5) {
                EnterCriticalSection(&g_cs);
                g_ovs.push_back(op.ov); g_bySlot[op.ov.slot] = op.ov.file;
                LeaveCriticalSection(&g_cs);
                if (why == 0)
                    LOG_INFO(LogCategory::Live, "LIVE-ADD: %s <- tex_overrides/%s (id=%u handle=%08x)", op.ov.slot, rel(op.ov.file), op.ov.id, op.ov.handle);
                else if (why == 4)
                    LOG_INFO(LogCategory::Live, "LIVE-TAKEOVER: %s <- tex_overrides/%s (id=%u raw handle=%08x)", op.ov.slot, rel(op.ov.file), op.ov.id, op.ov.handle);
                else
                    LOG_INFO(LogCategory::Live, "LIVE-WAIT: %s (raw handle=%08x; target slot attached when present)", op.ov.slot, op.ov.handle);
                uint64_t cv = 0, cp = 0;
                if (rscCost(op.ov.file, &cv, &cp) && cv + cp >= (8u << 20))
                    LOG_WARN(LogCategory::Live, "  HEAVY %.1f MB in memory (likely 4K or uncompressed; shrink to fight texture loss)", (cv + cp) / 1048576.0);
            } else if (why == 1) {
                // registerRawStreamingFile refuses a slot that already holds a handle.
                // recoverOccupiedSlot has already tried the takeover path by here, so
                // reaching this means we could not find a raw entry for our own file
                // either. A restart claims the slot before the server or DLC mounts.
                LOG_WARN(LogCategory::Live, "Live reload: %s is already loaded from server/DLC and no raw entry was available; restart FiveM to claim", op.ov.slot);
                freeLiveOp(op);
            } else {
                LOG_ERROR(LogCategory::Live, "Live reload: could not register %s (%s), restart to pick it up", op.ov.slot,
                          why == 2 ? "no handle came back" : "fault inside the game's register call");
                freeLiveOp(op);
            }
        } else {
            if (g_getRawStreamerFn && g_rawGetEntryFn && rawInvalidate(op.handle))
                LOG_INFO(LogCategory::Live, "LIVE-UPDATE: %s reread from disk (reapply outfit/tattoo to see it)", op.ov.slot);
            else
                LOG_WARN(LogCategory::Live, "Live reload: %s changed, could not refresh it, restart to apply", op.ov.slot);
            free((void*)op.ov.slot);
        }
    }
    EnterCriticalSection(&g_cs);
    if (g_opQ.empty()) InterlockedExchange(&g_opsPending, 0);   // more shards may still be waiting
    LeaveCriticalSection(&g_cs);
}

static BOOL WINAPI h_peekMsg(LPMSG m, HWND w, UINT a, UINT b, UINT r)
{
    // the first caller after install is the game's main loop (it pumps every frame);
    // only that thread ever drains, so ops always run where Cfx runs its own registrations
    DWORD tid = GetCurrentThreadId();
    if (!g_pumpTid) g_pumpTid = tid;
    if (g_opsPending && tid == g_pumpTid) drainOps();
    return g_origPeek(m, w, a, b, r);
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
            g_origPeek = (PeekMsg_t)thunk->u1.Function;
            thunk->u1.Function = (ULONGLONG)h_peekMsg;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// ---- crash saver ----
// APPEND, not overwrite. This opened with "w", so a second batch queued while the first was
// still inside its 30 second window wiped the first batch out of the journal, and a crash
// then quarantined the wrong file or none at all. A journal that cannot be written now
// refuses the batch: it is the only thing making a bad file cost one launch, not every one.
static bool journalWrite(const std::vector<LiveOp>& batch)
{
    FILE* f; if (fopen_s(&f, g_inflightPath, "a") != 0 || !f) return false;
    bool ok = true;
    for (auto& op : batch) if (fprintf(f, "%s\n", op.ov.slot) < 0) { ok = false; break; }
    if (ferror(f) || fflush(f) != 0) ok = false;
    if (fclose(f) == EOF) ok = false;
    return ok;
}

// Same journal, one key, for the startup registration loop. That loop hands the game one file at
// a time, so the journal names exactly the file in the game's hands: a fault inside
// registerRawStreamingFile quarantines that one file and nothing else. Live batches go in
// together and are journaled together, which is why there are two writers.
static void journalOne(const char* key)
{
    FILE* f; if (fopen_s(&f, g_inflightPath, "w") != 0 || !f) return;
    fputs(key, f); fputc('\n', f);
    fclose(f);
}

// Called at startup, before the folder scan. A leftover _inflight.txt means the game died while
// (or moments after) those files were being applied live — quarantine them.
static void crashSaverStartup()
{
    FILE* f;
    if (fopen_s(&f, g_inflightPath, "r") == 0 && f) {
        char line[512]; bool any = false;
        FILE* q = nullptr; fopen_s(&q, g_quarantinePath, "a");
        while (fgets(line, sizeof line, f)) {
            std::string k = line;
            while (!k.empty() && (k.back() == '\n' || k.back() == '\r')) k.pop_back();
            if (k.empty()) continue;
            if (g_quarantine.insert(k).second && q) fprintf(q, "%s\n", k.c_str());
            any = true;
        }
        fclose(f); if (q) fclose(q);
        if (any) LOG_WARN(LogCategory::Scan, "CRASH SAVER: Last run died while registering file(s); quarantined to prevent crash loop");
    }
    DeleteFileA(g_inflightPath);
    if (fopen_s(&f, g_quarantinePath, "r") == 0 && f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            std::string k = line;
            while (!k.empty() && (k.back() == '\n' || k.back() == '\r')) k.pop_back();
            if (!k.empty()) g_quarantine.insert(k);
        }
        fclose(f);
        for (auto& k : g_quarantine)
            LOG_WARN(LogCategory::Scan, "QUARANTINED %s — not loaded; delete _quarantine.txt in tex_overrides to try it again", k.c_str());
    }
}

struct Snap { uint64_t wt; uint64_t size; };
static std::unordered_map<std::string, Snap> g_snap;   // full path -> stamp; watcher thread only

static void mergePlacement(std::vector<PlColl>& fresh)
{
    EnterCriticalSection(&g_cs);
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
        LOG_INFO(LogCategory::Tattoo, "Placement: %s reloaded from %s (%zu presets%s)", npc.name.c_str(), npc.src.c_str(),
                 npc.presets.size(), npc.solved ? ", layout kept" : "");
        if (old) *old = std::move(npc); else g_pl.push_back(std::move(npc));
    }
    LeaveCriticalSection(&g_cs);
    if (g_plFault) LOG_WARN(LogCategory::Tattoo, "Placement: NOTE — placement is disabled for this session (earlier fault), edits will apply after a restart");
}

// Detects what changed and queues work; the game-touching half runs later in drainOps on the
// main thread. quiet = the baseline pass right after the watcher starts: it seeds the stamp map
// (and still queues files that appeared during loading) without re-logging SKIP/IGNORED lines
// the startup scan already wrote.
static void rescanTree(const std::string& base, const std::string& sub, bool quiet, std::vector<std::string>& xmls, std::vector<LiveOp>& batch)
{
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((base + sub + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string childRel = sub.empty() ? name : sub + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { rescanTree(base, childRel, quiet, xmls, batch); continue; }

        std::string full = base + childRel;
        Snap now{ ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime,
                  ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow };
        auto it = g_snap.find(full);
        bool isNew = (it == g_snap.end()), isChanged = !isNew && (it->second.wt != now.wt || it->second.size != now.size);
        g_snap[full] = now;
        if (!isNew && !isChanged) continue;

        std::string ln = lower(name);
        if (isIgnoredType(ln, fwd(childRel), isNew && !quiet)) continue;
        if (sub.empty() && ln.size() > 4 && ln.compare(ln.size()-4, 4, ".xml") == 0) { if (!quiet) xmls.push_back(name); continue; }
        if (!isOverrideExt(ln)) continue;

        std::string key = lower(fwd(childRel));
        if (g_quarantine.count(key)) continue;   // crash saver: refused until _quarantine.txt is deleted
        if (!isAllowedKey(key)) {
            if (isNew && !quiet) LOG_WARN(LogCategory::Scan, "SKIP %s - folder contents must use GTA ped part naming", key.c_str());
            continue;
        }

        EnterCriticalSection(&g_cs);
        uint32_t handle = 0; bool known = false;
        for (auto& ov : g_ovs) if (key == ov.slot) { known = true; handle = ov.handle; break; }
        LeaveCriticalSection(&g_cs);

        if (!known) {
            if (!g_origPeek) { LOG_INFO(LogCategory::Live, "Live reload: new file %s needs a game restart", key.c_str()); continue; }
            if (cannotLoadPath(fwd(full).c_str(), key.c_str(), quiet)) continue;   // quiet skips the baseline re-log
            { const char* nf = _strdup(fwd(full).c_str());
              batch.push_back({ 0, { _strdup(key.c_str()), nf, toUtf8(nf) }, 0 }); }
        }
        else if (handle && isChanged) {
            if ((handle >> 16) != 0)   // not in the game's own raw streamer; cannot re-stat it
                LOG_WARN(LogCategory::Live, "Live reload: %s changed, restart to apply (handle %08x not raw)", key.c_str(), handle);
            else if (!g_origPeek)
                LOG_INFO(LogCategory::Live, "Live reload: %s changed, restart to apply", key.c_str());
            else if (cannotLoadPath(fwd(full).c_str(), key.c_str(), false))
                // the registered slot still points at this path with the OLD size cached, so the
                // game may read a truncated slice of the new content; make that loud
                LOG_WARN(LogCategory::Live, "Live reload: %s cannot be opened; not reread", key.c_str());
            else
                batch.push_back({ 1, { _strdup(key.c_str()), nullptr }, handle });
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Hand a batch to the game thread: journal first (crash saver), one batch in flight at a time.
static void submitBatch(std::vector<LiveOp>& batch)
{
    // No waiting on the previous batch any more. The queue is a deque the pump drains in shards,
    // so a new batch just joins the back; the old 10 second sleep loop blocked the watcher thread
    // and threw the batch away for no reason other than the queue being busy.
    if (!journalWrite(batch)) {
        LOG_ERROR(LogCategory::Live, "Live reload: crash-saver journal could not be written; change not applied");
        for (auto& op : batch) freeLiveOp(op);
        return;
    }
    EnterCriticalSection(&g_cs);
    for (auto& op : batch) g_opQ.push_back(op);
    LeaveCriticalSection(&g_cs);
    InterlockedExchange(&g_opsPending, 1);
    g_journalClearAt = GetTickCount64() + 30000;   // journal outlives the apply; see CRASH SAVER above
}

static DWORD WINAPI WatchLoop(LPVOID)
{
    std::string dir = g_overrideDir; if (!dir.empty() && dir.back() == '\\') dir.pop_back();
    HANDLE h = FindFirstChangeNotificationA(dir.c_str(), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    if (h == INVALID_HANDLE_VALUE) { LOG_ERROR(LogCategory::Live, "Live reload: cannot watch tex_overrides (err %lu) — restart to apply changes", GetLastError()); return 0; }

    bool pump = installPump();
    { std::vector<std::string> ignore; std::vector<LiveOp> batch;
      rescanTree(g_overrideDir, "", true, ignore, batch);   // baseline stamps; also catches files added during loading
      if (!batch.empty()) submitBatch(batch); }
    LOG_INFO(LogCategory::Live, "Directory watcher active (%s)", pump ? "full: edits and new files" : "edits only: new files need restart");

    for (;;) {
        DWORD w = WaitForSingleObject(h, g_journalClearAt ? 1000 : INFINITE);
        if (g_journalClearAt && GetTickCount64() >= g_journalClearAt && !g_opsPending) {
            DeleteFileA(g_inflightPath);   // survived the risky window; nothing to quarantine
            g_journalClearAt = 0;
        }
        if (w == WAIT_TIMEOUT) continue;
        if (w != WAIT_OBJECT_0) break;
        do { FindNextChangeNotification(h); } while (WaitForSingleObject(h, 500) == WAIT_OBJECT_0);   // debounce until quiet
        std::vector<std::string> xmls; std::vector<LiveOp> batch;
        rescanTree(g_overrideDir, "", false, xmls, batch);
        if (!batch.empty()) submitBatch(batch);
        if (!xmls.empty()) {
            std::vector<PlColl> fresh;
            for (auto& f : xmls) parsePlacementXml(std::string(g_overrideDir) + f, f.c_str(), fresh);
            if (!fresh.empty()) mergePlacement(fresh);
        }
    }
    return 0;
}
// ========================================================================================

static uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    InterlockedIncrement(&g_regTotal);

    if (asName)
    {
        // Outside the lock, deliberately: scanFinish takes g_cs to publish its results, so a
        // wait with the lock held would deadlock. This is the only place the game's own thread
        // ever waits on us, and it only ever waits once.
        // Bounded, and skipped entirely when the plugin is off: if the scan thread never got
        // started (a fault in Setup disables the plugin but leaves this hook live) an infinite
        // wait here would hang the game on a plugin that is already doing nothing.
        //
        // How long this waits IS the plugin's cost to startup, and it is the only number that
        // says whether moving the scan off DllMain bought anything: the scan's own duration does
        // not, because most of it overlaps with the game starting. Logged once.
        if (!g_off && g_scanDone) {
            ULONGLONG w0 = GetTickCount64();
            DWORD r = WaitForSingleObject(g_scanDone, 300000);
            ULONGLONG ms = GetTickCount64() - w0;
            if (!InterlockedExchange(&g_waitLogged, 1)) {
                if (r == WAIT_TIMEOUT)
                    LOG_WARN(LogCategory::Core, "File scan still running after 5 minutes; registering partial results");
                else if (ms >= 100)
                    LOG_INFO(LogCategory::Core, "Game ready for files %.1fs before scan finished; waited", ms / 1000.0);
                else
                    LOG_INFO(LogCategory::Core, "Scan finished before game needed it (no startup delay)");
            }
        }
        EnterCriticalSection(&g_cs);

        // capture the flag values a real streamed call uses
        if (!g_captured) { g_b1 = b1; g_b2 = b2; g_captured = true; LOG_DEBUG(LogCategory::Core, "Captured streaming flags: b1=%d b2=%d", (int)b1, (int)b2); }

        // once the stream system is live (first call), register our files as base slot overrides.
        // o_regRaw is the trampoline (original), so these calls do NOT re-enter this hook.
        if (!g_off && !g_didRegister)
        {
            g_didRegister = true;
            // the pool must exist by now (this very call registers into it); if it looks wrong,
            // the manager pattern matched the wrong code — better no re-assert than a wild write
            if (g_mgr && (!g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)) {
                LOG_WARN(LogCategory::Core, "Streaming pool looks invalid (entries=%p num=%d); re-assert disabled", (void*)g_mgr->entries, g_mgr->numEntries);
                g_mgr = nullptr;
            }
            int direct = 0, takeovers = 0, waiting = 0, rejected = 0, aliased = 0, shown = 0;
            for (auto& ov : g_ovs)
            {
                uint32_t id = 0xFFFFFFFF;
                // named in _inflight.txt for the duration of the call; if the game dies in
                // there, the next launch quarantines this key and boots without it
                InterlockedExchange(&g_journalHot, 1);
                journalOne(ov.slot);
                o_regRaw(&id, ov.gfile ? ov.gfile : ov.file, g_b1, ov.slot, g_b2);
                ov.id = id;
                if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
                    ov.handle = g_mgr->entries[id].handle;
                int occupied = OCCUPIED_FAILED;
                if (id != 0xFFFFFFFF) {
                    ++direct;
                    // A claim that SUCCEEDS is not proof we took the slot over. For a name
                    // the game already owns, registerRawStreamingFile can mint a brand new
                    // index and hand that back, while the store keeps resolving the name to
                    // its original one. The handle then lands in a slot nothing looks up and
                    // the log reads perfectly while nothing changes in game (seen on .ycd:
                    // seven scattered vanilla dictionaries came back with seven CONSECUTIVE
                    // ids, which existing indices could never be). So ask the store what the
                    // name really resolves to, and pin that entry as well.
                    int why = SLOT_OK;
                    uint32_t tgt = targetStreamingId(ov.slot, &why);
                    noteSlotWhy(ov.slot, why);
                    if (validStreamingId(tgt) && tgt != id) { ov.altId = tgt; ++aliased; }
                }
                else {
                    occupied = recoverOccupiedSlot(ov);
                    if (occupied == OCCUPIED_ATTACHED) ++takeovers;
                    else if (occupied == OCCUPIED_WAITING) ++waiting;
                    else ++rejected;
                }
                if (g_minLogLevel == LogLevel::Debug || ++shown <= 10) {
                    if (id != 0xFFFFFFFF && ov.altId != 0xFFFFFFFF)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x; store resolves to id=%u, pinning both)", ov.slot, rel(ov.file), id, ov.handle, ov.altId);
                    else if (id != 0xFFFFFFFF)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x)", ov.slot, rel(ov.file), id, ov.handle);
                    else if (occupied == OCCUPIED_ATTACHED)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-TAKEOVER: %s <- tex_overrides/%s (id=%u raw handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
                    else if (occupied == OCCUPIED_WAITING)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-WAIT: %s <- tex_overrides/%s (raw handle=%08x; target not present yet)", ov.slot, rel(ov.file), ov.handle);
                    else
                        LOG_ERROR(LogCategory::Claim, "OVERRIDE-FAILED: %s <- tex_overrides/%s (registration rejected; no usable raw entry)", ov.slot, rel(ov.file));
                }
            }
            InterlockedExchange(&g_journalHot, 0);
            DeleteFileA(g_inflightPath);   // whole loop survived; nothing to quarantine
            if (g_ovs.size() > 10 && g_minLogLevel != LogLevel::Debug) {
                LOG_INFO(LogCategory::Claim, "  ...and %zu more override(s) registered directly", g_ovs.size() - 10);
            }
            LOG_INFO(LogCategory::Claim, "Claimed %d base-slot override(s): %d direct, %d occupied-slot takeover, %d waiting for target, %d rejected",
                     direct + takeovers + waiting, direct, takeovers, waiting, rejected);
            if (aliased) LOG_INFO(LogCategory::Claim, "%d of those named a slot the game already owned under a different id; pinned both", aliased);

            // Per type summary
            { std::unordered_map<std::string, int> byExt, aliasByExt, deadByExt;
              for (auto& ov : g_ovs) {
                  const char* dot = strrchr(ov.slot, '.');
                  std::string ext = dot && dot[1] ? lower(dot + 1) : std::string("(none)");
                  ++byExt[ext];
                  if (ov.altId != 0xFFFFFFFF) ++aliasByExt[ext];
                  if (ov.id == 0xFFFFFFFF && ov.altId == 0xFFFFFFFF) ++deadByExt[ext];
              }
              for (auto& kv : byExt)
                  LOG_INFO(LogCategory::Claim, "  .%-4s %4d file(s), %d also pinned under name's own id, %d with no slot at all",
                           kv.first.c_str(), kv.second, aliasByExt[kv.first], deadByExt[kv.first]); }

            // Read the pool back. A claim can report success and leave the entry pointing at the
            // game's own file, which every other line in this log would call healthy.
            if (g_mgr && g_mgr->entries) {
                int pointing = 0, elsewhere = 0, nohandle = 0;
                for (auto& ov : g_ovs) {
                    if (!ov.handle) { ++nohandle; continue; }
                    bool any = false, all = true;
                    for (uint32_t which : { ov.id, ov.altId }) {
                        if (which >= (uint32_t)g_mgr->numEntries) continue;
                        any = true;
                        if (g_mgr->entries[which].handle != ov.handle) all = false;
                    }
                    if (!any) { ++nohandle; continue; }
                    if (all) ++pointing;
                    else {
                        ++elsewhere;
                        LOG_WARN(LogCategory::Verify, "  NOT OURS YET: %s (id=%u alt=%u ours=%08x, pool holds %08x)", ov.slot, ov.id, ov.altId,
                                 ov.handle, g_mgr->entries[ov.id < (uint32_t)g_mgr->numEntries ? ov.id : 0].handle);
                    }
                }
                LOG_INFO(LogCategory::Verify, "Status: %d slot(s) pointing to user files, %d still point to game files (re-asserted each second), %d unassigned",
                         pointing, elsewhere, nohandle);

                // Owning the slot is only half of it. If the game already has the asset in memory
                // it will not read the handle again, so the swap is invisible no matter how
                // correct every line above is. Count how many of ours are already resident.
                { int resident = 0, pending = 0, cold = 0, pinned = 0;
                  for (auto& ov : g_ovs) {
                      if (ov.id >= (uint32_t)g_mgr->numEntries) continue;
                      uint32_t f = g_mgr->entries[ov.id].flags;
                      if ((f & 3) == 1) ++resident; else if ((f & 3) == 0) ++cold; else ++pending;
                      if ((f >> 16) & 1) ++pinned;
                      if ((f & 3) == 1)
                          LOG_WARN(LogCategory::Verify, "  ALREADY IN MEMORY: %s (status=%s, strflags=%04x, deps=%u) — game will reload when re-equipped",
                                   ov.slot, strStatusText(f), (unsigned)(f >> 16), (unsigned)((f >> 2) & 0x3FFF));
                  }
                  LOG_INFO(LogCategory::Verify, "Residency at claim time: %d resident in memory, %d loading, %d cold, %d persistent",
                           resident, pending, cold, pinned); }
            }
            if (g_mgr) LOG_DEBUG(LogCategory::Verify, "Streaming pool: entries=%p numEntries=%d", (void*)g_mgr->entries, g_mgr->numEntries);
            InterlockedExchange(&g_idsReady, 1);
        }

        // MAP: one line per distinct thing the server streams. This is the discovery channel:
        // names we REFUSE have to appear too, or a refused collection reads exactly like one the
        // server never streamed, and a user's log is the only way we ever learn about a naming
        // family the gate does not know yet. That is how the folder whitelist died in 0.8.5.
        //
        // Two axes, and they are not the same question: classifyCollection says WHAT the thing is,
        // the reach tag says whether we would ever touch it. Reach is worked out from the
        // COLLECTION, never from isAllowedKey on whichever file streamed first, which mislabelled
        // any collection whose first file happened to be oddly named. It has three states, because
        // for a collection that is neither freemode/a_c_ nor blocked the answer really does depend
        // on the file names inside it.
        //
        // Root files are not collections, so they get their own line instead of going through
        // collectionOf, which returns the whole filename for them.
        std::string keyLower = lower(asName);
        bool rootFile = keyLower.find('/') == std::string::npos;
        std::string coll = rootFile ? keyLower : collectionOf(keyLower);

        if (g_collSeen.insert(coll).second) {
            if (rootFile) {
                LOG_INFO(LogCategory::Collection, "Server file:       %-40s [%s]", coll.c_str(),
                         isAllowedKey(keyLower) ? "overridable, put yours in tex_overrides/"
                                                : "OTHER - never touched");
            } else if (isPedCollection(coll)) {
                LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [overridable] -> tex_overrides/%s/",
                         coll.c_str(), classifyCollection(coll), coll.c_str());
            } else if (isBlockedCollection(coll)) {
                LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [OTHER - never touched]",
                         coll.c_str(), classifyCollection(coll));
            } else {
                LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [depends on the file names inside] -> tex_overrides/%s/",
                         coll.c_str(), classifyCollection(coll), coll.c_str());
            }
            // the old cap stopped logging at 500 and said nothing, so the tail read like a server
            // that streams nothing at all. Say it out loud instead.
            if (g_collSeen.size() == 500)
                LOG_WARN(LogCategory::Collection, "500 distinct names logged, the rest will not be listed");
        }

        LeaveCriticalSection(&g_cs);
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
            EnterCriticalSection(&g_cs);
            auto it = g_bySlot.find(key);
            if (it != g_bySlot.end()) redirect = it->second;   // value is a leaked strdup — stable after release
            LeaveCriticalSection(&g_cs);
            if (redirect)
            {
                long n = InterlockedIncrement(&g_redirects);
                if (n <= 100)       LOG_INFO(LogCategory::Claim, "REDIRECT %s -> tex_overrides/%s", asName, rel(redirect));
                else if (n == 101)  LOG_INFO(LogCategory::Claim, "REDIRECT: 100 logged, the rest are counted but not listed");
                return o_regRaw(fileId, redirect, b1, asName, b2);
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
    if (sl == 0 || sl >= MAX_PATH) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot locate System32"); return; }
    strcat_s(dxPath, "\\dxgi.dll");
    HMODULE dx = LoadLibraryA(dxPath);
    if (!dx) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot load %s (err %lu)", dxPath, GetLastError()); return; }
    auto createFactory = (CreateFactory_t)GetProcAddress(dx, "CreateDXGIFactory1");
    if (!createFactory) { LOG_WARN(LogCategory::Audit, "Texture budget: dxgi.dll has no CreateDXGIFactory1"); return; }

    IDXGIFactory1* f = nullptr;
    HRESULT hr = createFactory(__uuidof(IDXGIFactory1), (void**)&f);
    if (FAILED(hr) || !f) { LOG_WARN(LogCategory::Audit, "Texture budget: CreateDXGIFactory1 failed (hr 0x%08lX)", (unsigned long)hr); return; }
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
    if (!best) LOG_WARN(LogCategory::Audit, "Texture budget: DXGI listed no hardware adapter");
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
    if (g_vramTable[3] == g_budget) return;   // our value is standing
    uint64_t old = g_vramTable[3];
    for (int i = 0; i < 80; i += 4) {         // same rows, same ratios as Cfx's own writer
        g_vramTable[i + 3] = g_budget;
        g_vramTable[i + 2] = g_budget;
        g_vramTable[i + 1] = (uint64_t)(g_budget / 1.5);
        g_vramTable[i]     = g_budget / 2;
    }
    if (++g_budgetWrites <= 10)
        LOG_DEBUG(LogCategory::Audit, "Texture budget re-asserted: %.1f -> %.1f GB%s", old / 1073741824.0, g_budget / 1073741824.0,
                  g_budgetWrites == 1 ? "" : " (re-asserted; settings screen rewrote it)");
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
            LOG_WARN(LogCategory::Audit, "Texture budget: Requested %.1f GB exceeds card's %.1f GB VRAM; clamped",
                     g_budget / 1073741824.0, g_vramTotal / 1073741824.0);
            g_budget = g_vramTotal;
        }
        if (g_budget <= g_budgetCurr) {    // lowering it would only make texture loss worse
            LOG_INFO(LogCategory::Audit, "Texture budget: Requested %.1f GB is <= game's %.1f GB; left unchanged",
                     g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
            g_budget = 0;
        }
        else LOG_INFO(LogCategory::Audit, "Texture budget: _budget.txt requested %.1f GB (up from %.1f GB)",
                      g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
        return;
    }
    g_budget = autoBudget(g_budgetCurr);
    if (g_budget)
        LOG_INFO(LogCategory::Audit, "Texture budget: Sized to this PC — %.1f GB, up from %.1f GB (card: %.1f GB, Windows offering: %.1f GB)",
                 g_budget / 1073741824.0, g_budgetCurr / 1073741824.0,
                 g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
    else if (!g_vramTotal && !g_vramBudget)
        LOG_WARN(LogCategory::Audit, "Texture budget: Could not query card memory; left at game default (%.1f GB)", g_budgetCurr / 1073741824.0);
    else
        LOG_INFO(LogCategory::Audit, "Texture budget: Game default %.1f GB is optimal for this GPU (card: %.1f GB, Windows offering: %.1f GB)",
                 g_budgetCurr / 1073741824.0, g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
}
static void decideBudget()
{
    __try { decideBudgetImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_budget = 0;
        LOG_WARN(LogCategory::Audit, "Texture budget: Fault reading card memory (code %08X); left at game default",
                 (unsigned)GetExceptionCode());
    }
}

static void budgetBeat()
{
    if (!g_budget || g_budgetFault || !g_vramTable) return;
    __try { budgetBeatImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_budgetFault, 1);
        LOG_ERROR(LogCategory::Audit, "Texture budget: Fault writing budget table; raised budget disabled for session");
    }
}

// What the pack costs the streamer once every file in it is resident, and how that compares
// with what the game is handing textures right now. Called from scanFinish.
static void costReport()
{
    if (g_costVirt + g_costPhys) {
        LOG_INFO(LogCategory::Audit, "Pack cost when fully loaded: %.1f MB texture memory + %.1f MB virtual memory",
                 g_costPhys / 1048576.0, g_costVirt / 1048576.0);
        std::sort(g_costBig.rbegin(), g_costBig.rend());
        for (const auto& item : g_costBig)
            LOG_WARN(LogCategory::Audit, "  HEAVY %6.1f MB  %s (likely 4K or uncompressed; shrink to fight texture loss)", item.first / 1048576.0, item.second.c_str());
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
            LOG_INFO(LogCategory::Audit, "  Texture pool: Game allocation is %.1f GB. %s",
                     g_budgetCurr / 1073741824.0,
                     g_costPhys < g_budgetCurr / 2
                       ? "Pack fits with room to spare."
                       : "Pack takes a large share of budget; shrink HEAVY files if texture loss occurs.");
        }
    }
}

// The budget table, the streaming manager and the live-reload functions are all optional, and
// none of them has to exist before the game's entry point runs. Only the hook does. So they are
// found out here on the beat thread. The hook waits on g_scanDone, which is not set until this
// and the file scan are both done, so it never sees half of this filled in.
static void locateRuntimePatterns()
{
    const short PAT_VRAM[] = { 0x4C,0x63,0xC0,0x48,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x14 };
    uint8_t* vram = scanModule(PAT_VRAM, 13);
    uint64_t* table = vram ? (uint64_t*)ripTarget(vram + 6) : nullptr;
    if (table && vramTableSane(table, &g_budgetCurr)) g_vramTable = table;
    else LOG_WARN(LogCategory::Audit, "Texture budget: VRAM table %s; budget adjustment disabled",
                  vram ? "failed sanity check" : "pattern NOT FOUND");

    const short PAT_MGR[] = { 0x74,0x1A,0x8B,0x15,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0x41 };
    if (uint8_t* q = scanModule(PAT_MGR, 16)) {
        g_mgr = (StrMgr*)ripTarget(q + 11);
        LOG_INFO(LogCategory::Core, "Streaming manager @ %p", (void*)g_mgr);
    }
    else LOG_WARN(LogCategory::Core, "Streaming manager pattern NOT FOUND — overrides will register but cannot re-assert");

    const short PAT_GRS[] = { 0x48,0x8B,0xD3,0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x41,0xFF,0x90,-1,0x01,0x00,0x00,0x8B,0xD8,0xE8 };
    if (uint8_t* q = scanModule(PAT_GRS, 19)) {
        if (q[-5] == 0xE8) g_getRawStreamerFn = (GetRawStreamer_t)ripTarget(q - 4);
    }
    const short PAT_GE[] = { 0x0F,0xB7,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x8B,0xD0,0x25,0xFF };
    if (uint8_t* q = scanModule(PAT_GE, 12)) g_rawGetEntryFn = (RawGetEntry_t)(q - 0x14);
    LOG_INFO(LogCategory::Live, "Occupied-slot exports: rawStreamer=%s getEntry=%s",
             g_getRawStreamerFn ? "ok" : "MISSING", g_rawGetEntryFn ? "ok" : "MISSING");
}

// _budget.txt and the placement .xml files are user files too, so they are read out here with
// the rest of them instead of under the loader lock. Two functions because they sit on opposite
// sides of the scan: the budget decision needs the file before the scan starts (see
// backgroundStartup), and the placement parse is not needed until the first beat.
static void readBudgetFile()
{
    char bp[MAX_PATH]; _snprintf_s(bp, _TRUNCATE, "%s_budget.txt", g_overrideDir);
    FILE* bf = nullptr;
    if (!fopen_s(&bf, bp, "rb") && bf) {
        char buf[32] = {}; fread(buf, 1, 31, bf); fclose(bf);
        double gb = atof(buf);
        if (gb >= 1.0 && gb <= 48.0) g_budgetWant = gb;
        else {
            g_budgetWant = 0.0;
            LOG_WARN(LogCategory::Audit, "Texture budget: _budget.txt does not hold a valid number (1-48 GB); left at game default");
        }
    }
}
static void loadPlacementFiles()
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(g_overrideDir) + "*.xml").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { parsePlacementXml(std::string(g_overrideDir) + fd.cFileName, fd.cFileName, g_pl); }
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (auto& pc : g_pl)
        LOG_INFO(LogCategory::Tattoo, "Loaded placement file: %s (%zu presets for %s)", pc.src.c_str(), pc.presets.size(), pc.name.c_str());
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
    InitializeCriticalSection(&g_logCs);
    g_logCsInit = true;
    InitializeCriticalSection(&g_cs);   // must exist before the hook can fire

    // check for verbose / debug logging triggers
    {
        std::string verb = std::string(g_overrideDir) + "_verbose.txt";
        std::string dbg  = std::string(g_overrideDir) + "_debug.txt";
        if (GetFileAttributesA(verb.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(dbg.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g_minLogLevel = LogLevel::Debug;
        }
    }

    // fresh log every launch, but keep one previous generation: after a crash the next launch
    // used to destroy the exact log that showed what the crashed session was doing
    { char oldLog[MAX_PATH + 8];
      _snprintf_s(oldLog, _TRUNCATE, "%s.old", g_logPath);
      if (!MoveFileExA(g_logPath, oldLog, MOVEFILE_REPLACE_EXISTING))
          DeleteFileA(g_logPath);   // rotation blocked (file held open): keep "fresh log" true
    }
    { time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
      char d[32]; strftime(d, sizeof d, "%Y-%m-%d", &tm);
      LOG_INFO(LogCategory::Core, "texoverride " TEXOVERRIDE_VERSION " initializing (%s, build %d)", d, runningGameBuild()); }
    // The hook waits on this. Every user-file read and every optional pattern scan now happens
    // on the beat thread (backgroundStartup), which sets it when it is done.
    g_scanDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    resolveOccupiedSlotExports();
    LOG_INFO(LogCategory::Core, "Occupied-slot exports: manager=%s module=%s rawEntries=%s",
         g_getStreamingManagerFn ? "ok" : "MISSING",
         g_getStreamingModuleFn ? "ok" : "MISSING",
         g_getRawEntriesFn ? "ok" : "MISSING");

    const short PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT, sizeof PAT / sizeof *PAT);
    if (!p) { LOG_ERROR(LogCategory::Core, "Streaming hook pattern NOT FOUND; plugin disabled for this session"); }
    else {
        void* target = (void*)(p - 0x25);
        LOG_INFO(LogCategory::Core, "registerRawStreamingFile @ %p", target);
        // Every one of these was logged and then ignored. A failed MH_CreateHook leaves o_regRaw
        // null while the hook is live, so the first stream call dereferences it and the game dies
        // on a plugin that already knew it had failed. Bail out instead, and unwind what we own.
        MH_STATUS s = MH_Initialize();
        LOG_INFO(LogCategory::Core, "MH_Initialize: %s", MH_StatusToString(s));
        bool ownMh = (s == MH_OK);
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
            g_off = true;
            LOG_ERROR(LogCategory::Core, "MinHook would not start; plugin disabled for this session");
        }
        else {
            s = MH_CreateHook(target, (void*)&h_regRaw, (void**)&o_regRaw);
            LOG_INFO(LogCategory::Core, "MH_CreateHook: %s", MH_StatusToString(s));
            if (s != MH_OK) {
                if (ownMh) MH_Uninitialize();
                g_off = true;
                LOG_ERROR(LogCategory::Core, "Streaming hook could not be created; plugin disabled for this session");
            }
            else {
                // this hook, not MH_ALL_HOOKS: nothing else in the process is ours to enable
                s = MH_EnableHook(target);
                LOG_INFO(LogCategory::Core, "MH_EnableHook: %s", MH_StatusToString(s));
                if (s != MH_OK) {
                    LOG_WARN(LogCategory::Core, "MH_RemoveHook after enable failure: %s", MH_StatusToString(MH_RemoveHook(target)));
                    if (ownMh) MH_Uninitialize();
                    g_off = true;
                    LOG_ERROR(LogCategory::Core, "Streaming hook could not be enabled; plugin disabled for this session");
                }
                else LOG_INFO(LogCategory::Core, g_off ? "Hooked, disabled" : "LIVE — will register base overrides on first stream call");
            }
        }
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
    if (latest.empty()) { LOG_DEBUG(LogCategory::Update, "Update check: could not reach GitHub (offline?)"); return 0; }
    const char* lv = (latest[0] == 'v' || latest[0] == 'V') ? latest.c_str() + 1 : latest.c_str();

    if (verCmp(lv, TEXOVERRIDE_VERSION) > 0) {
        LOG_INFO(LogCategory::Update, "Update available: %s (current: " TEXOVERRIDE_VERSION ")", latest.c_str());
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
    else LOG_INFO(LogCategory::Update, "Plugin is up to date (latest %s)", latest.c_str());
    return 0;
}

// Order matters. The budget is decided before the scan, not after: on a big pack the scan runs
// for a long while, and for all of it the game would be stuck at its own texture ceiling. It
// cannot go any earlier than this, because decideBudget needs the table locateRuntimePatterns
// finds and the number _budget.txt holds.
static void backgroundStartup()
{
    crashSaverStartup();
    g_crashSaverRan = true;
    locateRuntimePatterns();
    readBudgetFile();
    decideBudget();   // DXGI only works out here, after DllMain has returned
    walkDir(std::string(g_overrideDir), "", g_cands);
    LOG_INFO(LogCategory::Scan, "Discovered %zu candidate file(s); reading headers...", g_cands.size());
    scanFinish();
    loadPlacementFiles();
}

static bool backgroundStartupCppSafe()
{
    try { backgroundStartup(); return true; }
    catch (const std::exception& e) { LOG_ERROR(LogCategory::Core, "Background startup failed: %s", e.what()); }
    catch (...) { LOG_ERROR(LogCategory::Core, "Background startup failed with an unknown C++ exception"); }
    return false;
}

// SEH so a fault in background startup cannot leave the hook waiting on an event nobody will
// ever set. (No C++ objects in this frame, which is what makes __try legal; they live in the
// helper. Own function: SEH inside BeatLoop's infinite loop confuses MSVC's return analysis.)
// This now covers crashSaverStartup as well, and that is intended: a fault in there can leave
// the journal half processed, but g_scanDone is still set and the game still boots, which beats
// a hook waiting five minutes on a plugin that is already broken. The journal file is left where
// it is, so the next launch gets another go at it.
static void scanFinishSafe()
{
    bool ok = false;
    __try { ok = backgroundStartupCppSafe(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR(LogCategory::Core, "FAULT during background startup (code %08X); carrying on with available assets", GetExceptionCode());
    }
    if (!ok) LOG_WARN(LogCategory::Core, "Background startup did not finish; only pre-loaded assets active this session");
    if (g_scanDone) SetEvent(g_scanDone);
}

static DWORD WINAPI BeatLoop(LPVOID)
{
    int beatOurs = 0, beatTheirs = 0, beatLoaded = 0;   // ours / re-taken / resident right now
    long prevReclaims = 0, prevRedirects = 0, prevLateBinds = 0;
    int prevTheirs = 0;
    scanFinishSafe();   // all user-file reads, optional scans and the budget decision, off the loader lock
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            // once streaming is live, start the live-reload watcher — before that there is
            // nothing a change could apply to anyway
            if (!g_watcherStarted && !g_off && g_idsReady) {
                HANDLE w = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
                if (w) { g_watcherStarted = true; CloseHandle(w); }
                else LOG_WARN(LogCategory::Live, "Live reload: watcher thread could not start (err %lu), retrying next tick", GetLastError());
            }
            // re-assert: DLC mounts and FiveM's loader re-point claimed slots after us; whoever
            // writes the handle last wins, so write ours back. Same mechanism Cfx's own override
            // path uses (LoadStreamingFile.cpp writes Entries[].handle directly).
            // Lock held: the watcher can append to g_ovs (vector may reallocate) and swap g_pl
            // entries under us.
            if (!g_off && g_idsReady && g_mgr && g_mgr->entries) {
                EnterCriticalSection(&g_cs);
                beatOurs = beatTheirs = beatLoaded = 0;
                for (auto& ov : g_ovs) {
                    if (ov.handle && ov.id == 0xFFFFFFFF) {
                        uint32_t appeared = targetStreamingId(ov.slot);
                        if (validStreamingId(appeared)) {
                            ov.id = appeared;
                            if (++g_lateBinds <= 60)
                                LOG_INFO(LogCategory::Claim, "LATE-BIND: %s (target id=%u raw handle=%08x)", ov.slot, ov.id, ov.handle);
                        }
                    }
                    if (!ov.handle) continue;
                    // Both indices matter: the one the claim landed on, and the one the store
                    // resolves the NAME to. They are the same slot in the ordinary case.
                    for (uint32_t which : { ov.id, ov.altId }) {
                        if (which >= (uint32_t)g_mgr->numEntries) continue;
                        StrEntry& e = g_mgr->entries[which];
                        if ((e.flags & 3) == 1) ++beatLoaded;
                        if (e.handle == ov.handle) { ++beatOurs; continue; }
                        ++beatTheirs;
                        if ((e.flags & 3) >= 2) { ++g_deferred; continue; }   // being requested/loaded right now; retry next tick
                        uint32_t old = e.handle;
                        e.handle = ov.handle;
                        if (++g_reclaims <= 60) LOG_INFO(LogCategory::Claim, "RECLAIM: %s (id=%u, %08x -> %08x)", ov.slot, which, old, ov.handle);
                    }
                }
                LeaveCriticalSection(&g_cs);
            }
            if (!g_off) {
                EnterCriticalSection(&g_cs);
                placementBeatSafe();   // apply/re-assert tattoo placement edits
                LeaveCriticalSection(&g_cs);
                budgetBeat();          // re-assert the raised texture budget (aligned data writes)
            }
        }

        long dReclaims = g_reclaims - prevReclaims;
        long dRedirects = (long)g_redirects - prevRedirects;
        long dLateBinds = g_lateBinds - prevLateBinds;
        bool changed = (dReclaims > 0 || dLateBinds > 0 || beatTheirs != prevTheirs);
        prevReclaims = g_reclaims;
        prevRedirects = (long)g_redirects;
        prevLateBinds = g_lateBinds;
        prevTheirs = beatTheirs;

        if (changed) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %ld reclaims (+%ld), %ld redirects (+%ld)",
                     beat, beatOurs, beatTheirs, g_reclaims, dReclaims, (long)g_redirects, dRedirects);
        } else if (beat % (g_minLogLevel == LogLevel::Debug ? 4 : 20) == 0 || beat == 1) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %d in memory, %ld redirects",
                     beat, beatOurs, beatTheirs, beatLoaded, (long)g_redirects);
        }
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
        LOG_ERROR(LogCategory::Core, "FAULT during startup (code %08X) — plugin disabled for this session", GetExceptionCode());
        g_off = true;
        return false;
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h; DisableThreadLibraryCalls(h);
        if (SetupSafe()) {   // synchronous: must finish before the game's entry point runs (see Setup)
            // If the beat thread never starts, scanFinishSafe never runs, g_scanDone is never set
            // and the first stream call sits in its 5 minute wait for nothing. Signal it and stop.
            HANDLE beat = CreateThread(nullptr, 0, BeatLoop, nullptr, 0, nullptr);
            if (beat) {
                CloseHandle(beat);
                HANDLE upd = CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
                if (upd) CloseHandle(upd);
                else LOG_WARN(LogCategory::Update, "Update check thread could not start (err %lu)", GetLastError());
            }
            else {
                DWORD e = GetLastError();
                g_off = true;
                if (g_scanDone) SetEvent(g_scanDone);
                LOG_ERROR(LogCategory::Core, "Background beat thread could not start (err %lu); plugin disabled", e);
            }
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // orderly exit = no crash: the live-change journal must not quarantine anything.
        // only after crashSaverStartup has processed any leftover journal: if Setup faulted
        // before that point, deleting here would erase the previous crash's evidence.
        //
        // g_journalHot is the important half. "A real crash never reaches this line" was an
        // ASSUMPTION, and it is wrong often enough to matter: FiveM catches the fault, shows its
        // crash dialog, uploads a report and then tears the process down, and that path can run
        // detach for every loaded module. Deleting the journal there erases the name of the file
        // that just killed the game, so the next launch has nothing to quarantine and dies in the
        // same place forever. While the startup loop holds a key in the journal, leave it alone.
        if (g_crashSaverRan && g_inflightPath[0] && !InterlockedCompareExchange(&g_journalHot, 0, 0))
            DeleteFileA(g_inflightPath);
    }
    return TRUE;
}
