#pragma once

#include "core/types.h"
#include <windows.h>
#include <string>
#include <vector>

void freeLiveOp(LiveOp& op);
bool rawInvalidate(uint32_t handle);
int liveRegister(Ov& ov);
void drainOps();
BOOL WINAPI h_peekMsg(LPMSG m, HWND w, UINT a, UINT b, UINT r);
bool installPump();
void submitBatch(std::vector<LiveOp>& batch);
void rescanTree(const std::string& base, const std::string& sub, bool quiet, std::vector<std::string>& xmls, std::vector<LiveOp>& batch);
DWORD WINAPI WatchLoop(LPVOID);
