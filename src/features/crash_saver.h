#pragma once

#include "core/types.h"
#include <vector>

bool journalWrite(const std::vector<LiveOp>& batch);
void journalOne(const char* key);
void crashSaverStartup();
