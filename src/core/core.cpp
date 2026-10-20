#include "core/core.h"
#include "core/version.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/settings.h"
#include "core/pattern.h"
#include "core/cfx.h"
#include "streaming/gate.h"
#include "streaming/budget.h"
#include "streaming/streaming.h"
#include "features/placement.h"
#include "features/crash_saver.h"
#include "features/scanner.h"
#include "features/live_reload.h"
#include "features/clean_shot.h"
#include "MinHook.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>

static volatile LONG g_waitLogged = 0;
static volatile LONG g_redirects = 0;
static volatile LONG g_idsReady = 0;        // the startup claim has run
static volatile LONG g_firstLoadDone = 0;   // FiveM said the game finished its first load
static long g_reclaims = 0;                 // beat thread only
static long g_lateBinds = 0;                // beat thread only
// collection map: root files counted, not listed. Incremented under g_cs by the hook thread,
// read unlocked by the beat thread for the heartbeat, hence volatile: display only, and an
// aligned long cannot tear on x86-64.
static volatile long g_collOther = 0;
static std::unordered_set<std::string> g_collSeen;   // under g_cs
static bool g_didRegister = false;                   // under g_cs
static bool g_captured = false;                      // under g_cs
static bool g_watcherStarted = false;                // beat thread only

// The store cannot answer a name lookup on FiveM's FIRST stream call, which is where the base
// claim used to run. Every extension logged "the module does not know that name" there, and the
// identical lookup succeeded about three seconds later. That killed the pin-both path stone dead
// in every session ever logged: aliased was always 0, so a name the game already owned got a
// freshly minted index, our handle landed in a slot nothing looks up, and the log read perfectly
// while the game kept drawing its own file. Wait for the store to start answering before
// claiming. The probe is two base-game names that have shipped in streamedpeds_mp.rpf since
// launch, so it does not depend on what the user happens to have in tex_overrides.
static bool storeAnswersNames()
{
    static const char* kProbe[] = { "mp_f_freemode_01/head_diff_000_a_whi.ytd",
                                    "mp_f_freemode_01/head_000_r.ydd" };
    for (const char* probe : kProbe) {
        int why = SLOT_OK;
        if (validStreamingId(targetStreamingId(probe, &why))) return true;
    }
    // Never trade the claim away entirely: the whole point of claiming early is to be holding the
    // slot before the server's own mount arrives. If the store still says nothing by the deadline,
    // claim on the old terms and say so, because an unpinned claim still beats no claim.
    static ULONGLONG first = 0;
    const ULONGLONG now = GetTickCount64();
    if (!first) first = now;
    if (now - first < 20000) return false;
    static bool said = false;
    if (!said) {
        said = true;
        LOG_WARN(LogCategory::Claim, "The game's streaming store never answered a name lookup in 20s; claiming without checking which id each name already resolves to (files the game also ships may not take effect)");
    }
    return true;
}

// Outside the lock, deliberately: scanOverrides takes g_cs to publish its results, so a wait with
// the lock held would deadlock. This is the only place the game's own thread ever waits on us,
// and it only ever waits once.
// Bounded, and skipped entirely when the plugin is off: if the scan thread never got started (a
// fault in Setup disables the plugin but leaves this hook live) an infinite wait here would hang
// the game on a plugin that is already doing nothing.
//
// How long this waits IS the plugin's cost to startup, and it is the only number that says
// whether moving the scan off DllMain bought anything: the scan's own duration does not, because
// most of it overlaps with the game starting. Logged once.
static void waitForScan()
{
    if (g_off || !g_scanDone) return;
    ULONGLONG w0 = GetTickCount64();
    DWORD r = WaitForSingleObject(g_scanDone, 300000);
    ULONGLONG ms = GetTickCount64() - w0;
    if (InterlockedExchange(&g_waitLogged, 1)) return;
    if (r == WAIT_TIMEOUT)
        LOG_WARN(LogCategory::Core, "File scan still running after 5 minutes; registering partial results");
    else if (ms >= 100)
        LOG_INFO(LogCategory::Core, "Game ready for files %.1fs before scan finished; waited", ms / 1000.0);
    else
        LOG_INFO(LogCategory::Core, "Scan finished before game needed it (no startup delay)");
}

