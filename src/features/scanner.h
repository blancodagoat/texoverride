#pragma once

#include "core/types.h"
#include <string>
#include <vector>

void walkDir(const std::string& base, const std::string& rel, std::vector<Cand>& out);
void scanFinish();
