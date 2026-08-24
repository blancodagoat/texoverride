#pragma once

#include "core/types.h"
#include <windows.h>

uint32_t* h_regRaw(uint32_t* fileId, const char* name, bool b1, const char* asName, bool b2);
void locateRuntimePatterns();
void backgroundStartup();
bool backgroundStartupCppSafe();
void scanFinishSafe();
DWORD WINAPI BeatLoop(LPVOID);
void Setup();
bool SetupSafe();