// Once the stream system is live (its first call), register our files as base slot overrides.
// Hook thread, under g_cs. o_regRaw is the trampoline (the original), so these calls do NOT
// re-enter the hook.
static void claimAll()
{
    // the pool must exist by now (this very call registers into it); if it looks wrong, the
    // manager pattern matched the wrong code: better no re-assert than a wild write
    if (g_mgr && (!g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)) {
        LOG_WARN(LogCategory::Core, "Streaming pool looks invalid (entries=%p num=%d); re-assert disabled", (void*)g_mgr->entries, g_mgr->numEntries);
        g_mgr = nullptr;
    }
    int direct = 0, takeovers = 0, waiting = 0, rejected = 0, aliased = 0;
    journalBegin();
    for (auto& ov : g_ovs) {
        // named in _inflight.txt for the duration of the call; if the game dies in there, the
        // next launch quarantines this key and boots without it
        journalOne(ov.slot);
        switch (claimOne(ov)) {
        case CLAIM_DIRECT:
        case CLAIM_NO_HANDLE:
            ++direct;
            if (ov.altId != 0xFFFFFFFF) {
                ++aliased;
                LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x; store resolves to id=%u, pinning both)", ov.slot, rel(ov.file), ov.id, ov.handle, ov.altId);
            }
            else LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
            break;
        case CLAIM_TAKEOVER:
            ++takeovers;
            LOG_INFO(LogCategory::Claim, "OVERRIDE-TAKEOVER: %s <- tex_overrides/%s (id=%u raw handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
            break;
        case CLAIM_WAITING:
            ++waiting;
            LOG_INFO(LogCategory::Claim, "OVERRIDE-WAIT: %s <- tex_overrides/%s (raw handle=%08x; target not present yet)", ov.slot, rel(ov.file), ov.handle);
            break;
        default:
            ++rejected;
            LOG_ERROR(LogCategory::Claim, "OVERRIDE-FAILED: %s <- tex_overrides/%s (registration rejected; no usable raw entry)", ov.slot, rel(ov.file));
            break;
        }
    }
    journalEnd();   // whole loop survived; nothing to quarantine
    LOG_INFO(LogCategory::Claim, "Claimed %d base-slot override(s): %d direct, %d occupied-slot takeover, %d waiting for target, %d rejected",
             direct + takeovers + waiting, direct, takeovers, waiting, rejected);
    if (aliased) LOG_INFO(LogCategory::Claim, "%d of those named a slot the game already owned under a different id; pinned both", aliased);

    // Per type summary
    { std::unordered_map<std::string, int> byExt, aliasByExt, deadByExt;
      for (auto& ov : g_ovs) {
          std::string ext = extOf(ov.slot);
          if (ext.empty()) ext = "(none)";
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
                         ov.handle, g_mgr->entries[ov.id < (uint32_t)g_mgr->numEntries ? ov.id : ov.altId].handle);
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
                  LOG_WARN(LogCategory::Verify, "  ALREADY IN MEMORY: %s (status=%s, strflags=%04x, deps=%u); game will reload when re-equipped",
                           ov.slot, strStatusText(f), (unsigned)(f >> 16), (unsigned)((f >> 2) & 0x3FFF));
          }
          LOG_INFO(LogCategory::Verify, "Residency at claim time: %d resident in memory, %d loading, %d cold, %d persistent",
                   resident, pending, cold, pinned); }
    }
    if (g_mgr) LOG_DEBUG(LogCategory::Verify, "Streaming pool: entries=%p numEntries=%d", (void*)g_mgr->entries, g_mgr->numEntries);
    InterlockedExchange(&g_idsReady, 1);
}

