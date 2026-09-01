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
#include "features/update.h"
#include "MinHook.h"
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>

uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2)
{
    InterlockedIncrement(&g_regTotal);
    // lowered once for both blocks below; this hook runs for every file a server streams
    const std::string keyLower = asName ? lower(asName) : std::string();

    if (asName)
    {
        // Outside the lock, deliberately: scanFinish takes g_cs to publish its results, so a
        // wait with the lock held would deadlock. This is the only place the game's own thread
        // ever waits on us, and it only ever waits once.
        // Bounded, and skipped entirely when the plugin is off: if the scan thread never got
        // started (a fault in Setup disables the plugin but leaves this hook live) an infinite
        // wait here would hang the game on a plugin that is already doing nothing.
        //
        // How long this waits IS the plugin's cost to startup, and it is the only number that
        // says whether moving the scan off DllMain bought anything: the scan's own duration does
        // not, because most of it overlaps with the game starting. Logged once.
        if (!g_off && g_scanDone) {
            ULONGLONG w0 = GetTickCount64();
            DWORD r = WaitForSingleObject(g_scanDone, 300000);
            ULONGLONG ms = GetTickCount64() - w0;
            if (!InterlockedExchange(&g_waitLogged, 1)) {
                if (r == WAIT_TIMEOUT)
                    LOG_WARN(LogCategory::Core, "File scan still running after 5 minutes; registering partial results");
                else if (ms >= 100)
                    LOG_INFO(LogCategory::Core, "Game ready for files %.1fs before scan finished; waited", ms / 1000.0);
                else
                    LOG_INFO(LogCategory::Core, "Scan finished before game needed it (no startup delay)");
            }
        }
        EnterCriticalSection(&g_cs);

        // capture the flag values a real streamed call uses
        if (!g_captured) { g_b1 = b1; g_b2 = b2; g_captured = true; LOG_DEBUG(LogCategory::Core, "Captured streaming flags: b1=%d b2=%d", (int)b1, (int)b2); }

        // once the stream system is live (first call), register our files as base slot overrides.
        // o_regRaw is the trampoline (original), so these calls do NOT re-enter this hook.
        if (!g_off && !g_didRegister)
        {
            g_didRegister = true;
            // the pool must exist by now (this very call registers into it); if it looks wrong,
            // the manager pattern matched the wrong code — better no re-assert than a wild write
            if (g_mgr && (!g_mgr->entries || g_mgr->numEntries <= 0 || g_mgr->numEntries > 10000000)) {
                LOG_WARN(LogCategory::Core, "Streaming pool looks invalid (entries=%p num=%d); re-assert disabled", (void*)g_mgr->entries, g_mgr->numEntries);
                g_mgr = nullptr;
            }
            int direct = 0, takeovers = 0, waiting = 0, rejected = 0, aliased = 0, shown = 0;
            for (auto& ov : g_ovs)
            {
                uint32_t id = 0xFFFFFFFF;
                // named in _inflight.txt for the duration of the call; if the game dies in
                // there, the next launch quarantines this key and boots without it
                InterlockedExchange(&g_journalHot, 1);
                journalOne(ov.slot);
                o_regRaw(&id, ov.gfile ? ov.gfile : ov.file, g_b1, ov.slot, g_b2);
                ov.id = id;
                if (g_mgr && g_mgr->entries && id < (uint32_t)g_mgr->numEntries)
                    ov.handle = g_mgr->entries[id].handle;
                int occupied = OCCUPIED_FAILED;
                if (id != 0xFFFFFFFF) {
                    ++direct;
                    // A claim that SUCCEEDS is not proof we took the slot over. For a name
                    // the game already owns, registerRawStreamingFile can mint a brand new
                    // index and hand that back, while the store keeps resolving the name to
                    // its original one. The handle then lands in a slot nothing looks up and
                    // the log reads perfectly while nothing changes in game (seen on .ycd:
                    // seven scattered vanilla dictionaries came back with seven CONSECUTIVE
                    // ids, which existing indices could never be). So ask the store what the
                    // name really resolves to, and pin that entry as well.
                    int why = SLOT_OK;
                    uint32_t tgt = targetStreamingId(ov.slot, &why);
                    noteSlotWhy(ov.slot, why);
                    if (validStreamingId(tgt) && tgt != id) { ov.altId = tgt; ++aliased; }
                }
                else {
                    occupied = recoverOccupiedSlot(ov);
                    if (occupied == OCCUPIED_ATTACHED) ++takeovers;
                    else if (occupied == OCCUPIED_WAITING) ++waiting;
                    else ++rejected;
                }
                if (g_minLogLevel == LogLevel::Debug || ++shown <= 10) {
                    if (id != 0xFFFFFFFF && ov.altId != 0xFFFFFFFF)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x; store resolves to id=%u, pinning both)", ov.slot, rel(ov.file), id, ov.handle, ov.altId);
                    else if (id != 0xFFFFFFFF)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-REG: %s <- tex_overrides/%s (id=%u handle=%08x)", ov.slot, rel(ov.file), id, ov.handle);
                    else if (occupied == OCCUPIED_ATTACHED)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-TAKEOVER: %s <- tex_overrides/%s (id=%u raw handle=%08x)", ov.slot, rel(ov.file), ov.id, ov.handle);
                    else if (occupied == OCCUPIED_WAITING)
                        LOG_INFO(LogCategory::Claim, "OVERRIDE-WAIT: %s <- tex_overrides/%s (raw handle=%08x; target not present yet)", ov.slot, rel(ov.file), ov.handle);
                    else
                        LOG_ERROR(LogCategory::Claim, "OVERRIDE-FAILED: %s <- tex_overrides/%s (registration rejected; no usable raw entry)", ov.slot, rel(ov.file));
                }
            }
            InterlockedExchange(&g_journalHot, 0);
            DeleteFileA(g_inflightPath);   // whole loop survived; nothing to quarantine
            if (g_ovs.size() > 10 && g_minLogLevel != LogLevel::Debug) {
                LOG_INFO(LogCategory::Claim, "  ...and %zu more override(s) registered directly", g_ovs.size() - 10);
            }
            LOG_INFO(LogCategory::Claim, "Claimed %d base-slot override(s): %d direct, %d occupied-slot takeover, %d waiting for target, %d rejected",
                     direct + takeovers + waiting, direct, takeovers, waiting, rejected);
            if (aliased) LOG_INFO(LogCategory::Claim, "%d of those named a slot the game already owned under a different id; pinned both", aliased);

            // Per type summary
            { std::unordered_map<std::string, int> byExt, aliasByExt, deadByExt;
              for (auto& ov : g_ovs) {
                  const char* dot = strrchr(ov.slot, '.');
                  std::string ext = dot && dot[1] ? lower(dot + 1) : std::string("(none)");
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
                                 ov.handle, g_mgr->entries[ov.id < (uint32_t)g_mgr->numEntries ? ov.id : 0].handle);
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
                          LOG_WARN(LogCategory::Verify, "  ALREADY IN MEMORY: %s (status=%s, strflags=%04x, deps=%u) — game will reload when re-equipped",
                                   ov.slot, strStatusText(f), (unsigned)(f >> 16), (unsigned)((f >> 2) & 0x3FFF));
                  }
                  LOG_INFO(LogCategory::Verify, "Residency at claim time: %d resident in memory, %d loading, %d cold, %d persistent",
                           resident, pending, cold, pinned); }
            }
            if (g_mgr) LOG_DEBUG(LogCategory::Verify, "Streaming pool: entries=%p numEntries=%d", (void*)g_mgr->entries, g_mgr->numEntries);
            InterlockedExchange(&g_idsReady, 1);
        }

        // MAP: one line per distinct thing the server streams. This is the discovery channel:
        // names we REFUSE have to appear too, or a refused collection reads exactly like one the
        // server never streamed, and a user's log is the only way we ever learn about a naming
        // family the gate does not know yet. That is how the folder whitelist died in 0.8.5.
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
        bool rootFile = keyLower.find('/') == std::string::npos;
        std::string coll = rootFile ? keyLower : collectionOf(keyLower);

        if (g_collSeen.insert(coll).second) {
            if (!rootFile) {
                // COLLECTIONS ARE NEVER CAPPED AND NEVER HIDDEN. There are only ever a handful (32
                // on the log this split was measured from) and they are the whole discovery
                // channel: a refused collection that is not logged reads exactly like one the
                // server never streamed, and a user's log is the only way a naming family the gate
                // does not know yet ever reaches us. That is how caninesd killed the folder
                // whitelist in 0.8.5. Cheap, and load-bearing. Leave it alone.
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
                // here". Worth listing, but a big server streams enough of them to be worth a cap,
                // and THIS is the line the 500 cap was always meant for.
                // debug lifts it: the one job the cap gets in the way of is looking up the exact
                // name of a specific server prop, which is the whole reason to read this list, and
                // a player who has gone and turned debug on has asked for the long version.
                if (++g_collListed <= 500 || g_set.debug)
                    LOG_INFO(LogCategory::Collection, "Server file:       %-40s [overridable, put yours in tex_overrides/]", coll.c_str());
                else if (g_collListed == 501)
                    LOG_WARN(LogCategory::Collection, "500 overridable server files listed; the rest are counted only - set debug = yes in _settings.txt to list them all");
            }
            else {
                // Refused on TYPE or PREFIX, never on a name we might one day learn: a .ymap will
                // never be a ped part, and a .ybn is collision. So unlike
                // a refused COLLECTION there is nothing to discover here, and the volume is not
                // hypothetical: 24758 of 26337 map lines on one real server (94%), peaking at 1624
                // in a single second. Every one was an fopen/fprintf/fclose inside g_logCs while
                // this thread also held g_cs and the game's main thread sat waiting on g_cs in
                // drainOps. Count them, say so once, and put the names behind _debug.txt.
                if (++g_collOther == 1)
                    LOG_INFO(LogCategory::Collection, "Other server files (vehicle, prop and map data) are counted, not listed - set debug = yes in _settings.txt to see them");
                LOG_DEBUG(LogCategory::Collection, "Server file:       %-40s [OTHER - never touched]", coll.c_str());
            }
        }

        LeaveCriticalSection(&g_cs);
    }

    // redirect only exact-slot matches, and only for keys the gate allows (double guard):
    // freemode-ped collection slots, or bare-name .ytd dictionaries.
    // g_bySlot can gain entries at runtime now (live reload), so the lookup takes the lock.
    if (!g_off && asName)
    {
        const std::string& key = keyLower;
        if (isAllowedKey(key))
        {
            const char* redirect = nullptr;
            EnterCriticalSection(&g_cs);
            auto it = g_bySlot.find(key);
            if (it != g_bySlot.end()) redirect = it->second;   // value is a leaked strdup — stable after release
            LeaveCriticalSection(&g_cs);
            if (redirect)
            {
                long n = InterlockedIncrement(&g_redirects);
                if (n <= 100)       LOG_INFO(LogCategory::Claim, "REDIRECT %s -> tex_overrides/%s", asName, rel(redirect));
                else if (n == 101)  LOG_INFO(LogCategory::Claim, "REDIRECT: 100 logged, the rest are counted but not listed");
                return o_regRaw(fileId, redirect, b1, asName, b2);
            }
        }
    }
    return o_regRaw(fileId, name, b1, asName, b2);
}

