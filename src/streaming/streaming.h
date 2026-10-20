#pragma once

#include "core/types.h"
#include <cstdint>

// Why a name lookup came back empty. The difference between "no module for this type" and "the
// module does not know that name" is the whole diagnosis, so it reaches the log.
enum SlotWhy {
    SLOT_OK = 0,
    SLOT_NO_EXPORTS,
    SLOT_BAD_NAME,
    SLOT_NO_MANAGER,
    SLOT_NO_MODULE,
    SLOT_NO_NAME
};

// What one claim came to.
enum ClaimResult {
    CLAIM_DIRECT,      // the call took it and we read our handle back
    CLAIM_NO_HANDLE,   // the call took it, but the entry holds no handle we could read
    CLAIM_TAKEOVER,    // the call refused; our raw entry was attached to the existing slot
    CLAIM_WAITING,     // the call refused and the name has no slot yet; the beat binds it later
    CLAIM_REJECTED,    // nothing usable
};

const char* strStatusText(uint32_t flags);
void resolveOccupiedSlotExports();   // Setup(), and again later if FiveM's DLL was not loaded yet
void slotMemoryCost(uint32_t id, uint64_t* virt, uint64_t* phys);
uint32_t targetStreamingId(const char* slot, int* why = nullptr);
bool validStreamingId(uint32_t id);

// The one claim both the startup pass and live reload make. Calls into the game's register
// function, so it runs on a thread the game registers from (the streaming hook or the main-thread
// pump), never the beat or the watcher.
ClaimResult claimOne(Ov& ov);