// MAP: one line per distinct thing the server streams. Hook thread, under g_cs. This is the
// discovery channel: names we REFUSE have to appear too, or a refused collection reads exactly
// like one the server never streamed, and a user's log is the only way we ever learn about a
// naming family the gate does not know yet. That is how the folder whitelist died in 0.8.5.
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
static void logServerName(const std::string& keyLower)
{
    bool rootFile = keyLower.find('/') == std::string::npos;
    std::string coll = rootFile ? keyLower : collectionOf(keyLower);
    if (!g_collSeen.insert(coll).second) return;

    if (!rootFile) {
        // COLLECTIONS ARE NEVER CAPPED AND NEVER HIDDEN. There are only ever a handful (32
        // on the log this split was measured from) and they are the whole discovery
        // channel. Cheap, and load-bearing. Leave it alone.
        if (isPedCollection(coll))
            LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [overridable] -> tex_overrides/%s/",
                     coll.c_str(), classifyCollection(coll), coll.c_str());
        else if (isBlockedCollection(coll))
            LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [OTHER - never touched]",
                     coll.c_str(), classifyCollection(coll));
        else
            LOG_INFO(LogCategory::Collection, "Server collection: %-40s %-20s [depends on the file names inside] -> tex_overrides/%s/",
                     coll.c_str(), classifyCollection(coll), coll.c_str());
    }
    else if (isAllowedKey(keyLower)) {
        // A loose file we could actually take over, so it answers "what can I override
        // here". Every one is listed: looking up the exact name of one server prop is the
        // whole reason anyone reads this list, and the 500 cap used to block exactly that.
        LOG_INFO(LogCategory::Collection, "Server file:       %-40s [overridable, put yours in tex_overrides/]", coll.c_str());
    }
    else {
        // Refused on TYPE or PREFIX, never on a name we might one day learn: a .ymap will
        // never be a ped part, and a .ybn is collision. So unlike a refused COLLECTION there is
        // nothing to discover here, and the volume is not hypothetical: 24758 of 26337 map lines
        // on one real server (94%), peaking at 1624 in a single second. DEBUG, and counted for
        // the heartbeat.
        ++g_collOther;
        LOG_DEBUG(LogCategory::Collection, "Server file:       %-40s [OTHER - never touched]", coll.c_str());
    }
}

static uint32_t* regRawImpl(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    if (!asName) return o_regRaw(fileId, name, b1, asName, b2);
    // lowered once for everything below; this hook runs for every file a server streams
    const std::string key = lower(asName);

    waitForScan();
    {
        CsLock lk(g_cs);
        // capture the flag values a real streamed call uses
        if (!g_captured) { g_b1 = b1; g_b2 = b2; g_captured = true; LOG_DEBUG(LogCategory::Core, "Captured streaming flags: b1=%d b2=%d", (int)b1, (int)b2); }
        if (!g_off && !g_didRegister && storeAnswersNames()) { g_didRegister = true; claimAll(); }
        logServerName(key);
    }

    // redirect only exact-slot matches, and only for keys the gate allows (double guard).
    // g_bySlot gains entries at runtime (live reload), so the lookup takes the lock.
    if (!g_off && isAllowedKey(key)) {
        const char* redirect = nullptr;
        {
            CsLock lk(g_cs);
            auto it = g_bySlot.find(key);
            if (it != g_bySlot.end()) redirect = it->second;   // a leaked strdup: stable after release
        }
        if (redirect) {
            long n = InterlockedIncrement(&g_redirects);
            if (n <= 100)       LOG_INFO(LogCategory::Claim, "REDIRECT %s -> tex_overrides/%s", asName, rel(redirect));
            else if (n == 101)  LOG_INFO(LogCategory::Claim, "REDIRECT: 100 logged, the rest are counted but not listed");
            return o_regRaw(fileId, redirect, b1, asName, b2);
        }
    }
    return o_regRaw(fileId, name, b1, asName, b2);
}

