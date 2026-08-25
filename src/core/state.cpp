#include "core/state.h"

HINSTANCE g_self = nullptr;
char g_logPath[MAX_PATH] = {};
char g_overrideDir[MAX_PATH] = {};
bool g_off = false;

std::vector<Ov> g_ovs;
HANDLE g_scanDone = nullptr;
volatile LONG g_waitLogged = 0;
std::unordered_map<std::string, const char*> g_bySlot;
std::unordered_set<std::string> g_collSeen;
std::unordered_set<std::string> g_quarantine;
bool g_crashSaverRan = false;
volatile LONG g_journalHot = 0;
char g_inflightPath[MAX_PATH] = {};
char g_quarantinePath[MAX_PATH] = {};

uint64_t g_costVirt = 0;
uint64_t g_costPhys = 0;
std::vector<std::pair<uint64_t, std::string>> g_costBig;

LogLevel g_minLogLevel = LogLevel::Info;
CRITICAL_SECTION g_logCs;
bool g_logCsInit = false;

StrMgr* g_mgr = nullptr;
volatile LONG g_regTotal = 0;
volatile LONG g_redirects = 0;
volatile LONG g_idsReady = 0;
long g_reclaims = 0;
long g_deferred = 0;
long g_lateBinds = 0;
long g_collListed = 0;
volatile long g_collOther = 0;
bool g_didRegister = false;
bool g_b1 = true;
bool g_b2 = false;
bool g_captured = false;
CRITICAL_SECTION g_cs;

std::vector<Cand> g_cands;

std::vector<PlColl> g_pl;
uint8_t** g_decorMgr = nullptr;
int32_t   g_decorCollOff = -1;
bool g_plLocated = false;
volatile LONG g_plFault = 0;

RegRaw_t o_regRaw = nullptr;
GetStreamingManager_t g_getStreamingManagerFn = nullptr;
GetStreamingModule_t g_getStreamingModuleFn = nullptr;
GetRawEntries_t g_getRawEntriesFn = nullptr;
std::unordered_map<std::string, int> g_slotWhyByExt;

GetRawStreamer_t g_getRawStreamerFn = nullptr;
RawGetEntry_t    g_rawGetEntryFn = nullptr;
bool g_watcherStarted = false;
std::deque<LiveOp> g_opQ;
volatile LONG g_opsPending = 0;
ULONGLONG g_journalClearAt = 0;
PeekMsg_t g_origPeek = nullptr;
DWORD g_pumpTid = 0;
std::unordered_map<std::string, Snap> g_snap;

uint64_t* g_vramTable = nullptr;
uint64_t  g_budget = 0;
double    g_budgetWant = -1.0;
uint64_t  g_budgetCurr = 0;
volatile LONG g_budgetFault = 0;
long g_budgetWrites = 0;
uint64_t g_vramTotal = 0;
uint64_t g_vramBudget = 0;
