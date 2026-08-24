#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

uint8_t* scanModule(const short* pat, size_t len);
uint8_t* ripTarget(uint8_t* p);
int runningGameBuild();