// The game's own streaming thread calls this. Nothing may unwind out of here into game code, so
// a C++ exception in our half (out of memory in a map insert) hands the call through untouched.
// Every exception is thrown before regRawImpl forwards the call, so it is forwarded exactly once.
static uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    try { return regRawImpl(fileId, name, b1, asName, b2); }
    catch (...) { return o_regRaw(fileId, name, b1, asName, b2); }
}

// The budget table, the streaming manager and the live-reload functions are all optional, and
// none of them has to exist before the game's entry point runs. Only the hook does. So they are
// found out here on the beat thread. The hook waits on g_scanDone, which is not set until this
// and the file scan are both done, so it never sees half of this filled in.
static void locateRuntimePatterns()
{
    locateBudgetTable();
    const short PAT_MGR[] = { 0x74,0x1A,0x8B,0x15,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0x41 };
    if (uint8_t* q = scanModule(PAT_MGR)) {
        g_mgr = (StrMgr*)ripTarget(q + 11);
        LOG_INFO(LogCategory::Core, "Streaming manager @ %p", (void*)g_mgr);
    }
    else LOG_WARN(LogCategory::Core, "Streaming manager pattern NOT FOUND; overrides will register but cannot re-assert");
    locateRawStreamer();
}

// Order matters. The budget is decided before the scan, not after: on a big pack the scan runs
// for a long while, and for all of it the game would be stuck at its own texture ceiling. It
// cannot go any earlier than this, because decideBudget needs the table locateRuntimePatterns
// finds and the setting readBudgetFile resolves.
static void backgroundStartup()
{
    crashSaverStartup();
    locateRuntimePatterns();
    migrateSettings();   // first run writes the file; a marker file is absorbed and deleted
    readBudgetFile();
    decideBudget();      // DXGI only works out here, after DllMain has returned
    scanOverrides();
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
// This covers crashSaverStartup as well, and that is intended: a fault in there can leave the
// journal half processed, but g_scanDone is still set and the game still boots, which beats a
// hook waiting five minutes on a plugin that is already broken. The journal file is left where
// it is, so the next launch gets another go at it.
static void backgroundStartupSafe()
{
    bool ok = false;
    __try { ok = backgroundStartupCppSafe(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR(LogCategory::Core, "FAULT during background startup (code %08X); carrying on with available assets", GetExceptionCode());
    }
    if (!ok) LOG_WARN(LogCategory::Core, "Background startup did not finish; only pre-loaded assets active this session");
    if (g_scanDone) SetEvent(g_scanDone);
}

// The watcher normally starts on the first file the server streams, because that is when the
// hook fires and the plugin knows streaming is live. On a server that streams nothing at all the
// hook never fires, and then nothing here ever starts. This is the independent signal: FiveM says
// when the game has finished its first load, whatever the server did or did not send.
static void connectFirstLoad()
{
    void* ev = cfxSymbol("rage-nutsnbolts-five.dll", "?OnFirstLoadCompleted@@3V?$fwEvent@$$V@@A");
    if (!ev) return;
    cfxConnect(ev, [] {
        LOG_DEV(LogCategory::Core, "OnFirstLoadCompleted fired (idsReady=%ld)", (long)g_idsReady);
        InterlockedExchange(&g_firstLoadDone, 1);
        // Claiming happens inside the hook, so no hook means no claims. Say so rather than
        // leaving a session that looks healthy and replaced nothing.
        if (!g_off && !g_idsReady && !g_ovs.empty())
            LOG_WARN(LogCategory::Core, "Game finished loading but nothing has streamed through the hook yet, so no override has been claimed");
        return true;
    });
}

// Once streaming is live, start the live-reload watcher; before that there is nothing a change
// could apply to anyway. NEITHER signal is guaranteed to arrive, so there is a plain time fallback
// under them: g_idsReady is only ever set inside the hook, so a server that streams nothing never
// sets it, and OnFirstLoadCompleted is raised ONCE per process behind a bool (rage-nutsnbolts-five,
// the state handler at RVA 0xfbc0, flag at 0x2CDB5), so connecting to it after the game has
// already loaded means it never fires for us. That is what happened in the 2026-08-29 test: it
// fired for nobody, twice.
static void startWatcherIfDue(long& watchWait)
{
    if (g_watcherStarted || g_off) return;
    ++watchWait;
    if (!g_idsReady && !g_firstLoadDone && watchWait < 90) return;
    LOG_DEV(LogCategory::Live, "starting watcher after %ld tick(s) (idsReady=%ld firstLoad=%ld)",
            watchWait, (long)g_idsReady, (long)g_firstLoadDone);
    HANDLE w = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
    if (w) { g_watcherStarted = true; CloseHandle(w); }
    else LOG_WARN(LogCategory::Live, "Live reload: watcher thread could not start (err %lu), retrying next tick", GetLastError());
}

struct BeatState {
    int ours = 0, theirs = 0, loaded = 0;   // held / re-taken / resident, this tick
    long moved = 0;                         // slots whose name moved to another id
    size_t revalCursor = 0;                 // round robin over the name revalidation
    long heavyInMemory = 0;
};

// First time the game has this slot in memory: say whose file it read. A slot can be held all
// session and still show the game's texture if the load happened while the DLC mount owned the
// handle and refs then pinned the object (body skin blends AddRef their source txds).
static void noteFirstLoad(Ov& ov, uint32_t which, const StrEntry& e, BeatState& bs)
{
    ov.loadedSeen = 1;
    // The scan's cost audit reads page flags out of the file on disk, so it charges a drawable
    // nothing for the texture dictionaries it shares. Now that the slot is resident the game can
    // be asked what it really took. Texture memory is the physical figure; the virtual one is the
    // dependency walk. Reported apart because they are separate budgets.
    uint64_t memV = 0, memP = 0;
    slotMemoryCost(which, &memV, &memP);
    char cost[96] = "";
    if (memV || memP)
        _snprintf_s(cost, _TRUNCATE, " (%.1f MB texture, %.1f MB virtual with dependencies)", memP / 1048576.0, memV / 1048576.0);
    uint32_t readBy = ov.loadEdgeSeen ? ov.loadHandle : e.handle;
    // The game got there first. Holding the slot changes nothing while the object stays
    // resident, and the plugin must NOT drop it: 0.8.24 and 0.8.25 did (force_reload), which is
    // RemoveObject on clothing a ped is still drawing, and the game answered with ERR_GEN_PAGE_1
    // on every server that contests a slot. Cfx only runs that sequence when a resource is torn
    // down. So say whose copy it is and leave it alone.
    if (readBy != ov.handle)
        LOG_WARN(LogCategory::Claim, "LOADED: %s from the GAME file (handle %08x)%s; your file shows once the game reloads it, or after a restart", ov.slot, readBy, cost);
    else if (ov.loadEdgeSeen || !ov.reclaimedEarly)
        LOG_DEBUG(LogCategory::Claim, "LOADED: %s from your file%s", ov.slot, cost);
    else
        LOG_WARN(LogCategory::Claim, "LOADED: %s holds your file now%s, but it was reclaimed before the load was seen, so the game may have read its own copy", ov.slot, cost);
    if (memP >= (8ull << 20) && ++bs.heavyInMemory <= 20)
        LOG_WARN(LogCategory::Audit, "  HEAVY IN MEMORY: %s really costs %.1f MB of texture memory loaded (shrink it to fight texture loss)", ov.slot, memP / 1048576.0);
}

// Re-assert: DLC mounts and FiveM's loader re-point claimed slots after us; whoever writes the
// handle last wins, so write ours back. Same mechanism Cfx's own override path uses
// (LoadStreamingFile.cpp writes Entries[].handle directly). Lock held: live reload can append to
// g_ovs (the vector may reallocate) under us.
static void reassertBeat(BeatState& bs)
{
    CsLock lk(g_cs);
    bs.ours = bs.theirs = bs.loaded = 0;
    // The handle-match fast path below used to skip the name check entirely, so an index nobody
    // else writes was never validated again after the claim. A slot the store has since
    // re-pointed elsewhere then looks exactly like a healthy one for the rest of the session and
    // the heartbeat counts it as held. Re-check a slice each beat, round robin, so every override
    // is revalidated within a full sweep without putting a lookup per override per second under g_cs.
    // ponytail: fixed 32 per beat, so a 6,000 file pack sweeps in about 3 minutes.
    // Make it proportional if that ever needs to be faster.
    const size_t revalBudget = 32;
    const size_t ovCount = g_ovs.size();
    const size_t revalFrom = ovCount ? bs.revalCursor % ovCount : 0;
    if (ovCount) bs.revalCursor = (revalFrom + revalBudget) % ovCount;
    for (size_t ovN = 0; ovN < ovCount; ++ovN) {
        Ov& ov = g_ovs[ovN];
        const bool sliceHit = inSweepSlice(ovN, ovCount, revalFrom, revalBudget);
        if (ov.handle && ov.id == 0xFFFFFFFF) {
            uint32_t appeared = targetStreamingId(ov.slot);
            if (validStreamingId(appeared)) {
                ov.id = appeared;
                if (++g_lateBinds <= 60)
                    LOG_INFO(LogCategory::Claim, "LATE-BIND: %s (target id=%u raw handle=%08x)", ov.slot, ov.id, ov.handle);
            }
        }
        if (!ov.handle) continue;
        // Both indices matter: the one the claim landed on, and the one the store resolves the
        // NAME to. They are the same slot in the ordinary case.
        for (uint32_t which : { ov.id, ov.altId }) {
            if (which >= (uint32_t)g_mgr->numEntries) continue;
            StrEntry& e = g_mgr->entries[which];
            const uint32_t status = e.flags & 3;
            bool loaded = status == 1;
            if (loaded) ++bs.loaded;
            // Whose file the game reads is settled when the load STARTS. Sample the handle while
            // the load is in flight, so a RECLAIM landing between the load and the beat that
            // notices it cannot be reported as our win.
            if (status >= 2 && !ov.loadedSeen) { ov.loadHandle = e.handle; ov.loadEdgeSeen = 1; }
            if (loaded && !ov.loadedSeen) noteFirstLoad(ov, which, e, bs);
            const bool ours = e.handle == ov.handle;
            // Before writing back, check the slot still carries OUR name. FiveM frees a slot the
            // server created once its resource is gone (CfxCollection_RemoveStreamingTag sets
            // handle 0, the unload before a reconnect calls RemoveSlot), and the store hands that
            // index to the next file registered. Our handle written into it would point a
            // stranger's asset at our file. Only the index the NAME resolves to can be freed that
            // way, so altId, which is deliberately the index the name has ALREADY moved off, is
            // exempt. A lookup that fails for a reason other than "unknown name" says nothing
            // about the slot; skip the beat rather than guess. Checked whenever somebody else
            // wrote the slot, and on the round robin slice, because an untouched index needs
            // validating too.
            const bool phantom = (which == ov.altId && ov.altId != ov.id);
            if (!phantom && (!ours || sliceHit)) {
                int why = SLOT_OK;
                uint32_t byName = targetStreamingId(ov.slot, &why);
                if (byName != which) {
                    if (why != SLOT_OK && why != SLOT_NO_NAME) continue;
                    uint32_t& ref = (which == ov.altId) ? ov.altId : ov.id;
                    ref = validStreamingId(byName) ? byName : 0xFFFFFFFF;
                    if (++bs.moved <= 60)
                        LOG_INFO(LogCategory::Claim, "MOVED: %s no longer lives at id=%u (handle there %08x); %s", ov.slot, which, e.handle,
                                 ref == 0xFFFFFFFF ? "name unknown, waiting for it to come back" : "following the name to its new id");
                    continue;
                }
            }
            if (ours) { ++bs.ours; continue; }
            ++bs.theirs;
            if (status >= 2) continue;   // being requested/loaded right now; retry next tick
            uint32_t old = e.handle;
            e.handle = ov.handle;
            if (!ov.loadedSeen) ov.reclaimedEarly = 1;
            // Taking a slot back that is already resident is the same problem as the LOADED case
            // above: the handle is ours now, the pixels are still theirs.
            if (++g_reclaims <= 60) LOG_INFO(LogCategory::Claim, "RECLAIM: %s (id=%u, %08x -> %08x)%s", ov.slot, which, old, ov.handle,
                                              !loaded ? "" : " - already in memory from the game file, applies after reload");
        }
    }
}

static void beatLoop()
{
    BeatState bs;
    long watchWait = 0;                                // ticks spent waiting for a start signal
    long prevReclaims = 0, prevRedirects = 0, prevLateBinds = 0;
    int prevTheirs = 0;
    backgroundStartupSafe();   // all user-file reads, optional scans and the budget decision, off the loader lock
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            startWatcherIfDue(watchWait);
            if (g_off) continue;
            if (g_idsReady && g_mgr && g_mgr->entries) reassertBeat(bs);
            {
                CsLock lk(g_cs);
                placementBeatSafe();   // apply/re-assert tattoo placement edits
            }
            budgetBeat();              // re-assert the raised texture budget (aligned data writes)
        }

        long dReclaims = g_reclaims - prevReclaims;
        long dRedirects = (long)g_redirects - prevRedirects;
        long dLateBinds = g_lateBinds - prevLateBinds;
        bool changed = (dReclaims > 0 || dLateBinds > 0 || bs.theirs != prevTheirs);
        prevReclaims = g_reclaims;
        prevRedirects = (long)g_redirects;
        prevLateBinds = g_lateBinds;
        prevTheirs = bs.theirs;

        // "held" on its own was never the whole story: a slot can be held all session and still
        // show the game's texture.
        if (changed) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %ld reclaims (+%ld), %ld redirects (+%ld)",
                     beat, bs.ours, bs.theirs, g_reclaims, dReclaims, (long)g_redirects, dRedirects);
        } else if (beat % 4 == 0 || beat == 1) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %d in memory, %ld redirects, %ld other server files",
                     beat, bs.ours, bs.theirs, bs.loaded, (long)g_redirects, g_collOther);
        }
    }
}

