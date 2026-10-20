#include "features/live_reload.h"
#include "features/clean_shot.h"
#include "features/crash_saver.h"
#include "features/placement.h"
#include "features/scanner.h"
#include "core/state.h"
#include "core/cfx.h"
#include "core/logger.h"
#include "core/pattern.h"
#include "core/utils.h"
#include "streaming/gate.h"
#include "streaming/budget.h"
#include "streaming/rsc.h"
#include "streaming/streaming.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// ============================ live reload (watch tex_overrides) ============================
// A watcher thread sits on FindFirstChangeNotification: fully event-driven, no polling. When the
// folder changes it waits half a second for the writes to go quiet, then rescans once.
//
//   - edited placement .xml  -> re-parsed, layout carried over, applied by the next beat
//   - overwritten .ytd/.ydd  -> raw-streamer entry invalidated so the next load re-reads the file
//                               (Cfx's own trick: timestamp = 0, then GetEntry re-stats; see
//                               LoadStreamingFile.cpp; the game shows it when the item reloads)
//   - brand-new file         -> registered mid-session, exactly what Cfx does on every resource
//                               restart (CfxCollection_AddStreamingFileByTag -> LoadStreamingFiles)
//
// THREADING: the two game-touching operations (register, re-stat) never run on the watcher
// thread. RAGE's registration path (strStreamingInfoManager::RegisterObject -> module->Register)
// inserts into name tables with no lock, and the game's main thread reads those tables all the
// time; that is why Cfx runs its own mid-session registrations on the main thread
// (OnMainGameFrame). We subscribe to that same event; on a FiveM that does not export it, an IAT
// shim on the game's PeekMessageW import (Cfx's ZOffThreadWindowing hooks that exact import, and
// the main loop pumps it every frame) gets us there instead. The watcher queues work, the main
// thread drains it.
//
// CRASH SAVER: before a batch is handed to the game thread it is journaled (crash_saver.cpp), and
// the journal stays for 30 seconds after it is queued. If the game dies inside that window, the
// next launch quarantines those names. A bad file can crash at most one session.

typedef void* (*GetRawStreamer_t)();
typedef uint8_t* (*RawGetEntry_t)(void*, uint16_t);
typedef BOOL (WINAPI* PeekMsg_t)(LPMSG, HWND, UINT, UINT, UINT);

struct Snap { uint64_t wt, size; };

static GetRawStreamer_t g_getRawStreamerFn = nullptr;
static RawGetEntry_t    g_rawGetEntryFn = nullptr;
static std::deque<LiveOp> g_opQ;                           // under g_cs
static volatile LONG g_opsPending = 0;
static PeekMsg_t g_origPeek = nullptr;
static DWORD g_pumpTid = 0;
static bool g_pumpReady = false;                           // a main-thread pump is live, whichever it is
static bool g_framePumpConnected = false;                  // OnMainGameFrame, subscribed in Setup()
static std::unordered_map<std::string, Snap> g_snap;      // watcher thread only

void locateRawStreamer()
{
    const short PAT_GRS[] = { 0x48,0x8B,0xD3,0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x41,0xFF,0x90,-1,0x01,0x00,0x00,0x8B,0xD8,0xE8 };
    if (uint8_t* q = scanModule(PAT_GRS)) {
        if (q[-5] == 0xE8) g_getRawStreamerFn = (GetRawStreamer_t)ripTarget(q - 4);
    }
    const short PAT_GE[] = { 0x0F,0xB7,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x8B,0xD0,0x25,0xFF };
    if (uint8_t* q = scanModule(PAT_GE)) g_rawGetEntryFn = (RawGetEntry_t)(q - 0x14);
    LOG_INFO(LogCategory::Live, "Live reload exports: rawStreamer=%s getEntry=%s",
             g_getRawStreamerFn ? "ok" : "MISSING", g_rawGetEntryFn ? "ok" : "MISSING");
}

// Overwritten file: zero the raw entry's timestamp, then GetEntry re-stats it (new size picked
// up). Without this an overwritten file keeps its old cached size: short or wild reads.
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

