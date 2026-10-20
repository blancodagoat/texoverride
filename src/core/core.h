#pragma once

#include <windows.h>

// DllMain's two entry points. SetupSafe runs synchronously under the loader lock and installs the
// hook; BeatLoop is the background thread that does everything else (see core.cpp).
bool SetupSafe();
DWORD WINAPI BeatLoop(LPVOID);
