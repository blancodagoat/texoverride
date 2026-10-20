#include "features/scanner.h"
#include "features/crash_saver.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "streaming/gate.h"
#include "streaming/rsc.h"
#include "streaming/budget.h"
#include <windows.h>
#include <cstring>
#include <unordered_map>
#include <vector>

std::string overrideKeyFor(const std::string& name, const std::string& childRel, bool announce)
{
    std::string ln = lower(name), relFwd = fwd(childRel);
    if (isIgnoredType(ln, relFwd, announce)) return "";
    if (!isOverrideExt(ln)) return "";
    // SAFETY GATE: the key is collection/file or a bare name, whatever folders sit above it
    // (slotKeyFor). Folders that are not collections are the user's own organisation.
    std::string relKey = lower(relFwd);
    const char* why = nullptr;
    std::string key = slotKeyFor(relKey, &why);
    if (key.empty()) {
        if (announce) LOG_WARN(LogCategory::Scan, "SKIP %s - %s", relKey.c_str(), why ? why : "refused");
        return "";
    }
    if (isQuarantined(key)) return "";   // crash saver; already logged loudly
    return key;
}

// The walk itself opens nothing: it only decides which names are ours.
// One key can only be provided once. Two packs shipping the same file keep the first one seen,
// unless the later one sits under an *_override folder, which is the user saying which copy wins.
static std::vector<Cand> walkOverrides()
{
    std::vector<Cand> out;
    std::unordered_map<std::string, size_t> seen;   // key -> where it sits in out
    const std::string base = g_overrideDir;
    walkTree(base, "", [&](const WalkItem& it) {
        if (it.dir) {
            if (!isDisabledFolder(it.name)) return true;
            LOG_INFO(LogCategory::Scan, "DISABLED %s - folder skipped; rename it without the disabled prefix to load it", fwd(it.childRel).c_str());
            return false;
        }
        std::string slot = overrideKeyFor(it.name, it.childRel, true);
        if (slot.empty()) return false;
        std::string relStr = lower(fwd(it.childRel));
        bool prio = isOverridePath(relStr);           // *_override folder: this copy wins
        auto s = seen.find(slot);
        if (s != seen.end()) {
            Cand& held = out[s->second];
            if (prio && !held.prio) {
                LOG_INFO(LogCategory::Scan, "PREFERRED %s - %s takes the slot from %s", slot.c_str(), relStr.c_str(), rel(held.full.c_str()));
                held.full = fwd(base + it.childRel);
                held.prio = true;
            } else {
                LOG_WARN(LogCategory::Scan, "DUPLICATE %s - %s provides the same file as one already listed; this copy is ignored%s",
                         slot.c_str(), relStr.c_str(), prio && held.prio ? " (another _override folder got there first)" : "");
            }
            return false;
        }
        seen[slot] = out.size();
        out.push_back({ slot, fwd(base + it.childRel), Cost(), prio });
        return false;
    });
    return out;
}

// Runs on the beat thread, NOT in DllMain. Everything here opens files, and Setup() sits in
// front of the game's entry point: doing it there meant a big pack held the loading screen
// before the game had even started. The hook waits on g_scanDone before it registers anything,
// so the game gets on with its own startup while this runs.
void scanOverrides()
{
    std::vector<Cand> cands = walkOverrides();
    LOG_INFO(LogCategory::Scan, "Discovered %zu candidate file(s); reading headers...", cands.size());
    ULONGLONG t0 = GetTickCount64();
    readCosts(cands);

    uint64_t costVirt = 0, costPhys = 0;
    std::vector<std::pair<uint64_t, std::string>> costBig;
    int n = 0;
    {
        CsLock lk(g_cs);   // live reload appends to both later; take it now so the rule has no exception
        for (auto& cd : cands) {
            if (cannotLoad(cd.slot.c_str(), cd.c, false)) continue;
            const char* slot = _strdup(cd.slot.c_str());
            const char* file = _strdup(cd.full.c_str());   // our absolute path
            g_ovs.push_back({ slot, file });
            g_bySlot[slot] = file;
            ++n;
            if (cd.c.rsc) {
                costVirt += cd.c.cv; costPhys += cd.c.cp;
                if (cd.c.cv + cd.c.cp >= (8u << 20)) costBig.push_back({ cd.c.cv + cd.c.cp, cd.slot });
            }
        }
    }
    LOG_INFO(LogCategory::Scan, "Loaded %d override(s) in %.1fs", n, (GetTickCount64() - t0) / 1000.0);

    // Nothing else writes g_ovs until the watcher starts, which is after this returns.
    std::unordered_map<std::string, int> collCounts;
    std::unordered_map<std::string, int> rootExtCounts;
    for (auto& ov : g_ovs) {
        const char* slash = strchr(ov.slot, '/');
        if (slash) ++collCounts[std::string(ov.slot, (size_t)(slash - ov.slot))];
        else {
            std::string ext = extOf(ov.slot);
            ++rootExtCounts[ext.empty() ? "(none)" : ext];
        }
    }

    if (!collCounts.empty()) {
        LOG_INFO(LogCategory::Scan, "  Collections (%zu):", collCounts.size());
        for (auto& kv : collCounts)
            LOG_INFO(LogCategory::Scan, "    %-38s %3d file(s)  [%s]", kv.first.c_str(), kv.second, classifyCollection(kv.first));
    }
    if (!rootExtCounts.empty()) {
        LOG_INFO(LogCategory::Scan, "  Root Assets:");
        for (auto& kv : rootExtCounts) {
            const char* typeDesc = "Other";
            if (kv.first == "ytd") typeDesc = "Overlays (.ytd)";
            else if (kv.first == "ydr") typeDesc = "Weapon and prop models (.ydr)";
            else if (kv.first == "yft") typeDesc = "Fragments (.yft)";
            else if (kv.first == "ycd") typeDesc = "Clip animations (.ycd)";
            else if (kv.first == "ymt" || kv.first == "ydd") typeDesc = "Animal models/meta";
            LOG_INFO(LogCategory::Scan, "    %-38s %3d file(s)", typeDesc, kv.second);
        }
    }

    // Body skin and face overlays are the one family where a slot claim is provably not enough.
    // Verified in game on b3751, 2026-09-08: the claim takes the real slot, the game loads the
    // user file from it, the log says "from your file", and the ped still draws stock. The ped
    // builds its face and body out of these before anything reachable from the streaming slot.
    // Say so at scan time, because the log otherwise reads perfectly while nothing changes, and
    // that costs somebody a whole evening before they think to doubt it.
    {
        static const char* kUnreachable[] = { "mp_fm_skin_", "mp_fm_faov_", "ng_hip_faov_", "mp_eye_colour" };
        int unreachable = 0;
        for (auto& ov : g_ovs) {
            if (strchr(ov.slot, '/')) continue;            // root keys only
            for (const char* pre : kUnreachable)
                if (_strnicmp(ov.slot, pre, strlen(pre)) == 0) { ++unreachable; break; }
        }
        if (unreachable)
            LOG_WARN(LogCategory::Scan, "  %d body skin / face overlay file(s) here (mp_fm_skin_*, mp_fm_faov_*, ng_hip_faov_*, mp_eye_colour). These CANNOT be replaced from tex_overrides: the slot claim works and the log will say your file loaded, but the ped is built from these earlier and keeps the stock look. Ship them in a FiveM mods folder package instead. Head models and head textures in a collection folder are unaffected.", unreachable);
    }

    costReport(costVirt, costPhys, costBig);
}
