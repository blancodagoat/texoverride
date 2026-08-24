#include "core/pattern.h"
#include <cstdlib>
#include <cstring>

// pattern scan over the game module's executable sections; -1 in pat = wildcard byte
uint8_t* scanModule(const short* pat, size_t len)
{
    HMODULE mod = GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)mod;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* b = (uint8_t*)mod + sec->VirtualAddress;
        size_t sz = sec->Misc.VirtualSize;
        for (size_t j = 0; j + len <= sz; ++j) {
            size_t k = 0;
            while (k < len && (pat[k] < 0 || b[j + k] == (uint8_t)pat[k])) ++k;
            if (k == len) return b + j;
        }
    }
    return nullptr;
}

// resolve a rip-relative disp32 (p points at the 4 displacement bytes)
uint8_t* ripTarget(uint8_t* p)
{
    return p + 4 + *(int32_t*)p;
}

int runningGameBuild()
{
    char path[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return 0;
    for (const char* p = path; (p = strstr(p, "_b")) != nullptr; p += 2) {
        int build = atoi(p + 2);
        if (build >= 1000 && build <= 9999) return build;
    }
    return 0;
}
