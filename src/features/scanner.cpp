#include "features/scanner.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "streaming/gate.h"
#include "streaming/rsc.h"
#include "streaming/budget.h"
#include <windows.h>
#include <cstring>
#include <unordered_map>

// The walk itself opens nothing: it only decides which names are ours.
// One key can only be provided once; two packs shipping the same file keep the first one seen.
static std::unordered_set<std::string> g_seenKeys;

void walkDir(const std::string& base, const std::string& rel, std::vector<Cand>& out)
{
    if (rel.empty()) g_seenKeys.clear();
    std::string pattern = base + rel + "\\*";
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string childRel = rel.empty() ? name : rel + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (isDisabledFolder(name)) { LOG_INFO(LogCategory::Scan, "DISABLED %s - folder skipped; rename it without the disabled prefix to load it", fwd(childRel).c_str()); continue; }
            walkDir(base, childRel, out); continue;
        }
        std::string ln = lower(name);
        if (isIgnoredType(ln, fwd(childRel), true)) continue;
        if (!isOverrideExt(ln)) continue;
        std::string relStr = lower(fwd(childRel));
        // SAFETY GATE: the key is collection/file or a bare name, whatever folders sit above it
        // (slotKeyFor). Folders that are not collections are the user's own organisation.
        const char* why = nullptr;
        std::string slotStr = slotKeyFor(relStr, &why);
        if (slotStr.empty()) {
            LOG_WARN(LogCategory::Scan, "SKIP %s - %s", relStr.c_str(), why ? why : "refused");
            continue;
        }
        if (!g_seenKeys.insert(slotStr).second) {
            LOG_WARN(LogCategory::Scan, "DUPLICATE %s - %s provides the same file as one already listed; this copy is ignored", slotStr.c_str(), relStr.c_str());
            continue;
        }
        if (g_quarantine.count(slotStr)) continue;   // crash saver; already logged loudly
        out.push_back({ slotStr, fwd(base + childRel), Cost() });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Runs on the beat thread, NOT in DllMain. Everything here opens files, and Setup() sits in
// front of the game's entry point: doing it there meant a big pack held the loading screen
// before the game had even started. The hook waits on g_scanDone before it registers anything,
// so the game gets on with its own startup while this runs.
void scanFinish()
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
            else if (kv.first == "ydr") typeDesc = "Weapon and prop models (.ydr)";
            else if (kv.first == "yft") typeDesc = "Fragments (.yft)";
            else if (kv.first == "ycd") typeDesc = "Clip animations (.ycd)";
            else if (kv.first == "ymt" || kv.first == "ydd") typeDesc = "Animal models/meta";
            LOG_INFO(LogCategory::Scan, "    %-38s %3d file(s)", typeDesc, kv.second);
        }
    }

    costReport();
    g_cands.clear(); g_cands.shrink_to_fit();
}
