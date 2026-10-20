#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

// Pattern scan over the game module's executable sections; -1 in pat = wildcard byte.
uint8_t* scanModule(const short* pat, size_t len);
// Takes the array itself, so the length cannot be typed wrong.
template <size_t N> uint8_t* scanModule(const short (&pat)[N]) { return scanModule(pat, N); }
uint8_t* ripTarget(uint8_t* p);
int runningGameBuild();   // the b#### in the game exe's name; 0 when it has none. Read once.
