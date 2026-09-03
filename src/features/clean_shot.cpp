#include "features/clean_shot.h"
#include "core/cfx.h"
#include "core/logger.h"
#include "core/settings.h"
#include "core/state.h"
#include <windows.h>
#include <string>
#include <vector>

// ============================ FiveM's overlays, out of shot ============================
// FiveM draws its version watermark ("FiveM ... (b3751)") and citizen-mod-loader-five draws
// "%d mod packs loaded", and both land in every screenshot. Neither is ours to switch off:
// they live in FiveM's own DLLs, and this plugin patches the GAME module only, never a Cfx
// component (CLAUDE.md, "How the hook works").
//
// Both draw from rage-graphics-five!OnPostFrontendRender. For the moment of a screenshot the
// two handlers those DLLs own are taken OFF that event, and put back a second later. Nothing
// else on the event is touched: the server's NUI (glue.dll, which draws chat and every HUD a
// server ships) sits on the same list, and a screenshot with the server's UI missing is not
// what anyone asked for. No code is patched anywhere; two list pointers move and move back.
//
// Two modes. A list of keys shuts the gate for a second and a half around a screenshot;
// `always` shuts it for the whole session (0.8.18; 0.8.16 shipped without it and said there
// would never be one, which is reversed here on purpose and said so in the notes). Either way
// this is the player's own screen only: the server and other players never see any of it.
//
// How the key is seen: the frame pump polls GetAsyncKeyState, the same way the refresh key does.
// A low-level keyboard hook was tried first (2026-09-01) and, with the game window in front, it
// never saw F9 at all and saw only the release half of PrintScreen; the same hook saw both keys
// perfectly with the game closed. Polling reads the physical key state and does not care who
// else is hooked. Screenshot tools like ShareX grab the pixels tens of ms after the key, on a
// thread of their own, and the overlays are gone from the next frame, so that race is won with
// room to spare. What this cannot beat is a capture that happens inside the keystroke itself,
// like Windows' own PrintScreen-to-clipboard: there is no frame between key and shot.

static volatile LONG64 g_hideUntil = 0;   // GetTickCount64() past which the overlays come back
static std::vector<int> g_keys;
static void*   g_shotEvent = nullptr;
static void*   g_owners[2] = {};          // the two DLLs whose handlers come off the event
static bool    g_hidden = false;          // render thread only
static void*   g_theFonts = nullptr;      // font-renderer!TheFonts, a FontRenderer**
static void*   g_standIns[2] = {};        // our node that sits where font-renderer's was

// rage-graphics-five's im-mode draw path, the same five calls font-renderer's own draw makes.
static void     (*g_imPush)()  = nullptr;
static void     (*g_imPop)()   = nullptr;
static void     (*g_imBegin)(int, int) = nullptr;
static void     (*g_imVertex)(float, float, float, float, float, float, uint32_t, float, float) = nullptr;
static void     (*g_imEnd)()   = nullptr;

// ---- Why the watermark handler cannot simply be removed (0.8.21, a field report) ----
// In a stock FiveM, font-renderer's handler is the LAST thing to draw in every frame: it queues
// the watermark and then calls TheFonts->DrawPerFrame(), which draws the queue with the game's
// im-mode path (GtaGameInterface.cpp, order 1000). 0.8.18's `always` took that handler off and
// put nothing in its place, and a player's DUI television then drew exactly one triangle of its
// two-triangle screen, whole again the moment hide_overlay was set to no. Every other draw in
// that frame is identical between the two settings, so the one difference is that the frame no
// longer ends with an im draw. Cfx itself issues a degenerate three-vertex draw before firing
// this event "to flush other grcore states" (RenderHooks.cpp), and that is what the stand-in
// below does at the parked handler's exact position: zero area, no pixels, same state walk.
//
// The same handler is also the ONLY place FiveM draws the text every other component queues
// through TheFonts (the red reconnect warning in BindNetLibrary.cpp, for one) and the only
// caller of its text arena's page swap (FontRendererAllocator.cpp), so DrawPerFrame is called
// here as well; Cfx calls it from a foreign handler in NuiLoadWarning.cpp too. DrawPerFrame is
// slot 3 of FontRenderer's vtable (Initialize, DrawText, DrawRectangle, DrawPerFrame,
// GetStringMetrics; font-renderer/include/FontRenderer.h), and the slot has to point into
// font-renderer.dll or nothing is called. Not SEH-wrapped on purpose: a call into Cfx code
// fails at the call site, not minutes later looking like somebody else's bug.
static bool standInForWatermark()
{
    void* obj = g_theFonts ? *(void**)g_theFonts : nullptr;
    if (obj) {
        void** vt = *(void***)obj;
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt[3], &m)
            && m && m == (HMODULE)g_owners[1])
            ((void(__fastcall*)(void*))vt[3])(obj);
    }
    if (g_imPush && g_imPop && g_imBegin && g_imVertex && g_imEnd) {
        g_imPush();
        g_imBegin(3, 3);   // 3 = triangle list, the type font-renderer's glyph draw uses
        for (int i = 0; i < 3; ++i) g_imVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u, 0.0f, 0.0f);
        g_imEnd();
        g_imPop();
    }
    return true;
}