// The budget table, the streaming manager and the live-reload functions are all optional, and
// none of them has to exist before the game's entry point runs. Only the hook does. So they are
// found out here on the beat thread. The hook waits on g_scanDone, which is not set until this
// and the file scan are both done, so it never sees half of this filled in.
void locateRuntimePatterns()
{
    const short PAT_VRAM[] = { 0x4C,0x63,0xC0,0x48,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x14 };
    uint8_t* vram = scanModule(PAT_VRAM, 13);
    uint64_t* table = vram ? (uint64_t*)ripTarget(vram + 6) : nullptr;
    if (table && vramTableSane(table, &g_budgetCurr)) g_vramTable = table;
    else LOG_WARN(LogCategory::Audit, "Texture budget: VRAM table %s; budget adjustment disabled",
                  vram ? "failed sanity check" : "pattern NOT FOUND");

    const short PAT_MGR[] = { 0x74,0x1A,0x8B,0x15,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0x41 };
    if (uint8_t* q = scanModule(PAT_MGR, 16)) {
        g_mgr = (StrMgr*)ripTarget(q + 11);
        LOG_INFO(LogCategory::Core, "Streaming manager @ %p", (void*)g_mgr);
    }
    else LOG_WARN(LogCategory::Core, "Streaming manager pattern NOT FOUND — overrides will register but cannot re-assert");

    const short PAT_GRS[] = { 0x48,0x8B,0xD3,0x4C,0x8B,0x00,0x48,0x8B,0xC8,0x41,0xFF,0x90,-1,0x01,0x00,0x00,0x8B,0xD8,0xE8 };
    if (uint8_t* q = scanModule(PAT_GRS, 19)) {
        if (q[-5] == 0xE8) g_getRawStreamerFn = (GetRawStreamer_t)ripTarget(q - 4);
    }
    const short PAT_GE[] = { 0x0F,0xB7,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x8B,0xD0,0x25,0xFF };
    if (uint8_t* q = scanModule(PAT_GE, 12)) g_rawGetEntryFn = (RawGetEntry_t)(q - 0x14);
    LOG_INFO(LogCategory::Live, "Occupied-slot exports: rawStreamer=%s getEntry=%s",
             g_getRawStreamerFn ? "ok" : "MISSING", g_rawGetEntryFn ? "ok" : "MISSING");
}

