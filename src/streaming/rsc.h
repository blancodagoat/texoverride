#pragma once

#include "core/types.h"
#include <cstdint>
#include <vector>

uint64_t rscSizeFromFlags(uint32_t f);
bool rscCost(const char* path, uint64_t* virt, uint64_t* phys);
Cost readCost(const char* path);
bool cannotLoad(const char* key, const Cost& c, bool quiet);
bool cannotLoadPath(const char* path, const char* key, bool quiet);
void readCosts(std::vector<Cand>& v);
