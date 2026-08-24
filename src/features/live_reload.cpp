#include "features/live_reload.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "streaming/gate.h"
#include "streaming/rsc.h"
#include "streaming/streaming.h"
#include "features/placement.h"
#include "features/crash_saver.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// every call site freed slot and file and forgot gfile, which 0.8.3 added beside them
void freeLiveOp(LiveOp& op)
{
    free((void*)op.ov.slot); free((void*)op.ov.file);
    if (op.ov.gfile && op.ov.gfile != op.ov.file) free((void*)op.ov.gfile);
    op.ov.slot = op.ov.file = op.ov.gfile = nullptr;
}

// Overwritten file: zero the raw entry's timestamp, then GetEntry re-stats it (new size picked
// up). Without this an overwritten file keeps its old cached size — short or wild reads.
// RawEntry layout from Cfx fiCollectionWrapper.h: fe 16 bytes, timestamp at +16.
bool rawInvalidate(uint32_t handle)
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
int liveRegister(Ov& ov)
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

void drainOps()   // runs on the game's main thread
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

static volatile LONGLONG g_lastPumpWorkAt = 0;                  // caps work when PeekMessageW spins

BOOL WINAPI h_peekMsg(LPMSG m, HWND w, UINT a, UINT b, UINT r)
{
    // the first caller after install is the game's main loop (it pumps every frame);
    // only that thread ever drains, so ops always run where Cfx runs its own registrations
    DWORD tid = GetCurrentThreadId();
    if (!g_pumpTid) g_pumpTid = tid;
    if (g_opsPending && tid == g_pumpTid) {
        // Some message loops call PeekMessageW repeatedly in one rendered frame. One shared
        // work window every 10 ms keeps "eight per call" from becoming hundreds per frame.
        LONGLONG now = (LONGLONG)GetTickCount64();
        LONGLONG last = InterlockedCompareExchange64(&g_lastPumpWorkAt, 0, 0);
        if (now - last >= 10 && InterlockedCompareExchange64(&g_lastPumpWorkAt, now, last) == last)
            drainOps();
    }
    return g_origPeek(m, w, a, b, r);
}

bool installPump()
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

// Hand a batch to the game thread: journal first, then bound and coalesce pending work by slot.
void submitBatch(std::vector<LiveOp>& batch)
{
    // No waiting on the previous batch any more. The queue is a deque the pump drains in shards,
    // so a new batch just joins the back; the old 10 second sleep loop blocked the watcher thread
    // and threw the batch away for no reason other than the queue being busy.
    if (!journalWrite(batch)) {
        LOG_ERROR(LogCategory::Live, "Live reload: crash-saver journal could not be written; change not applied");
        for (auto& op : batch) freeLiveOp(op);
        return;
    }
    size_t dropped = 0;
    EnterCriticalSection(&g_cs);
    try {
        // find_if over the deque per incoming op: the 2,048 bound is what keeps this cheap
        for (auto& op : batch) {
            auto same = std::find_if(g_opQ.begin(), g_opQ.end(), [&](const LiveOp& queued) {
                return strcmp(queued.ov.slot, op.ov.slot) == 0;
            });
            if (same != g_opQ.end()) {
                // A re-stat cannot replace a registration that has not run yet: there is no raw
                // handle to refresh until that registration succeeds. The pending registration
                // already points at this path and will read the newest bytes.
                if (same->kind == 0 && op.kind == 1) continue;

                freeLiveOp(*same);
                *same = op;
                op.ov.slot = op.ov.file = op.ov.gfile = nullptr;
            }
            else if (g_opQ.size() < 2048) {
                g_opQ.push_back(op);
                op.ov.slot = op.ov.file = op.ov.gfile = nullptr;
            }
            else {
                ++dropped;
            }
        }
    }
    catch (...) {
        LOG_ERROR(LogCategory::Live, "Live reload: queue allocation failed; remaining changes need a FiveM restart");
    }
    if (!g_opQ.empty()) InterlockedExchange(&g_opsPending, 1);
    LeaveCriticalSection(&g_cs);
    for (auto& op : batch) freeLiveOp(op);
    if (dropped) {
        LOG_WARN(LogCategory::Live, "Live reload: queue limit reached; %zu change(s) need a FiveM restart", dropped);
    }
    g_journalClearAt = GetTickCount64() + 30000;   // journal outlives the apply; see CRASH SAVER above
}

// Detects what changed and queues work; the game-touching half runs later in drainOps on the
// main thread. quiet = the baseline pass right after the watcher starts: it seeds the stamp map
// (and still queues files that appeared during loading) without re-logging SKIP/IGNORED lines
// the startup scan already wrote.
void rescanTree(const std::string& base, const std::string& sub, bool quiet, std::vector<std::string>& xmls, std::vector<LiveOp>& batch)
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

DWORD WINAPI WatchLoop(LPVOID)
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
