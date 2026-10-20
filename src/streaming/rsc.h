#pragma once

#include <cstdint>
#include <string>
#include <vector>

// What a file on disk costs the streamer once resident, read from its RSC7 header.
struct Cost {
    uint64_t cv = 0;
    uint64_t cp = 0;
    bool readable = true;
    bool rsc = true;
};

// One file the scan found, before it is checked and claimed.
struct Cand {
    std::string slot;
    std::string full;
    Cost c;
    bool prio = false;   // came out of an *_override folder, so it outranks an ordinary copy
};

uint64_t rscSizeFromFlags(uint32_t f);
bool rscCost(const std::string& path, uint64_t* virt, uint64_t* phys);
bool cannotLoad(const char* key, const Cost& c, bool quiet);
bool cannotLoadPath(const std::string& path, const char* key, bool quiet);
void readCosts(std::vector<Cand>& v);   // off the loader lock only: it starts threads and joins them
