// texoverride — FiveM client ASI plugin
// Draws user's own .ydd/.ytd files on ped, replaces weapon drawables and animation dictionaries

#include "core/core.h"
#include "core/state.h"
#include "core/logger.h"
#include "features/update.h"
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        if (SetupSafe() && !g_off) {   // synchronous: must finish before the game's entry point runs (see Setup)
            // If the beat thread never starts, scanFinishSafe never runs, g_scanDone is never set
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
        // orderly exit = no crash: the live-change journal must not quarantine anything.
        // only after crashSaverStartup has processed any leftover journal: if Setup faulted
        // before that point, deleting here would erase the previous crash's evidence.
        //
        // g_journalHot is the important half. "A real crash never reaches this line" was an
        // ASSUMPTION, and it is wrong often enough to matter: FiveM catches the fault, shows its
        // crash dialog, uploads a report and then tears the process down, and that path can run
        // detach for every loaded module. Deleting the journal there erases the name of the file
        // that just killed the game, so the next launch has nothing to quarantine and dies in the
        // same place forever. While the startup loop holds a key in the journal, leave it alone.
        if (g_crashSaverRan && g_inflightPath[0] && !InterlockedCompareExchange(&g_journalHot, 0, 0))
            DeleteFileA(g_inflightPath);
    }
    return TRUE;
}
