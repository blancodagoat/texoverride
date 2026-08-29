#pragma once

#include "core/types.h"
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>

extern HINSTANCE g_self;
extern char g_logPath[MAX_PATH];
extern char g_overrideDir[MAX_PATH];
extern bool g_off;

extern std::vector<Ov> g_ovs;
extern HANDLE g_scanDone;
extern volatile LONG g_waitLogged;
extern std::unordered_map<std::string, const char*> g_bySlot;
extern std::unordered_set<std::string> g_collSeen;
extern std::unordered_set<std::string> g_quarantine;
extern bool g_crashSaverRan;
extern volatile LONG g_journalHot;
extern char g_inflightPath[MAX_PATH];
extern char g_quarantinePath[MAX_PATH];

extern uint64_t g_costVirt;
extern uint64_t g_costPhys;
extern std::vector<std::pair<uint64_t, std::string>> g_costBig;

extern LogLevel g_minLogLevel;
extern CRITICAL_SECTION g_logCs;
extern bool g_logCsInit;

extern StrMgr* g_mgr;
extern volatile LONG g_regTotal;
extern volatile LONG g_redirects;
extern volatile LONG g_idsReady;
extern volatile LONG g_firstLoadDone;   // FiveM said the game finished its first load
extern long g_reclaims;
extern long g_deferred;
extern long g_lateBinds;
// collection map: root files shown / counted only. Incremented under g_cs by the hook thread,
// read unlocked by the beat thread for the heartbeat, hence volatile: display only, and an
// aligned long cannot tear on x86-64.
extern long g_collListed;
extern volatile long g_collOther;
extern bool g_didRegister;
extern bool g_b1;
extern bool g_b2;
extern bool g_captured;
extern CRITICAL_SECTION g_cs;

extern std::vector<Cand> g_cands;

extern std::vector<PlColl> g_pl;
extern uint8_t** g_decorMgr;
extern int32_t   g_decorCollOff;
extern bool g_plLocated;
extern volatile LONG g_plFault;

extern RegRaw_t o_regRaw;
extern GetStreamingManager_t g_getStreamingManagerFn;
extern GetStreamingModule_t g_getStreamingModuleFn;
extern GetRawEntries_t g_getRawEntriesFn;
extern std::unordered_map<std::string, int> g_slotWhyByExt;

extern GetRawStreamer_t g_getRawStreamerFn;
extern RawGetEntry_t    g_rawGetEntryFn;
extern bool g_watcherStarted;
extern std::deque<LiveOp> g_opQ;
extern volatile LONG g_opsPending;
extern ULONGLONG g_journalClearAt;
extern PeekMsg_t g_origPeek;
extern DWORD g_pumpTid;
extern bool g_pumpReady;   // a main-thread pump is live, whichever of the two it is
extern bool g_framePumpConnected;   // connected to FiveM's OnMainGameFrame in Setup()
extern HANDLE g_refreshEvent;   // set by the refresh key, waited on by the watcher thread
extern std::unordered_map<std::string, Snap> g_snap;

extern uint64_t* g_vramTable;
extern uint64_t  g_budget;
extern double    g_budgetWant;
extern uint64_t  g_budgetCurr;
extern volatile LONG g_budgetFault;
extern long g_budgetWrites;
extern uint64_t g_vramTotal;
extern uint64_t g_vramBudget;