// Order matters. The budget is decided before the scan, not after: on a big pack the scan runs
// for a long while, and for all of it the game would be stuck at its own texture ceiling. It
// cannot go any earlier than this, because decideBudget needs the table locateRuntimePatterns
// finds and the number _budget.txt holds.
void backgroundStartup()
{
    crashSaverStartup();
    g_crashSaverRan = true;
    locateRuntimePatterns();
    migrateSettings();   // first run writes the file; a marker file is absorbed and deleted
    readBudgetFile();
    decideBudget();   // DXGI only works out here, after DllMain has returned
    walkDir(std::string(g_overrideDir), "", g_cands);
    LOG_INFO(LogCategory::Scan, "Discovered %zu candidate file(s); reading headers...", g_cands.size());
    scanFinish();
    loadPlacementFiles();
}

bool backgroundStartupCppSafe()
{
    try { backgroundStartup(); return true; }
    catch (const std::exception& e) { LOG_ERROR(LogCategory::Core, "Background startup failed: %s", e.what()); }
    catch (...) { LOG_ERROR(LogCategory::Core, "Background startup failed with an unknown C++ exception"); }
    return false;
}

// SEH so a fault in background startup cannot leave the hook waiting on an event nobody will
// ever set. (No C++ objects in this frame, which is what makes __try legal; they live in the
// helper. Own function: SEH inside BeatLoop's infinite loop confuses MSVC's return analysis.)
// This now covers crashSaverStartup as well, and that is intended: a fault in there can leave
// the journal half processed, but g_scanDone is still set and the game still boots, which beats
// a hook waiting five minutes on a plugin that is already broken. The journal file is left where
// it is, so the next launch gets another go at it.
void scanFinishSafe()
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

