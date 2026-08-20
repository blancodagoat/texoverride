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
#include <dxgi.h>
// no dxgi import lib: a static import resolves against the APPLICATION directory first, and a
// ReShade/ENB dxgi.dll in the FiveM folder then breaks the load of this whole plugin with
// "Couldn't load texoverride.asi". dedicatedVram() loads the real one from System32 instead.
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
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
#include <algorithm>
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
static uint64_t g_costVirt, g_costPhys;
static std::vector<std::pair<uint64_t, std::string>> g_costBig;   // files >= 8 MB in memory

static void logf(const char* fmt, ...);

// One crash gate for every load path (startup scan, live new file, live overwrite): refuse
// anything with more than 32 MB in EITHER resource segment. CONFIRMED cause of the
// summer-maine-steak crash twice over: five files with 64 MB graphics segments (removal test),
// then a player crashing on mesh files whose 77+ MB sat in the virtual segment while graphics
// stayed under the line — same crash address both times. 32 MB per segment is verified fine.
// When the header cannot be read (locked by antivirus, mid-copy) the on-disk size stands in,
// so the gate never fails open.
static bool tooBigToStream(const char* path, const char* key, bool quiet)
{
    uint64_t cv = 0, cp = 0;
    if (!rscCost(path, &cv, &cp)) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
            if (!quiet) logf("UNREADABLE %s — cannot open it, so it is not loaded this launch", key);
            return true;
        }
        cp = (((uint64_t)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;   // on-disk stand-in
    }
    uint64_t worst = (cv > cp) ? cv : cp;
    if (worst <= (32ull << 20)) return false;
    if (!quiet) logf("TOO BIG  %s — %.1f MB of %s data; files this big are the confirmed cause of game crashes, so it is NOT loaded. Shrink it (CodeWalker, Tools, Shrink Textures).",
                     key, worst / 1048576.0, (cv > cp) ? "mesh" : "texture");
    return true;
}

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

#define TEXOVERRIDE_VERSION "0.6.3"

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
                if (g_quarantine.count(slotStr)) continue;   // crash saver; already logged loudly
                std::string full = fwd(base + childRel);
                if (tooBigToStream(full.c_str(), slotStr.c_str(), false)) continue;
                const char* slot = _strdup(slotStr.c_str());
                const char* file = _strdup(full.c_str());                      // our absolute path
                g_ovs.push_back({ slot, file });
                g_bySlot[slot] = file;
                ++n;
                uint64_t cv = 0, cp = 0;
                if (rscCost(file, &cv, &cp)) {
                    g_costVirt += cv; g_costPhys += cp;
                    if (cv + cp >= (8u << 20)) g_costBig.push_back({ cv + cp, slotStr });
                }
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

