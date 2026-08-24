#include "streaming/budget.h"
#include "core/state.h"
#include "core/logger.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

bool vramTableSane(uint64_t* t, uint64_t* cur)   // refuse to write unless it looks like Cfx filled it
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

// One DXGI probe, two numbers, both 0 when unknowable:
//   g_vramTotal  — the card's physical VRAM (biggest hardware adapter)
//   g_vramBudget — what Windows is willing to give THIS process right now. WDDM works it out
//                  itself, so it already subtracts the desktop, the browser, and anything else
//                  on the GPU. Microsoft is blunt about the other side of it: a process that
//                  runs past its budget "will likely experience stuttering, as they are
//                  intermittently frozen and paged-out". That is the exact failure the old
//                  opt-in rule was guarding against, so this is the number to size against
//                  rather than a fraction guessed from the card's sticker capacity.
void probeVram()
{
    // dxgi loaded by FULL System32 path: a static import (or a bare-name load) binds to any
    // already-loaded ReShade/ENB proxy dxgi.dll, and a proxy missing CreateDXGIFactory1 used
    // to fail the load of this whole plugin. The full path always gets Windows' own copy.
    typedef HRESULT (WINAPI* CreateFactory_t)(REFIID, void**);
    char dxPath[MAX_PATH + 16];
    UINT sl = GetSystemDirectoryA(dxPath, MAX_PATH);
    if (sl == 0 || sl >= MAX_PATH) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot locate System32"); return; }
    strcat_s(dxPath, "\\dxgi.dll");
    HMODULE dx = LoadLibraryA(dxPath);
    if (!dx) { LOG_WARN(LogCategory::Audit, "Texture budget: Cannot load %s (err %lu)", dxPath, GetLastError()); return; }
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

uint64_t autoBudget(uint64_t current)
{
    uint64_t cap = g_vramBudget;
    if (!cap || (g_vramTotal && cap > g_vramTotal)) cap = g_vramTotal;   // Budget can span shared RAM
    return budgetFor(cap, current);
}

void budgetBeatImpl()
{
    if (g_vramTable[3] == g_budget) return;   // our value is standing
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

void decideBudgetImpl()
{
    if (!g_vramTable) return;              // nothing to write into; Setup already said so
    if (g_budgetWant == 0.0) return;       // _budget.txt said leave it alone
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
        else LOG_INFO(LogCategory::Audit, "Texture budget: _budget.txt requested %.1f GB (up from %.1f GB)",
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

// What the pack costs the streamer once every file in it is resident, and how that compares
// with what the game is handing textures right now. Called from scanFinish.
void costReport()
{
    if (g_costVirt + g_costPhys) {
        LOG_INFO(LogCategory::Audit, "Pack cost when fully loaded: %.1f MB texture memory + %.1f MB virtual memory",
                 g_costPhys / 1048576.0, g_costVirt / 1048576.0);
        std::sort(g_costBig.rbegin(), g_costBig.rend());
        for (const auto& item : g_costBig)
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
        if (g_costPhys >= (1024ull << 20) && g_budgetCurr) {
            LOG_INFO(LogCategory::Audit, "  Texture pool: Game allocation is %.1f GB. %s",
                     g_budgetCurr / 1073741824.0,
                     g_costPhys < g_budgetCurr / 2
                       ? "Pack fits with room to spare."
                       : "Pack takes a large share of budget; shrink HEAVY files if texture loss occurs.");
        }
    }
}

// _budget.txt and the placement .xml files are user files too, so they are read out here with
// the rest of them instead of under the loader lock. Two functions because they sit on opposite
// sides of the scan: the budget decision needs the file before the scan starts (see
// backgroundStartup), and the placement parse is not needed until the first beat.
void readBudgetFile()
{
    char bp[MAX_PATH]; _snprintf_s(bp, _TRUNCATE, "%s_budget.txt", g_overrideDir);
    FILE* bf = nullptr;
    if (!fopen_s(&bf, bp, "rb") && bf) {
        char buf[32] = {}; fread(buf, 1, 31, bf); fclose(bf);
        double gb = atof(buf);
        if (gb >= 1.0 && gb <= 48.0) g_budgetWant = gb;
        else {
            g_budgetWant = 0.0;
            LOG_WARN(LogCategory::Audit, "Texture budget: _budget.txt does not hold a valid number (1-48 GB); left at game default");
        }
    }
}
