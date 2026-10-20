#pragma once

#include "core/types.h"
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

// Only what more than one module really shares. Everything else is static in the file that owns it.

// UTF-8 paths are longer than the ANSI ones they replaced: a CJK letter is three bytes.
constexpr size_t kPathMax = 4 * MAX_PATH;

extern HINSTANCE g_self;
extern char g_overrideDir[kPathMax];   // tex_overrides\ beside the plugin, UTF-8, trailing backslash
extern bool g_off;

// The overrides and the name -> file map the hook redirects through. Both grow at runtime (live
// reload), so both are read and written under g_cs.
extern std::vector<Ov> g_ovs;
extern std::unordered_map<std::string, const char*> g_bySlot;   // values are leaked strdups
extern CRITICAL_SECTION g_cs;
extern HANDLE g_scanDone;   // set once background startup is done; the hook waits on it

extern StrMgr* g_mgr;
extern RegRaw_t o_regRaw;   // the original registerRawStreamingFile (MinHook trampoline)
extern bool g_b1;           // the flag values a real streamed call uses (constants in practice)
extern bool g_b2;

// g_cs (or any critical section) for exactly one scope. An exception thrown while holding it
// (a std::bad_alloc in a push_back) releases it on the way out instead of leaving the game's
// streaming thread waiting on it forever.
struct CsLock {
    CRITICAL_SECTION& cs;
    explicit CsLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
    ~CsLock() { LeaveCriticalSection(&cs); }
    CsLock(const CsLock&) = delete;
    CsLock& operator=(const CsLock&) = delete;
};
