#pragma once

// Takes FiveM's own on-screen text out of screenshots. Reads hide_overlay from _settings.txt.
// Must be called from Setup(), on the loader thread, like every other cfxConnect (see core.cpp).
void connectShotGate();

// Main thread, every frame (framePumpTick): polls the screenshot keys.
void shotKeyTick();
