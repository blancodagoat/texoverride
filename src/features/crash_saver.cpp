#include "features/crash_saver.h"
#include "core/state.h"
#include "core/logger.h"
#include <windows.h>
#include <cstdio>
#include <string>

// ---- crash saver ----
// APPEND, not overwrite. This opened with "w", so a second batch queued while the first was
// still inside its 30 second window wiped the first batch out of the journal, and a crash
// then quarantined the wrong file or none at all. A journal that cannot be written now
// refuses the batch: it is the only thing making a bad file cost one launch, not every one.
bool journalWrite(const std::vector<LiveOp>& batch)
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
void journalOne(const char* key)
{
    FILE* f; if (fopen_s(&f, g_inflightPath, "w") != 0 || !f) return;
    fputs(key, f); fputc('\n', f);
    fclose(f);
}

// Called at startup, before the folder scan. A leftover _inflight.txt means the game died while
// (or moments after) those files were being applied live — quarantine them.
void crashSaverStartup()
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
