#pragma once

#include "core/types.h"
#include <string>
#include <vector>

// The crash saver. Before a file goes into the game's hands its key is written to
// tex_overrides\_inflight.txt; if the game dies with it there, the next launch moves the key into
// _quarantine.txt and refuses to load it until the user deletes that file. A bad file costs one
// launch, not every launch. Everything it owns lives in crash_saver.cpp.

void crashSaverInit();      // Setup(): works out the two paths, touches nothing on disk
void crashSaverStartup();   // beat thread, before the scan: quarantine what the last run died on
bool isQuarantined(const std::string& key);

// The startup claim: one key at a time, so a fault inside the game's register call names exactly
// the file it was holding. Begin/End bracket the loop and own the detach guard while it runs.
void journalBegin();
void journalOne(const char* key);
void journalEnd();   // the whole loop survived: delete the journal

// Live reload: a batch goes in together and stays for 30 seconds after it is queued, because a
// broken file usually crashes within moments of streaming in. False = not written, do not apply.
bool journalWrite(const std::vector<LiveOp>& batch);
// Watcher thread: deletes the journal once its window has passed with no work still queued.
// Returns true while a window is open, so the watcher knows to keep waking up.
bool journalTick(bool workPending);

void journalOnDetach();   // DLL_PROCESS_DETACH
