#pragma once

#include "core/types.h"
#include <cstdint>

const char* strStatusText(uint32_t flags);
void resolveOccupiedSlotExports();
uint32_t findModuleSlotSafe(void* module, const char* stem, int build);
const char* slotWhyText(int w);
uint32_t targetStreamingId(const char* slot, int* why = nullptr);
void noteSlotWhy(const char* slot, int why);
bool localRawHandle(const char* file, uint32_t* handle);
bool validStreamingId(uint32_t id);
StrMgr* exportedManagerSafe();
int recoverOccupiedSlot(Ov& ov);
