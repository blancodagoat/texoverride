#pragma once

#include <windows.h>

// Live reload: a watcher thread notices changes in tex_overrides and the game's main thread
// applies them. See live_reload.cpp for the threading rules.

bool connectFramePump();          // Setup(), on the loader thread: subscribe to OnMainGameFrame
void locateRawStreamer();         // beat thread, with the other optional patterns
DWORD WINAPI WatchLoop(LPVOID);   // the watcher thread
