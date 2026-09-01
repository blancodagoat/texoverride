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
// The gate is only ever shut for the moment of a screenshot, and there is deliberately no way to
// shut it for good. This is not about removing FiveM's branding, which is theirs and which every
// player sees the whole time they play; it is about it not being in the picture afterwards. Any
// "leave it off" option would be the other thing wearing this one's name, so it does not exist.
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

// Render thread, once per frame, from our own node on the event. It is the only thread that
// walks the list, which is what makes moving nodes here safe (cfxDetachOwned).
static bool overlayGate()
{
    bool hide = (LONG64)GetTickCount64() < InterlockedCompareExchange64(&g_hideUntil, 0, 0);
    if (hide != g_hidden) {
        g_hidden = hide;
        int n = cfxDetachOwned(g_shotEvent, g_owners, 2, hide);
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
        int vk = vkFromName(t);
        if (vk > 0) g_keys.push_back(vk);
        else if (vk < 0)
            LOG_WARN(LogCategory::Core, "hide_overlay in _settings.txt lists \"%s\", which is not a key I know (use printscreen, f1 to f12, a letter, or a digit)", t.c_str());
    }
    if (g_keys.empty()) return;

    g_owners[0] = GetModuleHandleA("citizen-mod-loader-five.dll");
    g_owners[1] = GetModuleHandleA("font-renderer.dll");
    g_shotEvent = cfxSymbol("rage-graphics-five.dll", "?OnPostFrontendRender@@3V?$fwEvent@$$V@@A");
    if (!g_shotEvent || (!g_owners[0] && !g_owners[1]) || !cfxConnect(g_shotEvent, [] { return overlayGate(); })) {
        LOG_WARN(LogCategory::Core, "hide_overlay: this FiveM does not offer the drawing event, so its overlays cannot be hidden");
        g_keys.clear();
        return;
    }
    LOG_INFO(LogCategory::Core, "hide_overlay: FiveM's version watermark and mod pack counter come off the screen for a moment when you press your screenshot key (%d key(s) watched)",
             (int)g_keys.size());
}
