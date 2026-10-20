#include "streaming/budget.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/settings.h"
#include "core/cfx.h"
#include "core/pattern.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cctype>

static uint64_t* g_vramTable = nullptr;    // FiveM's budget table in GTA5.exe; null = not found or not sane
static uint64_t  g_budget = 0;             // what we hold the table at; 0 = leave it alone
static double    g_budgetWant = -1.0;      // from the setting: -1 auto, 0 game, else GB
static uint64_t  g_budgetCurr = 0;         // what FiveM had set when we first looked
static volatile LONG g_budgetFault = 0;
static long g_budgetWrites = 0;
static uint64_t g_vramTotal = 0;
static uint64_t g_vramBudget = 0;

static bool vramTableSane(uint64_t* t, uint64_t* cur)   // refuse to write unless it looks like Cfx filled it
{
    __try {
        for (int i = 0; i < 80; i += 4) {
            uint64_t full = t[i + 3];       // rows are half / 1.5th / full / full of the budget
            if (full != t[i + 2] || full < (1ull << 30) || full > (48ull << 30) || t[i] >= full)
                return false;
        }
        *cur = t[3];                        // what FiveM set: 3e9 x (vid_budgetScale/12 + 1)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void locateBudgetTable()
{
    const short PAT_VRAM[] = { 0x4C,0x63,0xC0,0x48,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x14 };
    uint8_t* vram = scanModule(PAT_VRAM);
    uint64_t* table = vram ? (uint64_t*)ripTarget(vram + 6) : nullptr;
    if (table && vramTableSane(table, &g_budgetCurr)) g_vramTable = table;
    else LOG_WARN(LogCategory::Audit, "Texture budget: VRAM table %s; budget adjustment disabled",
                  vram ? "failed sanity check" : "pattern NOT FOUND");
}

// One DXGI probe, two numbers, both 0 when unknowable:
//   g_vramTotal  — the card's physical VRAM (biggest hardware adapter)
//   g_vramBudget — what Windows is willing to give THIS process right now. WDDM works it out
//                  itself, so it already subtracts the desktop, the browser, and anything else
//                  on the GPU. Microsoft is blunt about the other side of it: a process that
//                  runs past its budget "will likely experience stuttering, as they are
//                  intermittently frozen and paged-out". That is the exact failure the old
//                  opt-in rule was guarding against, so this is the number to size against
//                  rather than a fraction guessed from the card's sticker capacity.
static void probeVram()
{
    // dxgi loaded by FULL System32 path: a static import (or a bare-name load) binds to any
    // already-loaded ReShade/ENB proxy dxgi.dll, and a proxy missing CreateDXGIFactory1 used
    // to fail the load of this whole plugin. The full path always gets Windows' own copy.
    typedef HRESULT (WINAPI* CreateFactory_t)(REFIID, void**);
    wchar_t dxPath[MAX_PATH + 16];
    UINT sl = GetSystemDirectoryW(dxPath, MAX_PATH);
    if (sl == 0 || sl >= MAX_PATH) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot locate System32"); return; }
    wcscat_s(dxPath, L"\\dxgi.dll");
    HMODULE dx = LoadLibraryW(dxPath);
    if (!dx) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot load %s (err %lu)", narrow(dxPath).c_str(), GetLastError()); return; }
    auto createFactory = (CreateFactory_t)GetProcAddress(dx, "CreateDXGIFactory1");
    if (!createFactory) { LOG_WARN(LogCategory::Audit, "Texture budget: dxgi.dll has no CreateDXGIFactory1"); return; }

    IDXGIFactory1* f = nullptr;
    HRESULT hr = createFactory(__uuidof(IDXGIFactory1), (void**)&f);
    if (FAILED(hr) || !f) { LOG_WARN(LogCategory::Audit, "Texture budget: CreateDXGIFactory1 failed (hr 0x%08lX)", (unsigned long)hr); return; }
    IDXGIAdapter1* a = nullptr; IDXGIAdapter1* best = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > g_vramTotal) {
            g_vramTotal = d.DedicatedVideoMemory;
            if (best) best->Release();
            best = a; continue;
        }
        a->Release();
    }
    if (!best) LOG_WARN(LogCategory::Audit, "Texture budget: DXGI listed no hardware adapter");
    if (best) {
        IDXGIAdapter3* a3 = nullptr;   // DXGI 1.4, so Windows 10 and up; older just keeps 0 here
        if (SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO vm = {};
            if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm)))
                g_vramBudget = vm.Budget;
            a3->Release();
        }
        best->Release();
    }
    f->Release();
}

static uint64_t autoBudget(uint64_t current)
{
    uint64_t cap = g_vramBudget;
    if (!cap || (g_vramTotal && cap > g_vramTotal)) cap = g_vramTotal;   // Budget can span shared RAM
    return budgetFor(cap, current);
}