// Render thread, once per frame, from our own node on the event. It is the only thread that
// walks the list, which is what makes moving nodes here safe (cfxDetachOwned).
static bool overlayGate()
{
    bool hide = (LONG64)GetTickCount64() < InterlockedCompareExchange64(&g_hideUntil, 0, 0);
    if (hide != g_hidden) {
        g_hidden = hide;
        int n = cfxDetachOwned(g_shotEvent, g_owners, g_standIns, 2, hide);
        LOG_DEV(LogCategory::Core, "hide_overlay: %d handler(s) %s the event", n, hide ? "taken off" : "put back on");
#ifdef TEXOVERRIDE_DEV
        if (hide) cfxDumpEvent(g_shotEvent, "after-detach");
#endif
    }
    return true;   // never stops the chain
}

// Main thread, every frame, from framePumpTick. Edge off the raw key state, foreground test
// second, same shape as refreshKeyTick and for the same reason: a press that arrives while
// something else is in front should say so rather than look like a press never seen.
void shotKeyTick()
{
    static std::vector<char> held;
    if (g_keys.empty()) return;
    if (held.size() != g_keys.size()) held.assign(g_keys.size(), 0);
    for (size_t i = 0; i < g_keys.size(); ++i) {
        if (g_keys[i] == 0) continue;   // the "always" placeholder
        bool down = (GetAsyncKeyState(g_keys[i]) & 0x8000) != 0;
        if (down && !held[i]) {
            DWORD pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            if (pid == GetCurrentProcessId()) {
                InterlockedExchange64(&g_hideUntil, (LONG64)GetTickCount64() + 1500);
                LOG_DEV(LogCategory::Core, "hide_overlay: screenshot key vk=0x%02X pressed, overlays off for 1.5s", g_keys[i]);
            }
            else
                LOG_DEV(LogCategory::Core, "hide_overlay: key vk=0x%02X ignored: foreground window is pid %lu, we are %lu",
                        g_keys[i], (unsigned long)pid, (unsigned long)GetCurrentProcessId());
        }
        held[i] = down;
    }
}

void connectShotGate()
{
    // "no" / "off" / blank -> vkFromName gives 0 for the whole value and there is nothing to do.
    for (size_t a = 0; a <= g_set.hideOverlay.size(); ) {
        size_t b = g_set.hideOverlay.find(',', a);
        if (b == std::string::npos) b = g_set.hideOverlay.size();
        std::string t = g_set.hideOverlay.substr(a, b - a);
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        while (!t.empty() && t.back()  == ' ') t.pop_back();
        a = b + 1;
        if (t.empty()) continue;
        if (t == "always") { InterlockedExchange64(&g_hideUntil, 0x7FFFFFFFFFFFFFFFLL); g_keys.push_back(0); continue; }   // 0 = placeholder, never polled
        int vk = vkFromName(t);
        if (vk > 0) g_keys.push_back(vk);
        else if (vk < 0)
            LOG_WARN(LogCategory::Core, "hide_overlay in _settings.txt lists \"%s\", which is not a key I know (use printscreen, f1 to f12, a letter, or a digit)", t.c_str());
    }
    if (g_keys.empty()) return;

    g_owners[0] = GetModuleHandleA("citizen-mod-loader-five.dll");
    g_owners[1] = GetModuleHandleA("font-renderer.dll");
    g_theFonts  = cfxSymbol("font-renderer.dll", "?TheFonts@@3PEAVFontRenderer@@EA");
    g_shotEvent = cfxSymbol("rage-graphics-five.dll", "?OnPostFrontendRender@@3V?$fwEvent@$$V@@A");
    g_imPush   = (decltype(g_imPush))  cfxSymbol("rage-graphics-five.dll", "?PushDrawBlitImShader@@YAXXZ");
    g_imPop    = (decltype(g_imPop))   cfxSymbol("rage-graphics-five.dll", "?PopDrawBlitImShader@@YAXXZ");
    g_imBegin  = (decltype(g_imBegin)) cfxSymbol("rage-graphics-five.dll", "?grcBegin@rage@@YAXHH@Z");
    g_imVertex = (decltype(g_imVertex))cfxSymbol("rage-graphics-five.dll", "?grcVertex@rage@@YAXMMMMMMIMM@Z");
    g_imEnd    = (decltype(g_imEnd))   cfxSymbol("rage-graphics-five.dll", "?grcEnd@rage@@YAXXZ");
    g_standIns[1] = cfxMakeNode(g_shotEvent, [] { return standInForWatermark(); }, 1000);
    if (!g_shotEvent || (!g_owners[0] && !g_owners[1]) || !cfxConnect(g_shotEvent, [] { return overlayGate(); })) {
        LOG_WARN(LogCategory::Core, "hide_overlay: this FiveM does not offer the drawing event, so its overlays cannot be hidden");
        g_keys.clear();
        return;
    }
    if (!g_imPush || !g_imPop || !g_imBegin || !g_imVertex || !g_imEnd)
        LOG_WARN(LogCategory::Core, "hide_overlay: this FiveM does not export its im-mode draw path, so the frame-end draw is skipped (DUI screens may draw half)");
    if (g_keys.size() == 1 && g_keys[0] == 0)
        LOG_INFO(LogCategory::Core, "hide_overlay: FiveM's version watermark and mod pack counter stay off your screen this session (always)");
    else
        LOG_INFO(LogCategory::Core, "hide_overlay: FiveM's version watermark and mod pack counter come off the screen for a moment when you press your screenshot key (%d key(s) watched)",
                 (int)g_keys.size());
}
