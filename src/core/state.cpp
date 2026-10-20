#include "core/state.h"

HINSTANCE g_self = nullptr;
char g_overrideDir[kPathMax] = {};
bool g_off = false;

std::vector<Ov> g_ovs;
std::unordered_map<std::string, const char*> g_bySlot;
CRITICAL_SECTION g_cs;
HANDLE g_scanDone = nullptr;

StrMgr* g_mgr = nullptr;
RegRaw_t o_regRaw = nullptr;
bool g_b1 = true;
bool g_b2 = false;
