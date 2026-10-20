// texoverride — FiveM client ASI plugin
// Draws user's own .ydd/.ytd files on ped, replaces weapon drawables and animation dictionaries

#include "core/core.h"
#include "core/state.h"
#include "core/logger.h"
#include "features/crash_saver.h"
#include "features/update.h"
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        if (SetupSafe() && !g_off) {   // synchronous: must finish before the game's entry point runs (see Setup)
            // If the beat thread never starts, backgroundStartupSafe never runs, g_scanDone is never set
            // and the first stream call sits in its 5 minute wait for nothing. Signal it and stop.
            HANDLE beat = CreateThread(nullptr, 0, BeatLoop, nullptr, 0, nullptr);
            if (beat) {
                CloseHandle(beat);
                HANDLE upd = CreateThread(nullptr, 0, UpdateCheck, nullptr, 0, nullptr);
                if (upd) CloseHandle(upd);
                else LOG_WARN(LogCategory::Update, "Update check thread could not start (err %lu)", GetLastError());
            }
            else {
                DWORD e = GetLastError();
                g_off = true;
                if (g_scanDone) SetEvent(g_scanDone);
                LOG_ERROR(LogCategory::Core, "Background beat thread could not start (err %lu); plugin disabled", e);
            }
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Orderly exit, so the journal must not quarantine anything, except when the exit is
        // FiveM tearing down after a crash. journalOnDetach tells the two apart; see there.
        journalOnDetach();
    }
    return TRUE;
}