DWORD WINAPI BeatLoop(LPVOID)
{
    // A thread that lets an exception out takes the whole process with it (std::terminate).
    // backgroundStartupSafe has already set g_scanDone by any path that reaches here.
    try { beatLoop(); }
    catch (...) { LOG_ERROR(LogCategory::Core, "Beat thread stopped (out of memory); claimed slots are no longer re-asserted this session"); }
    return 0;
}

// Runs synchronously inside asi-five's LoadLibrary call. FiveM loads plugins in
// LauncherInterface::PostLoadGame, which returns the game's entry point to the launcher; the
// entry point has NOT run yet, so no thread anywhere is executing game code. That makes the hook
// patch race-free by construction, with no thread freezing: the same guarantee FiveM relies on
// for its own HookFunction patches in that window.
static void Setup()
{
    std::string self = modulePath(g_self);
    size_t slash = self.find_last_of('\\');
    std::string plug = (slash == std::string::npos) ? self : self.substr(0, slash);
    std::string dir = plug + "\\tex_overrides\\";
    if (self.empty() || dir.size() >= sizeof g_overrideDir) {
        // Too deep to hold; there is no tex_overrides path to read the settings from either.
        g_off = true;
        if (!self.empty()) {
            logInit(plug + "\\texoverride.log");
            LOG_ERROR(LogCategory::Core, "The plugin folder path is too long (%zu bytes); plugin disabled. Move FiveM somewhere shorter.", dir.size());
        }
        return;
    }
    strcpy_s(g_overrideDir, dir.c_str());
    // a download the updater never finished; texoverride.asi.old is kept on purpose (rollback)
    deleteFile(self + ".download");
    // The settings are the one set of user files read here, under the loader lock, and that is
    // deliberate: `off` and `hide_overlay` decide whether there is a hook and an event
    // subscription at all, and both have to be made in this window. Reading a file is safe here;
    // loading a DLL or waiting on a thread is not. Everything else is read on the beat thread.
    loadSettings();   // never logs, the log does not exist yet
    g_off = g_set.off;
    // _off is the diagnostic control. Stop before logs, events, scans, hooks or worker threads
    // so it behaves like an installed but unloaded plugin rather than a bypass inside the hook.
    if (g_off) return;
    crashSaverInit();
    logInit(plug + "\\texoverride.log");
    InitializeCriticalSection(&g_cs);   // must exist before the hook can fire
    { time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
      char d[32]; strftime(d, sizeof d, "%Y-%m-%d", &tm);
      LOG_INFO(LogCategory::Core, "texoverride " TEXOVERRIDE_VERSION " initializing (%s, build %d)", d, runningGameBuild()); }
    // The hook waits on this. Every other user-file read and every optional pattern scan
    // happens on the beat thread (backgroundStartup), which sets it when it is done.
    g_scanDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    resolveOccupiedSlotExports();

    const short PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT);
    // g_off as well as the line: without it the beat, watcher and frame pump all start anyway,
    // and live reload calls an o_regRaw that was never set
    if (!p) { g_off = true; LOG_ERROR(LogCategory::Core, "Streaming hook pattern NOT FOUND; plugin disabled for this session"); }
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
                else LOG_INFO(LogCategory::Core, "LIVE: will register base overrides on first stream call");
            }
        }
    }

    // Both subscriptions to FiveM's own frame events happen HERE, on the loader thread, and that
    // timing is the whole safety argument. FiveM loads plugins from LauncherInterface::PostLoadGame,
    // which has not yet handed the game its entry point, so no game thread is running and no Cfx
    // component is inserting into these event lists. 0.8.14 connected from worker threads a second
    // or two later instead, which is the middle of the game's own init and exactly when Cfx
    // components subscribe; two unsynchronised splices into one list can drop a handler, and a
    // FiveM handler that quietly goes missing during load is a crash seconds later with nothing of
    // ours on the stack. Connecting this early also means OnFirstLoadCompleted can actually fire
    // for us, which it never did when we arrived after the first load had already finished.
    if (!g_off) {
        connectFramePump();
        connectFirstLoad();
        connectShotGate();
    }
}

static bool SetupCppSafe()
{
    try { Setup(); return true; }
    catch (...) { g_off = true; return false; }   // out of memory under the loader lock: degrade, never brick
}

// Setup runs inside LoadLibrary: if it faults, FiveM's asi loader shows "Couldn't load
// texoverride.asi" as a FATAL error and the game refuses to start until the file is deleted.
// A broken plugin must degrade to a do-nothing plugin, never brick the launch, so the fault is
// swallowed here and the beat/update threads are only started on success.
bool SetupSafe()
{
    // stack overflow is NOT swallowed: continuing on this thread with an unarmed guard page
    // would convert a clean load failure into an unattributable crash later in game code
    __try { return SetupCppSafe(); }
    __except ((GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR(LogCategory::Core, "FAULT during startup (code %08X); plugin disabled for this session", GetExceptionCode());
        g_off = true;
        return false;
    }
}