struct LiveOp { int kind; Ov ov; uint32_t handle; };            // kind: 0 = register, 1 = re-stat
static std::vector<LiveOp> g_opQ;                               // guarded by g_cs
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
static bool liveRegister(Ov& ov)
{
    __try {
        uint32_t id = 0xFFFFFFFF;
        o_regRaw(&id, ov.file, g_b1, ov.slot, g_b2);
        ov.id = id;
        if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
            ov.handle = g_mgr->entries[id].handle;
        return ov.id != 0xFFFFFFFF && ov.handle != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- main-thread pump: IAT shim on the game exe's PeekMessageW import ----
typedef BOOL (WINAPI* PeekMsg_t)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMsg_t g_origPeek = nullptr;
static DWORD g_pumpTid = 0;

static void drainOps()   // runs on the game's main thread
{
    EnterCriticalSection(&g_cs);
    std::vector<LiveOp> ops; ops.swap(g_opQ);
    LeaveCriticalSection(&g_cs);
    for (auto& op : ops) {
        if (op.kind == 0) {
            if (liveRegister(op.ov)) {
                EnterCriticalSection(&g_cs);
                g_ovs.push_back(op.ov); g_bySlot[op.ov.slot] = op.ov.file;
                LeaveCriticalSection(&g_cs);
                logf("LIVE-ADD  %s  <-  tex_overrides/%s  (id=%u handle=%08x)", op.ov.slot, rel(op.ov.file), op.ov.id, op.ov.handle);
                uint64_t cv = 0, cp = 0;
                if (rscCost(op.ov.file, &cv, &cp) && cv + cp >= (8u << 20))
                    logf("  HEAVY %.1f MB in memory — likely 4K or uncompressed; shrink it to fight texture loss", (cv + cp) / 1048576.0);
            } else {
                logf("live reload: could not register %s, restart to pick it up", op.ov.slot);
                free((void*)op.ov.slot); free((void*)op.ov.file);
            }
        } else {
            if (g_getRawStreamerFn && g_rawGetEntryFn && rawInvalidate(op.handle))
                logf("LIVE-UPDATE  %s reread from disk; reapply the outfit or tattoo to see it", op.ov.slot);
            else
                logf("live reload: %s changed, could not refresh it, restart to apply", op.ov.slot);
            free((void*)op.ov.slot);
        }
    }
    InterlockedExchange(&g_opsPending, 0);
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
static void journalWrite(const std::vector<LiveOp>& batch)
{
    FILE* f; if (fopen_s(&f, g_inflightPath, "w") != 0 || !f) return;
    for (auto& op : batch) fprintf(f, "%s\n", op.ov.slot);
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
        if (any) logf("CRASH SAVER: the game did not shut down cleanly right after live changes; the files involved are quarantined");
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
            logf("QUARANTINED %s — not loaded; delete _quarantine.txt in tex_overrides to try it again", k.c_str());
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
        logf("placement: %s reloaded from %s (%zu presets%s)", npc.name.c_str(), npc.src.c_str(),
             npc.presets.size(), npc.solved ? ", layout kept" : "");
        if (old) *old = std::move(npc); else g_pl.push_back(std::move(npc));
    }
    LeaveCriticalSection(&g_cs);
    if (g_plFault) logf("placement: NOTE — placement is disabled for this session (earlier fault), edits will apply after a restart");
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

        EnterCriticalSection(&g_cs);
        uint32_t handle = 0; bool known = false;
        for (auto& ov : g_ovs) if (key == ov.slot) { known = true; handle = ov.handle; break; }
        LeaveCriticalSection(&g_cs);

        if (!known) {
            if (!g_origPeek) { logf("live reload: new file %s needs a game restart", key.c_str()); continue; }
            if (tooBigToStream(fwd(full).c_str(), key.c_str(), quiet)) continue;   // crash gate; quiet skips the baseline re-log
            batch.push_back({ 0, { _strdup(key.c_str()), _strdup(fwd(full).c_str()) }, 0 });
        }
        else if (handle && isChanged) {
            if ((handle >> 16) != 0)   // not in the game's own raw streamer; cannot re-stat it
                logf("live reload: %s changed, restart to apply (handle %08x not raw)", key.c_str(), handle);
            else if (!g_origPeek)
                logf("live reload: %s changed, restart to apply", key.c_str());
            else if (tooBigToStream(fwd(full).c_str(), key.c_str(), false))
                // the registered slot still points at this path with the OLD size cached, so the
                // game may read a truncated slice of the new content; make that loud
                logf("live reload: %s grew past the safe size and was NOT re-read; restore the smaller file or restart", key.c_str());
            else
                batch.push_back({ 1, { _strdup(key.c_str()), nullptr }, handle });
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Hand a batch to the game thread: journal first (crash saver), one batch in flight at a time.
static void submitBatch(std::vector<LiveOp>& batch)
{
    // the previous batch drains within a frame; if the game thread stopped pumping (shutdown,
    // hang) give up after 10s instead of wedging the watcher
    for (int i = 0; InterlockedCompareExchange(&g_opsPending, 0, 0) && i < 200; ++i) Sleep(50);
    if (g_opsPending) {
        logf("live reload: game thread is not draining changes, dropping this batch");
        for (auto& op : batch) { free((void*)op.ov.slot); free((void*)op.ov.file); }
        return;
    }
    journalWrite(batch);
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
    if (h == INVALID_HANDLE_VALUE) { logf("live reload: cannot watch tex_overrides (err %lu) — restart to apply changes", GetLastError()); return 0; }

    bool pump = installPump();
    { std::vector<std::string> ignore; std::vector<LiveOp> batch;
      rescanTree(g_overrideDir, "", true, ignore, batch);   // baseline stamps; also catches files added during loading
      if (!batch.empty()) submitBatch(batch); }
    logf("live reload: watching tex_overrides (%s)", pump ? "full: edits and new files" : "edits only: no main-thread pump, new files need a restart");

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
                InterlockedIncrement(&g_redirects);
                if (g_redirects <= 100) logf("REDIRECT  %s  ->  tex_overrides/%s", asName, rel(redirect));
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
// 3 GB x the Extended Texture Budget slider's multiplier, and rewrites it whenever the slider
// moves. "Texture loss" (stuck low detail, black walls, restart needed) is this budget running
// dry — the eviction algorithm inside GTA5.exe only frees memory under pressure (cfx issue
// #3874), so a bigger ceiling means more headroom before the cliff. Data writes only, same
// class as the handle re-assert; re-asserted each beat because the settings screen rewrites it.
// Clamped to the card's real dedicated VRAM: past that, D3D11 demotes textures to system RAM
// and the game stutters hard, which is why this is opt-in and never a silent default.
static uint64_t* g_vramTable = nullptr;
static uint64_t  g_budget = 0;              // requested bytes; 0 = feature off
static volatile LONG g_budgetFault = 0;
static long g_budgetWrites = 0;

static bool vramTableSane(uint64_t* t)      // refuse to write unless it looks like Cfx filled it
{
    __try {
        for (int i = 0; i < 80; i += 4) {
            uint64_t full = t[i + 3];       // rows are half / 1.5th / full / full of the budget
            if (full != t[i + 2] || full < (1ull << 30) || full > (48ull << 30) || t[i] >= full)
                return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static uint64_t dedicatedVram()             // biggest hardware adapter; 0 when unknowable
{
    // dxgi loaded by FULL System32 path: a static import (or a bare-name load) binds to any
    // already-loaded ReShade/ENB proxy dxgi.dll, and a proxy missing CreateDXGIFactory1 used
    // to fail the load of this whole plugin. The full path always gets Windows' own copy.
    typedef HRESULT (WINAPI* CreateFactory_t)(REFIID, void**);
    char dxPath[MAX_PATH + 16];
    UINT sl = GetSystemDirectoryA(dxPath, MAX_PATH);
    if (sl == 0 || sl >= MAX_PATH) return 0;
    strcat_s(dxPath, "\\dxgi.dll");
    HMODULE dx = LoadLibraryA(dxPath);
    if (!dx) return 0;
    auto createFactory = (CreateFactory_t)GetProcAddress(dx, "CreateDXGIFactory1");
    if (!createFactory) return 0;

    uint64_t best = 0; IDXGIFactory1* f = nullptr;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return 0;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > best)
            best = d.DedicatedVideoMemory;
        a->Release();
    }
    f->Release();
    return best;
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
        logf("texture budget: %.1f -> %.1f GB%s", old / 1073741824.0, g_budget / 1073741824.0,
             g_budgetWrites == 1 ? "" : " (re-asserted; the settings screen rewrote it)");
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
    crashSaverStartup();   // quarantine anything a crash left in the journal, before the scan
    g_crashSaverRan = true;
    int n = scanDir(std::string(g_overrideDir), "");
    logf("loaded %d override(s); mode %s", n, g_off ? "OFF" : "ON");
    { std::unordered_map<std::string, int> per;   // per-collection tally, the first thing to check in a report
      for (auto& ov : g_ovs) ++per[collectionOf(ov.slot)];
      for (auto& kv : per) logf("  %-40s %d file(s)", kv.first.c_str(), kv.second); }
    if (g_costVirt + g_costPhys) {
        logf("pack cost when fully loaded: %.1f MB of texture memory + %.1f MB other. Vanilla clothing files stay well under 2 MB each; a heavy pack feeds the \"stuck on low detail / textures gone\" bug on busy servers.",
             g_costPhys / 1048576.0, g_costVirt / 1048576.0);
        std::sort(g_costBig.rbegin(), g_costBig.rend());
        for (size_t i = 0; i < g_costBig.size() && i < 20; ++i)
            logf("  HEAVY %6.1f MB  %s — likely 4K or uncompressed; shrink it to fight texture loss", g_costBig[i].first / 1048576.0, g_costBig[i].second.c_str());
        if (g_costBig.size() > 20) logf("  ...and %zu more file(s) over 8 MB", g_costBig.size() - 20);
    }

    // tattoo placement files: *.xml at the root of tex_overrides (edited overlays.xml copies)
    { WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((std::string(g_overrideDir) + "*.xml").c_str(), &fd);
      if (h != INVALID_HANDLE_VALUE) {
          do { parsePlacementXml(std::string(g_overrideDir) + fd.cFileName, fd.cFileName, g_pl); } while (FindNextFileA(h, &fd));
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

    // live-reload plumbing, both from Cfx's published patterns (LoadStreamingFile.cpp):
    //   getRawStreamer          — the game's pgRawStreamer instance
    //   pgRawStreamer::GetEntry — re-stats an entry when its timestamp is 0
    { const short PAT_GRS[] = { 0x48,0x8B,0xD3,0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x41,0xFF,0x90,-1,0x01,0x00,0x00,0x8B,0xD8,0xE8 };
      if (uint8_t* q = scanModule(PAT_GRS, 19)) { if (q[-5] == 0xE8) g_getRawStreamerFn = (GetRawStreamer_t)ripTarget(q - 4); }
      const short PAT_GE[] = { 0x0F,0xB7,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x8B,0xD0,0x25,0xFF };
      if (uint8_t* q = scanModule(PAT_GE, 12)) g_rawGetEntryFn = (RawGetEntry_t)(q - 0x14);
      logf("live reload: rawStreamer=%s getEntry=%s",
           g_getRawStreamerFn ? "ok" : "MISSING", g_rawGetEntryFn ? "ok" : "MISSING"); }

    // opt-in texture budget raise: _budget.txt in tex_overrides holds a number of GB
    { char bp[MAX_PATH]; _snprintf_s(bp, _TRUNCATE, "%s_budget.txt", g_overrideDir);
      FILE* bf = nullptr;
      if (!fopen_s(&bf, bp, "rb") && bf) {
          char buf[32] = {}; fread(buf, 1, 31, bf); fclose(bf);
          double gb = atof(buf);
          if (gb >= 1.0 && gb <= 48.0) g_budget = (uint64_t)(gb * 1073741824.0);
          else logf("budget: _budget.txt must hold a number of GB between 1 and 48 — ignored");
      } }
    if (g_budget) {
        uint64_t vram = dedicatedVram();
        if (vram && g_budget > vram) {
            logf("budget: your card has %.1f GB of VRAM; clamping the requested %.1f GB to that",
                 vram / 1073741824.0, g_budget / 1073741824.0);
            g_budget = vram;
        }
        // the per-quality VRAM budget table, via the pattern Cfx resolves it with
        // (PatchExtendedBudgeting.cpp: "4C 63 C0 48 8D 05 ? ? ? ? 48 8D 14", address at +6)
        const short PAT_VRAM[] = { 0x4C,0x63,0xC0,0x48,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x14 };
        uint8_t* q = scanModule(PAT_VRAM, 13);
        uint64_t* t = q ? (uint64_t*)ripTarget(q + 6) : nullptr;
        if (t && vramTableSane(t)) {
            g_vramTable = t;
            logf("budget: raising the texture budget to %.1f GB (table @ %p, was %.1f GB)",
                 g_budget / 1073741824.0, (void*)t, t[3] / 1073741824.0);
        }
        else {
            g_budget = 0;
            logf("budget: vram table %s — budget raise disabled, everything else still works",
                 q ? "failed the sanity check" : "pattern NOT FOUND");
        }
    }

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

static DWORD WINAPI BeatLoop(LPVOID)
{
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            // once streaming is live, start the live-reload watcher — before that there is
            // nothing a change could apply to anyway
            if (!g_watcherStarted && !g_off && g_idsReady) {
                g_watcherStarted = true;
                CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
            }
            // re-assert: DLC mounts and FiveM's loader re-point claimed slots after us; whoever
            // writes the handle last wins, so write ours back. Same mechanism Cfx's own override
            // path uses (LoadStreamingFile.cpp writes Entries[].handle directly).
            // Lock held: the watcher can append to g_ovs (vector may reallocate) and swap g_pl
            // entries under us.
            if (!g_off && g_idsReady && g_mgr && g_mgr->entries) {
                EnterCriticalSection(&g_cs);
                for (auto& ov : g_ovs) {
                    if (!ov.handle || ov.id >= (uint32_t)g_mgr->numEntries) continue;
                    StrEntry& e = g_mgr->entries[ov.id];
                    if (e.handle == ov.handle) continue;
                    if ((e.flags & 3) >= 2) { ++g_deferred; continue; }   // being requested/loaded right now; retry next tick
                    uint32_t old = e.handle;
                    e.handle = ov.handle;
                    if (++g_reclaims <= 60) logf("RECLAIM  %s  (%08x -> %08x)", ov.slot, old, ov.handle);
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
        logf("alive (beat %d) — reg=%ld redirects=%ld reclaims=%ld deferred=%ld baseRegistered=%s",
             beat, (long)g_regTotal, (long)g_redirects, g_reclaims, g_deferred, g_didRegister ? "yes" : "no");
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
        g_self = h; DisableThreadLibraryCalls(h);
        if (SetupSafe()) {   // synchronous: must finish before the game's entry point runs (see Setup)
            CreateThread(nullptr, 0, BeatLoop, nullptr, 0, nullptr);
            CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
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
