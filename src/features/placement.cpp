#include "features/placement.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/pattern.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>

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

uint32_t joaat(const char* s, size_t n)   // GTA's case-insensitive hash
{
    uint32_t h = 0;
    for (size_t i = 0; i < n; ++i) { h += (uint8_t)tolower((unsigned char)s[i]); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15; return h;
}

void parsePlacementXml(const std::string& path, const char* fname, std::vector<PlColl>& out)
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

void placementLocate()
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

bool placementSolveImpl(PlColl& pc, uint8_t* coll)
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
bool placementSolve(PlColl& pc, uint8_t* coll)
{
    __try { return placementSolveImpl(pc, coll); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        pc.dead = true;
        LOG_WARN(LogCategory::Tattoo, "%s hit unreadable memory while solving, skipped", pc.name.c_str());
        return false;
    }
}

void placementBeat()
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
void placementBeatSafe()
{
    if (g_plFault || g_pl.empty()) return;
    __try { placementBeat(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_plFault, 1);
        LOG_ERROR(LogCategory::Tattoo, "Memory fault — tattoo placement disabled for this session");
    }
}

void mergePlacement(std::vector<PlColl>& fresh)
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

void loadPlacementFiles()
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