static void applyRegister(LiveOp& op)
{
    // The strings live in the op until the claim is known to stick; only then do they become the
    // leaked copies every published override is made of.
    Ov ov;
    ov.slot = op.slot.c_str();
    ov.file = op.file.c_str();
    ClaimResult r = claimOne(ov);
    if (r == CLAIM_REJECTED) {
        // registerRawStreamingFile refuses a slot that already holds a handle, and the
        // occupied-slot path could not find a raw entry for our own file either. A restart
        // claims the slot before the server or DLC mounts.
        LOG_WARN(LogCategory::Live, "Live reload: %s is already loaded from server/DLC and no raw entry was available; restart FiveM to claim", op.slot.c_str());
        return;
    }
    if (r == CLAIM_NO_HANDLE) {
        LOG_ERROR(LogCategory::Live, "Live reload: could not register %s (no handle came back), restart to pick it up", op.slot.c_str());
        return;
    }
    ov.slot = _strdup(op.slot.c_str());
    ov.file = _strdup(op.file.c_str());
    {
        CsLock lk(g_cs);
        g_ovs.push_back(ov);
        g_bySlot[ov.slot] = ov.file;
    }
    if (r == CLAIM_DIRECT)
        LOG_INFO(LogCategory::Live, "LIVE-ADD: %s <- tex_overrides/%s (id=%u handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
    else if (r == CLAIM_TAKEOVER)
        LOG_INFO(LogCategory::Live, "LIVE-TAKEOVER: %s <- tex_overrides/%s (id=%u raw handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
    else
        LOG_INFO(LogCategory::Live, "LIVE-WAIT: %s (raw handle=%08x; target slot attached when present)", ov.slot, ov.handle);
    uint64_t cv = 0, cp = 0;
    if (rscCost(op.file, &cv, &cp) && cv + cp >= (8u << 20))
        LOG_WARN(LogCategory::Live, "  HEAVY %.1f MB in memory (likely 4K or uncompressed; shrink to fight texture loss)", (cv + cp) / 1048576.0);
}

static void drainOps()   // runs on the game's main thread
{
    // A folder copy can queue hundreds at once. Take a small bounded shard: the pump runs every
    // frame, so the rest lands on the next few instead of one giant stall on the game thread
    // that a player experiences as a freeze.
    std::vector<LiveOp> ops;
    {
        CsLock lk(g_cs);
        for (int i = 0; i < 8 && !g_opQ.empty(); ++i) { ops.push_back(std::move(g_opQ.front())); g_opQ.pop_front(); }
    }
    for (auto& op : ops) {
        if (op.kind == LiveOp::Register) { applyRegister(op); continue; }
        if (g_getRawStreamerFn && g_rawGetEntryFn && rawInvalidate(op.handle))
            // Honest about the ceiling: re-statting the entry points it at the new bytes, so
            // the next STREAM IN reads them. It cannot touch what the game already holds in
            // memory, and taking the item off and putting it back does not force a reload.
            LOG_INFO(LogCategory::Live, "LIVE-UPDATE: %s reread from disk (the game keeps the copy it already loaded until you restart)", op.slot.c_str());
        else
            LOG_WARN(LogCategory::Live, "Live reload: %s changed, could not refresh it, restart to apply", op.slot.c_str());
    }
    CsLock lk(g_cs);
    if (g_opQ.empty()) InterlockedExchange(&g_opsPending, 0);   // more shards may still be waiting
}

// Everything the plugin does on the game's main thread, from whichever of the two pumps is live.
// Nothing may escape from here: it is called from inside FiveM's own frame event or the game's
// message pump, and an exception unwinding into either ends the process.
static void framePumpTick()
{
    try {
#ifdef TEXOVERRIDE_DEV
        // Whether this is being called at all is the single least certain thing about the frame
        // event, so a dev build counts the calls and says so once every ten seconds.
        static ULONGLONG devNext = 0; static long devTicks = 0;
        ++devTicks;
        ULONGLONG nowMs = GetTickCount64();
        if (!devNext) devNext = nowMs + 10000;
        if (nowMs >= devNext) { LOG_DEV(LogCategory::Live, "frame pump: %ld tick(s) in the last 10s", devTicks); devTicks = 0; devNext = nowMs + 10000; }
#endif
        if (g_opsPending) drainOps();
        shotKeyTick();
        // Once a second, on the game's own thread. grcResourceCache::GetInstance is a jump straight
        // into GTA5.exe, and the plugin's standing rule for calls into game code is that they run
        // where the game runs them; 0.8.14 called it from the beat thread every second instead.
        static ULONGLONG poolNext = 0;
        ULONGLONG poolNow = GetTickCount64();
        if (poolNow >= poolNext) { poolNext = poolNow + 1000; poolBeat(); }
    }
    catch (...) {
        static bool said = false;
        if (!said) { said = true; LOG_ERROR(LogCategory::Live, "Live reload: out of memory on the game thread; queued changes may need a restart"); }
    }
}

static volatile LONGLONG g_lastPumpWorkAt = 0;   // caps work when PeekMessageW spins

static BOOL WINAPI h_peekMsg(LPMSG m, HWND w, UINT a, UINT b, UINT r)
{
    // the first caller after install is the game's main loop (it pumps every frame);
    // only that thread ever drains, so ops always run where Cfx runs its own registrations
    DWORD tid = GetCurrentThreadId();
    if (!g_pumpTid) g_pumpTid = tid;
    if (tid == g_pumpTid) {
        // Some message loops call PeekMessageW repeatedly in one rendered frame. One shared
        // work window every 10 ms keeps "eight per call" from becoming hundreds per frame, and
        // is still far faster than anyone can press a key.
        LONGLONG now = (LONGLONG)GetTickCount64();
        LONGLONG last = InterlockedCompareExchange64(&g_lastPumpWorkAt, 0, 0);
        if (now - last >= 10 && InterlockedCompareExchange64(&g_lastPumpWorkAt, now, last) == last)
            framePumpTick();
    }
    return g_origPeek(m, w, a, b, r);
}

// FiveM runs its own mid-session registrations on the game's main thread, and rage-nutsnbolts-five
// exports the very event it uses to get there. Subscribing to it lands on the same thread, once a
// frame, with no patch into the game at all - so it is tried first, and the PeekMessageW import
// hook below stays only as the fallback for a FiveM that does not export it.
bool connectFramePump()
{
    void* ev = cfxSymbol("rage-nutsnbolts-five.dll", "?OnMainGameFrame@@3V?$fwEvent@$$V@@A");
    // Returning true keeps the rest of the chain running: a handler that returns false stops
    // every subscriber after it, which here would be FiveM's own per-frame work.
    g_framePumpConnected = ev && cfxConnect(ev, [] { framePumpTick(); return true; });
    return g_framePumpConnected;
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

// Hand a batch to the game thread: journal first, then bound and coalesce pending work by slot.
static void submitBatch(std::vector<LiveOp>& batch)
{
    if (!journalWrite(batch)) {
        LOG_ERROR(LogCategory::Live, "Live reload: crash-saver journal could not be written; change not applied");
        return;
    }
    size_t dropped = 0;
    {
        CsLock lk(g_cs);
        // find_if over the deque per incoming op: the 2,048 bound is what keeps this cheap
        for (auto& op : batch) {
            auto same = std::find_if(g_opQ.begin(), g_opQ.end(), [&](const LiveOp& queued) { return queued.slot == op.slot; });
            if (same != g_opQ.end()) {
                // A re-stat cannot replace a registration that has not run yet: there is no raw
                // handle to refresh until that registration succeeds. The pending registration
                // already points at this path and will read the newest bytes.
                if (same->kind == LiveOp::Register && op.kind == LiveOp::Restat) continue;
                *same = std::move(op);
            }
            else if (g_opQ.size() < 2048) g_opQ.push_back(std::move(op));
            else { ++dropped; continue; }
            InterlockedExchange(&g_opsPending, 1);
        }
    }
    if (dropped)
        LOG_WARN(LogCategory::Live, "Live reload: queue limit reached; %zu change(s) need a FiveM restart", dropped);
}

// Detects what changed and queues work; the game-touching half runs later in drainOps on the
// main thread. quiet = the baseline pass right after the watcher starts: it seeds the stamp map
// (and still queues files that appeared during loading) without re-logging SKIP/IGNORED lines
// the startup scan already wrote.
static void rescanTree(bool quiet, std::vector<std::string>& xmls, std::vector<LiveOp>& batch)
{
    const std::string base = g_overrideDir;
    walkTree(base, "", [&](const WalkItem& it) {
        // startup logged a disabled folder; a rename back makes its files new and they load
        if (it.dir) return !isDisabledFolder(it.name);

        std::string full = base + it.childRel;
        Snap now{ it.writeTime, it.size };
        auto seen = g_snap.find(full);
        bool isNew = seen == g_snap.end();
        bool isChanged = !isNew && (seen->second.wt != now.wt || seen->second.size != now.size);
        g_snap[full] = now;
        if (!isNew && !isChanged) return false;
        bool announce = isNew && !quiet;

        if (it.childRel == it.name && hasExt(lower(it.name), ".xml")) { if (!quiet) xmls.push_back(it.name); return false; }
        std::string key = overrideKeyFor(it.name, it.childRel, announce);   // same rule as the startup scan
        if (key.empty()) return false;

        uint32_t handle = 0; bool known = false;
        {
            CsLock lk(g_cs);
            for (auto& ov : g_ovs) if (key == ov.slot) { known = true; handle = ov.handle; break; }
        }

        std::string file = fwd(full);
        if (!known) {
            if (!g_pumpReady) { LOG_INFO(LogCategory::Live, "Live reload: new file %s needs a game restart", key.c_str()); return false; }
            if (cannotLoadPath(file, key.c_str(), quiet)) return false;   // quiet skips the baseline re-log
            batch.push_back({ LiveOp::Register, key, file });
        }
        else if (handle && isChanged) {
            if ((handle >> 16) != 0)   // not in the game's own raw streamer; cannot re-stat it
                LOG_WARN(LogCategory::Live, "Live reload: %s changed, restart to apply (handle %08x not raw)", key.c_str(), handle);
            else if (!g_pumpReady)
                LOG_INFO(LogCategory::Live, "Live reload: %s changed, restart to apply", key.c_str());
            else if (cannotLoadPath(file, key.c_str(), false))
                // the registered slot still points at this path with the OLD size cached, so the
                // game may read a truncated slice of the new content; make that loud
                LOG_WARN(LogCategory::Live, "Live reload: %s cannot be opened; not reread", key.c_str());
            else
                batch.push_back({ LiveOp::Restat, key, std::string(), handle });
        }
        else if (announce) {
            // A brand new file whose slot is already taken is a second copy of something already
            // loaded, usually an _override pack copied in mid-session. Which copy wins is decided
            // by the startup scan, so this one does nothing until the next launch. Silence here
            // is indistinguishable from a plugin that did not notice the file at all.
            LOG_INFO(LogCategory::Live, "Live reload: %s is already loaded from another folder; restart FiveM to use this copy", key.c_str());
        }
        return false;
    });
}

static void watchLoop()
{
    std::string dir = g_overrideDir; if (!dir.empty() && dir.back() == '\\') dir.pop_back();
    HANDLE h = FindFirstChangeNotificationW(widen(dir).c_str(), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    if (h == INVALID_HANDLE_VALUE) { LOG_ERROR(LogCategory::Live, "Live reload: cannot watch tex_overrides (err %lu); restart to apply changes", GetLastError()); return; }

    // The frame event was subscribed back in Setup(), on the loader thread. Only the fallback
    // is left to decide here, and it patches an import, so it stays where it always was.
    bool viaEvent = g_framePumpConnected;
    g_pumpReady = viaEvent || installPump();
    { std::vector<std::string> ignore; std::vector<LiveOp> batch;
      rescanTree(true, ignore, batch);   // baseline stamps; also catches files added during loading
      if (!batch.empty()) submitBatch(batch); }
    LOG_INFO(LogCategory::Live, "Directory watcher active (%s)",
             !g_pumpReady   ? "edits only: new files need restart"
             : viaEvent     ? "full: edits and new files, on FiveM's own frame event"
                            : "full: edits and new files, via the message pump");
    for (;;) {
        DWORD w = WaitForSingleObject(h, journalTick(g_opsPending != 0) ? 1000 : INFINITE);
        if (w == WAIT_TIMEOUT) continue;
        if (w != WAIT_OBJECT_0) break;
        do { FindNextChangeNotification(h); } while (WaitForSingleObject(h, 500) == WAIT_OBJECT_0);   // debounce until quiet
        std::vector<std::string> xmls; std::vector<LiveOp> batch;
        rescanTree(false, xmls, batch);
        if (!batch.empty()) submitBatch(batch);
        if (!xmls.empty()) {
            std::vector<PlColl> fresh;
            for (auto& f : xmls) parsePlacementXml(std::string(g_overrideDir) + f, f.c_str(), fresh);
            if (!fresh.empty()) mergePlacement(fresh);
        }
    }
    FindCloseChangeNotification(h);
}

DWORD WINAPI WatchLoop(LPVOID)
{
    // A thread that lets an exception out takes the whole process with it (std::terminate).
    try { watchLoop(); }
    catch (...) { LOG_ERROR(LogCategory::Live, "Live reload stopped (out of memory); restart FiveM to pick up further changes"); }
    return 0;
}