static void budgetBeatImpl()
{
    // Our value is standing, or FiveM has since set a higher one (the player raised the slider).
    // Writing ours over that would lower the budget, which decideBudget refuses to do as well.
    if (g_vramTable[3] >= g_budget) return;
    uint64_t old = g_vramTable[3];
    for (int i = 0; i < 80; i += 4) {         // same rows, same ratios as Cfx's own writer
        g_vramTable[i + 3] = g_budget;
        g_vramTable[i + 2] = g_budget;
        g_vramTable[i + 1] = (uint64_t)(g_budget / 1.5);
        g_vramTable[i]     = g_budget / 2;
    }
    if (++g_budgetWrites <= 10)
        LOG_DEBUG(LogCategory::Audit, "Texture budget re-asserted: %.1f -> %.1f GB%s", old / 1073741824.0, g_budget / 1073741824.0,
                  g_budgetWrites == 1 ? "" : " (re-asserted; settings screen rewrote it)");
}

static void decideBudgetImpl()
{
    if (!g_vramTable) return;              // nothing to write into; locateBudgetTable already said so
    if (g_budgetWant == 0.0) return;       // texture_budget said leave it alone
    probeVram();
    if (g_budgetWant > 0.0) {
        g_budget = (uint64_t)(g_budgetWant * 1073741824.0);
        if (g_vramTotal && g_budget > g_vramTotal) {
            LOG_WARN(LogCategory::Audit, "Texture budget: Requested %.1f GB exceeds card's %.1f GB VRAM; clamped",
                     g_budget / 1073741824.0, g_vramTotal / 1073741824.0);
            g_budget = g_vramTotal;
        }
        if (g_budget <= g_budgetCurr) {    // lowering it would only make texture loss worse
            LOG_INFO(LogCategory::Audit, "Texture budget: Requested %.1f GB is <= game's %.1f GB; left unchanged",
                     g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
            g_budget = 0;
        }
        else LOG_INFO(LogCategory::Audit, "Texture budget: Settings asked for %.1f GB (up from %.1f GB)",
                      g_budget / 1073741824.0, g_budgetCurr / 1073741824.0);
        return;
    }
    g_budget = autoBudget(g_budgetCurr);
    if (g_budget)
        LOG_INFO(LogCategory::Audit, "Texture budget: Sized to this PC — %.1f GB, up from %.1f GB (card: %.1f GB, Windows offering: %.1f GB)",
                 g_budget / 1073741824.0, g_budgetCurr / 1073741824.0,
                 g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
    else if (!g_vramTotal && !g_vramBudget)
        LOG_WARN(LogCategory::Audit, "Texture budget: Could not query card memory; left at game default (%.1f GB)", g_budgetCurr / 1073741824.0);
    else
        LOG_INFO(LogCategory::Audit, "Texture budget: Game default %.1f GB is optimal for this GPU (card: %.1f GB, Windows offering: %.1f GB)",
                 g_budgetCurr / 1073741824.0, g_vramTotal / 1073741824.0, g_vramBudget / 1073741824.0);
}

void decideBudget()
{
    __try { decideBudgetImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_budget = 0;
        LOG_WARN(LogCategory::Audit, "Texture budget: Fault reading card memory (code %08X); left at game default",
                 (unsigned)GetExceptionCode());
    }
}

void budgetBeat()
{
    if (!g_budget || g_budgetFault || !g_vramTable) return;
    __try { budgetBeatImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_budgetFault, 1);
        LOG_ERROR(LogCategory::Audit, "Texture budget: Fault writing budget table; raised budget disabled for session");
    }
}

// ===================== what the game is actually using (rage-graphics-five) =====================
// The DXGI probe above can only say what Windows is willing to hand this process. This says what
// the game has actually spent, because grcResourceCache keeps the running total itself and Cfx
// exports the getter. Texture loss IS this number reaching the ceiling (eviction inside GTA5.exe
// is passive-only, cfx #3874), so a log line saying how close it got turns "my textures went
// missing" into something a log can answer instead of a guess about the player's card.
//
// Only GetUsedPhysicalMemory is called. It is a plain field read (`mov rax,[rcx+0x20168]; ret`).
// grcResourceCache::GetTotalPhysicalMemory is deliberately NOT used: it calls the game's own
// _getAndUpdateAvailableMemory, which is a call INTO engine code that updates state, and the
// ceiling is already known here without asking (it is whatever the budget table holds).
typedef void*    (*GrcInstance_t)();
typedef uint64_t (*GrcUsed_t)(void*);
static GrcInstance_t g_grcInstance = nullptr;
static GrcUsed_t     g_grcUsed = nullptr;
static bool     g_poolTried = false;
static bool     g_poolSaid = false;      // the one-time "here is your pool" line
static bool     g_poolWarned = false;    // the one-time "pool is nearly full" line
static long     g_poolTicks = 0;

// SEH because GetInstance is a jump through a pointer rage-graphics-five fills in at runtime:
// before the graphics device exists it is still null, and a beat can land there.
static bool poolUsedBytes(uint64_t* used)
{
    __try {
        void* cache = g_grcInstance();
        if (!cache) return false;
        *used = g_grcUsed(cache);
        return *used != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void poolBeat()
{
    if (!g_poolTried) {
        g_poolTried = true;
        g_grcInstance = (GrcInstance_t)cfxSymbol("rage-graphics-five.dll", "?GetInstance@grcResourceCache@rage@@SAPEAV12@XZ");
        g_grcUsed     = (GrcUsed_t)cfxSymbol("rage-graphics-five.dll", "?GetUsedPhysicalMemory@grcResourceCache@rage@@QEAA_KXZ");
        if (!g_grcInstance || !g_grcUsed)
            LOG_DEBUG(LogCategory::Audit, "Texture pool: FiveM's grcResourceCache exports are missing; usage not reported");
    }
    if (!g_grcInstance || !g_grcUsed) return;

    uint64_t used = 0;
    if (!poolUsedBytes(&used)) return;
    uint64_t ceiling = g_budget ? g_budget : g_budgetCurr;   // ours if we raised it, else FiveM's
    if (!ceiling) return;

    double pct = used * 100.0 / ceiling;
    LOG_DEV(LogCategory::Audit, "pool %.2f GB of %.2f GB (%.1f%%)", used / 1073741824.0, ceiling / 1073741824.0, pct);
    if (!g_poolSaid) {
        g_poolSaid = true;
        LOG_INFO(LogCategory::Audit, "Texture pool: %.1f GB of %.1f GB in use (%.0f%%)",
                 used / 1073741824.0, ceiling / 1073741824.0, pct);
    }
    // Once a minute at DEBUG, so the log has a timeline to point at when a player says the world
    // went low-detail at a particular moment.
    else if (++g_poolTicks % 60 == 0)
        LOG_DEBUG(LogCategory::Audit, "Texture pool: %.1f GB of %.1f GB in use (%.0f%%)",
                  used / 1073741824.0, ceiling / 1073741824.0, pct);

    if (!g_poolWarned && used * 10 >= ceiling * 9) {
        g_poolWarned = true;
        LOG_WARN(LogCategory::Audit, "Texture pool is %.0f%% full (%.1f of %.1f GB). This is what texture loss looks like: "
                                     "shrink the HEAVY files above, or raise texture_budget in _settings.txt",
                 pct, used / 1073741824.0, ceiling / 1073741824.0);
    }
}

// What the pack costs the streamer once every file in it is resident, and how that compares
// with what the game is handing textures right now. Called from scanOverrides.
void costReport(uint64_t costVirt, uint64_t costPhys, std::vector<std::pair<uint64_t, std::string>>& big)
{
    if (costVirt + costPhys) {
        LOG_INFO(LogCategory::Audit, "Pack cost when fully loaded: %.1f MB texture memory + %.1f MB virtual memory",
                 costPhys / 1048576.0, costVirt / 1048576.0);
        std::sort(big.rbegin(), big.rend());
        for (const auto& item : big)
            LOG_WARN(LogCategory::Audit, "  HEAVY %6.1f MB  %s (likely 4K or uncompressed; shrink to fight texture loss)", item.first / 1048576.0, item.second.c_str());
        // Past ~1 GB the pack no longer fits the budget, and eviction inside GTA5.exe is
        // passive-only (cfx #3874), so the pool saturates and the whole world drops to low LOD.
        // That is the "textures not loading" report from players who never crash. The ceiling is
        // a fixed table FiveM fills at boot (PatchExtendedBudgeting.cpp: 3 GB x the console-locked
        // vid_budgetScale) with no VRAM term in it, which is why a 24 GB card saturates at exactly
        // the same point an 8 GB one does. Say that here: without it, high-end players read the
        // HEAVY list as a low-end problem and assume their hardware already covers it.
        // compare against what the game is giving RIGHT NOW; the budget line on the first beat
        // reports separately whether that ceiling then got raised
        if (costPhys >= (1024ull << 20) && g_budgetCurr) {
            LOG_INFO(LogCategory::Audit, "  Texture pool: Game allocation is %.1f GB. %s",
                     g_budgetCurr / 1073741824.0,
                     costPhys < g_budgetCurr / 2
                       ? "Pack fits with room to spare."
                       : "Pack takes a large share of budget; shrink HEAVY files if texture loss occurs.");
        }
    }
}

// Turns the setting into g_budgetWant. Runs before the scan, because decideBudget needs it there
// (see backgroundStartup). A legacy _budget file is already in g_set.budget: loadSettings reads it
// ahead of texture_budget, so it still wins, and migrateSettings has deleted it by now.
void readBudgetFile()
{
    const std::string& val = g_set.budget;
    if (val.empty() || val.find("auto") == 0) return;          // auto: leave g_budgetWant at -1

    double gb = atof(val.c_str());
    // only a written 0 means "leave it alone"; text atof cannot read is a typo, and says so
    if (val.find("game") == 0 || val.find("off") == 0 || (gb == 0.0 && isdigit((unsigned char)val[0]))) g_budgetWant = 0.0;
    else if (gb >= 1.0 && gb <= 48.0) g_budgetWant = gb;
    else {
        g_budgetWant = 0.0;
        LOG_WARN(LogCategory::Audit, "Texture budget: texture_budget = %s is not auto, game, or a number from 1 to 48; left at game default", val.c_str());
    }
}