DWORD WINAPI BeatLoop(LPVOID)
{
    int beatOurs = 0, beatTheirs = 0, beatLoaded = 0;   // ours / re-taken / resident right now
    long watchWait = 0;                                // ticks spent waiting for a start signal
    long heavyInMemory = 0;
    long prevReclaims = 0, prevRedirects = 0, prevLateBinds = 0;
    int prevTheirs = 0;
    scanFinishSafe();   // all user-file reads, optional scans and the budget decision, off the loader lock
    for (int beat = 1;; ++beat) {
        for (int tick = 0; tick < 15; ++tick) {
            Sleep(1000);
            // once streaming is live, start the live-reload watcher — before that there is
            // nothing a change could apply to anyway.
            // NEITHER signal is guaranteed to arrive, so there is a plain time fallback under
            // them: g_idsReady is only ever set inside the hook, so a server that streams nothing
            // never sets it, and OnFirstLoadCompleted is raised ONCE per process behind a bool
            // (rage-nutsnbolts-five, the state handler at RVA 0xfbc0, flag at 0x2CDB5), so
            // connecting to it after the game has already loaded means it never fires for us.
            // That is what happened in the 2026-08-29 test: it fired for nobody, twice.
            if (!g_watcherStarted && !g_off) {
                ++watchWait;
                if (g_idsReady || g_firstLoadDone || watchWait >= 90) {
                    LOG_DEV(LogCategory::Live, "starting watcher after %ld tick(s) (idsReady=%ld firstLoad=%ld)",
                            watchWait, (long)g_idsReady, (long)g_firstLoadDone);
                    HANDLE w = CreateThread(nullptr, 0, WatchLoop, nullptr, 0, nullptr);
                    if (w) { g_watcherStarted = true; CloseHandle(w); }
                    else LOG_WARN(LogCategory::Live, "Live reload: watcher thread could not start (err %lu), retrying next tick", GetLastError());
                }
            }
            // re-assert: DLC mounts and FiveM's loader re-point claimed slots after us; whoever
            // writes the handle last wins, so write ours back. Same mechanism Cfx's own override
            // path uses (LoadStreamingFile.cpp writes Entries[].handle directly).
            // Lock held: the watcher can append to g_ovs (vector may reallocate) and swap g_pl
            // entries under us.
            if (!g_off && g_idsReady && g_mgr && g_mgr->entries) {
                EnterCriticalSection(&g_cs);
                beatOurs = beatTheirs = beatLoaded = 0;
                for (auto& ov : g_ovs) {
                    if (ov.handle && ov.id == 0xFFFFFFFF) {
                        uint32_t appeared = targetStreamingId(ov.slot);
                        if (validStreamingId(appeared)) {
                            ov.id = appeared;
                            if (++g_lateBinds <= 60)
                                LOG_INFO(LogCategory::Claim, "LATE-BIND: %s (target id=%u raw handle=%08x)", ov.slot, ov.id, ov.handle);
                        }
                    }
                    if (!ov.handle) continue;
                    // Both indices matter: the one the claim landed on, and the one the store
                    // resolves the NAME to. They are the same slot in the ordinary case.
                    for (uint32_t which : { ov.id, ov.altId }) {
                        if (which >= (uint32_t)g_mgr->numEntries) continue;
                        StrEntry& e = g_mgr->entries[which];
                        bool loaded = (e.flags & 3) == 1;
                        if (loaded) ++beatLoaded;
                        // First time the game has this slot in memory: say whose file it read.
                        // A slot can be held all session and still show the game's texture if
                        // the load happened while the DLC mount owned the handle and refs then
                        // pinned the object (body skin blends AddRef their source txds).
                        if (loaded && !ov.loadedSeen) {
                            ov.loadedSeen = 1;
                            // The scan's cost audit reads page flags out of the file on disk, so it
                            // charges a drawable nothing for the texture dictionaries it shares.
                            // Now that the slot is resident the game can be asked what it really
                            // took, dependencies included.
                            uint64_t mem = slotMemoryCost(which);
                            char cost[64] = "";
                            if (mem) _snprintf_s(cost, _TRUNCATE, " (%.1f MB in memory with dependencies)", mem / 1048576.0);
                            if (e.handle == ov.handle) LOG_DEBUG(LogCategory::Claim, "LOADED: %s from your file%s", ov.slot, cost);
                            else LOG_WARN(LogCategory::Claim, "LOADED: %s from the GAME file (handle %08x)%s; your file shows only after the game drops and reloads it", ov.slot, e.handle, cost);
                            if (mem >= (8ull << 20) && ++heavyInMemory <= 20)
                                LOG_WARN(LogCategory::Audit, "  HEAVY IN MEMORY: %s really costs %.1f MB loaded (shrink it to fight texture loss)", ov.slot, mem / 1048576.0);
                        }
                        if (e.handle == ov.handle) { ++beatOurs; continue; }
                        ++beatTheirs;
                        if ((e.flags & 3) >= 2) { ++g_deferred; continue; }   // being requested/loaded right now; retry next tick
                        uint32_t old = e.handle;
                        e.handle = ov.handle;
                        if (++g_reclaims <= 60) LOG_INFO(LogCategory::Claim, "RECLAIM: %s (id=%u, %08x -> %08x)%s", ov.slot, which, old, ov.handle,
                                                          loaded ? " - already in memory from the game file, applies after reload" : "");
                    }
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

        long dReclaims = g_reclaims - prevReclaims;
        long dRedirects = (long)g_redirects - prevRedirects;
        long dLateBinds = g_lateBinds - prevLateBinds;
        bool changed = (dReclaims > 0 || dLateBinds > 0 || beatTheirs != prevTheirs);
        prevReclaims = g_reclaims;
        prevRedirects = (long)g_redirects;
        prevLateBinds = g_lateBinds;
        prevTheirs = beatTheirs;

        if (changed) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %ld reclaims (+%ld), %ld redirects (+%ld)",
                     beat, beatOurs, beatTheirs, g_reclaims, dReclaims, (long)g_redirects, dRedirects);
        } else if (beat % (g_minLogLevel == LogLevel::Debug ? 4 : 20) == 0 || beat == 1) {
            LOG_INFO(LogCategory::Core, "Heartbeat (beat %d): %d held, %d contested, %d in memory, %ld redirects, %ld other server files",
                     beat, beatOurs, beatTheirs, beatLoaded, (long)g_redirects, g_collOther);
        }
    }
}

// Runs synchronously inside asi-five's LoadLibrary call. FiveM loads plugins in
// LauncherInterface::PostLoadGame, which returns the game's entry point to the launcher — the
// entry point has NOT run yet, so no thread anywhere is executing game code. That makes the hook
// patch race-free by construction, with no thread freezing: the same guarantee FiveM relies on
// for its own HookFunction patches in that window. (MinHook's freeze never worked under FiveM
// anyway — CreateToolhelp32Snapshot is blocked — so before this it was safe only by luck.)
void Setup()
{
    char self[MAX_PATH]; GetModuleFileNameA(g_self, self, MAX_PATH);
    std::string dir = self; size_t slash = dir.find_last_of('\\');
    std::string plug = (slash==std::string::npos) ? dir : dir.substr(0, slash);
    _snprintf_s(g_logPath, MAX_PATH, _TRUNCATE, "%s\\texoverride.log", plug.c_str());
    _snprintf_s(g_overrideDir, MAX_PATH, _TRUNCATE, "%s\\tex_overrides\\", plug.c_str());
    // a download the updater never finished; texoverride.asi.old is kept on purpose (rollback)
    DeleteFileA((std::string(self) + ".download").c_str());
    loadSettings();   // marker files plus _settings.txt; never logs, the log does not exist yet
    g_off = g_set.off;
    // _off is the diagnostic control. Stop before logs, events, scans, hooks or worker threads
    // so it behaves like an installed but unloaded plugin rather than a bypass inside the hook.
    if (g_off) return;
    _snprintf_s(g_inflightPath,   MAX_PATH, _TRUNCATE, "%s_inflight.txt",   g_overrideDir);
    _snprintf_s(g_quarantinePath, MAX_PATH, _TRUNCATE, "%s_quarantine.txt", g_overrideDir);
    InitializeCriticalSection(&g_logCs);
    g_logCsInit = true;
    InitializeCriticalSection(&g_cs);   // must exist before the hook can fire

    if (g_set.debug) g_minLogLevel = LogLevel::Debug;

    // fresh log every launch, but keep one previous generation: after a crash the next launch
    // used to destroy the exact log that showed what the crashed session was doing
    { char oldLog[MAX_PATH + 8];
      _snprintf_s(oldLog, _TRUNCATE, "%s.old", g_logPath);
      if (!MoveFileExA(g_logPath, oldLog, MOVEFILE_REPLACE_EXISTING))
          DeleteFileA(g_logPath);   // rotation blocked (file held open): keep "fresh log" true
    }
    { time_t t = time(nullptr); struct tm tm; localtime_s(&tm, &t);
      char d[32]; strftime(d, sizeof d, "%Y-%m-%d", &tm);
      LOG_INFO(LogCategory::Core, "texoverride " TEXOVERRIDE_VERSION " initializing (%s, build %d)", d, runningGameBuild()); }
    // The hook waits on this. Every user-file read and every optional pattern scan now happens
    // on the beat thread (backgroundStartup), which sets it when it is done.
    g_scanDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    resolveOccupiedSlotExports();
    LOG_INFO(LogCategory::Core, "Occupied-slot exports: manager=%s module=%s rawEntries=%s",
         g_getStreamingManagerFn ? "ok" : "MISSING",
         g_getStreamingModuleFn ? "ok" : "MISSING",
         g_getRawEntriesFn ? "ok" : "MISSING");

    const short PAT[] = { 0xB2,0x01,0x48,0x8B,0xCD,0x45,0x8A,0xE0,0x4D,0x0F,0x45,0xF9,0xE8 };
    uint8_t* p = scanModule(PAT, sizeof PAT / sizeof *PAT);
    if (!p) { LOG_ERROR(LogCategory::Core, "Streaming hook pattern NOT FOUND; plugin disabled for this session"); }
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
                else LOG_INFO(LogCategory::Core, g_off ? "Hooked, disabled" : "LIVE — will register base overrides on first stream call");
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
        g_framePumpConnected = connectFramePump();
        connectFirstLoad();
        connectShotGate();
    }
}

// Setup runs inside LoadLibrary: if it faults, FiveM's asi loader shows "Couldn't load
// texoverride.asi" as a FATAL error and the game refuses to start until the file is deleted.
// A broken plugin must degrade to a do-nothing plugin, never brick the launch, so the fault is
// swallowed here and the beat/update threads are only started on success.
bool SetupSafe()
{
    // stack overflow is NOT swallowed: continuing on this thread with an unarmed guard page
    // would convert a clean load failure into an unattributable crash later in game code
    __try { Setup(); return true; }
    __except ((GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR(LogCategory::Core, "FAULT during startup (code %08X) — plugin disabled for this session", GetExceptionCode());
        g_off = true;
        return false;
    }
}
