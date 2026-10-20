#include "features/crash_saver.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include <windows.h>
#include <io.h>
#include <cstdio>
#include <string>
#include <unordered_set>

static std::string s_inflight, s_quarantine;
static std::unordered_set<std::string> s_keys;   // written at startup only, read by both scans
static bool s_ran = false;                       // crashSaverStartup has processed the old journal
static volatile LONG s_hot = 0;                  // the startup claim loop owns the journal
static volatile ULONGLONG s_clearAt = 0;         // a live batch's window ends here; 0 = none open
static FILE* s_journal = nullptr;                // held open across the startup loop

void crashSaverInit()
{
    s_inflight   = std::string(g_overrideDir) + "_inflight.txt";
    s_quarantine = std::string(g_overrideDir) + "_quarantine.txt";
}

bool isQuarantined(const std::string& key)
{
    return s_keys.count(key) != 0;
}

// Reads one key per line into s_keys; returns how many lines held one.
static int readKeys(FILE* f)
{
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        std::string k = trim(line);
        if (k.empty()) continue;
        s_keys.insert(k);
        ++n;
    }
    return n;
}

// Called at startup, before the folder scan. A leftover _inflight.txt means the game died while
// (or moments after) those files were being applied, so quarantine them.
void crashSaverStartup()
{
    if (FILE* f = openFile(s_inflight, "rb")) {
        std::unordered_set<std::string> before = s_keys;
        int any = readKeys(f);
        fclose(f);
        if (FILE* q = openFile(s_quarantine, "ab")) {
            for (auto& k : s_keys) if (!before.count(k)) fprintf(q, "%s\n", k.c_str());
            fclose(q);
        }
        if (any) LOG_WARN(LogCategory::Scan, "CRASH SAVER: Last run died while registering file(s); quarantined to prevent crash loop");
    }
    deleteFile(s_inflight);
    if (FILE* f = openFile(s_quarantine, "rb")) {
        readKeys(f);
        fclose(f);
        for (auto& k : s_keys)
            LOG_WARN(LogCategory::Scan, "QUARANTINED %s - not loaded; delete _quarantine.txt in tex_overrides to try it again", k.c_str());
    }
    s_ran = true;
}

// One handle for the whole loop. It used to be an open, a write and a close per file, inside
// g_cs on the game's streaming thread, and every close is where antivirus scans the file: a pack
// of a few thousand files paid for a few thousand scans before the game could go on loading.
void journalBegin()
{
    InterlockedExchange(&s_hot, 1);
    s_journal = openFile(s_inflight, "wb");
}

void journalOne(const char* key)
{
    if (!s_journal) return;
    // Empty it and write the one key. The game can only fault inside the register call that
    // comes after this, never in here, so there is no moment where the journal names nothing.
    fflush(s_journal);
    _chsize_s(_fileno(s_journal), 0);
    rewind(s_journal);
    fputs(key, s_journal); fputc('\n', s_journal);
    fflush(s_journal);
}

void journalEnd()
{
    if (s_journal) { fclose(s_journal); s_journal = nullptr; }
    deleteFile(s_inflight);   // whole loop survived; nothing to quarantine
    InterlockedExchange(&s_hot, 0);
}

// APPEND, not overwrite. This opened with "w", so a second batch queued while the first was
// still inside its 30 second window wiped the first batch out of the journal, and a crash
// then quarantined the wrong file or none at all. A journal that cannot be written refuses the
// batch: it is the only thing making a bad file cost one launch, not every one.
bool journalWrite(const std::vector<LiveOp>& batch)
{
    FILE* f = openFile(s_inflight, "ab");
    if (!f) return false;
    bool ok = true;
    for (auto& op : batch) if (fprintf(f, "%s\n", op.slot.c_str()) < 0) { ok = false; break; }
    if (ferror(f) || fflush(f) != 0) ok = false;
    if (fclose(f) == EOF) ok = false;
    if (ok) s_clearAt = GetTickCount64() + 30000;
    return ok;
}

bool journalTick(bool workPending)
{
    ULONGLONG at = s_clearAt;
    if (!at) return false;
    if (GetTickCount64() < at || workPending) return true;
    // survived the risky window; nothing to quarantine. Unless the startup claim is holding the
    // journal right now (the watcher can start before it on the time fallback).
    if (!InterlockedCompareExchange(&s_hot, 0, 0)) deleteFile(s_inflight);
    s_clearAt = 0;
    return false;
}

// Orderly exit = no crash: the journal must not quarantine anything. Only once crashSaverStartup
// has processed any leftover journal, though: if startup faulted before that point, deleting here
// would erase the previous crash's evidence.
//
// The two guards are the important half. "A real crash never reaches this line" was an
// ASSUMPTION, and it is wrong often enough to matter: FiveM catches the fault, shows its crash
// dialog, uploads a report and then tears the process down, and that path can run detach for
// every loaded module. Deleting the journal there erases the name of the file that just killed
// the game, so the next launch has nothing to quarantine and dies in the same place forever. So
// leave it alone while the startup loop holds a key, and while a live batch is inside its 30
// second window. The price of the second is that quitting within 30s of a live add quarantines
// that file once, loudly, in the log.
void journalOnDetach()
{
    if (s_ran && !s_inflight.empty() && !InterlockedCompareExchange(&s_hot, 0, 0) && !s_clearAt)
        deleteFile(s_inflight);
}
