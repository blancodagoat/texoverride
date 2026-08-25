#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

enum class LogCategory {
    Core,
    Scan,
    Collection,
    Audit,
    Claim,
    Verify,
    Live,
    Tattoo,
    Update
};

struct Ov {
    const char* slot = nullptr;
    const char* file = nullptr;   // both persistent, forward-slash, lowercased
    const char* gfile = nullptr;  // `file` as UTF-8: the only form the game may be handed
    uint32_t id = 0xFFFFFFFF;     // global streaming index our claim landed on
    uint32_t altId = 0xFFFFFFFF;  // the index the STORE resolves this name to
    uint32_t handle = 0;          // the handle value that points at OUR file
    uint8_t loadedSeen = 0;       // 1 once the first LOADED has been logged for this slot
};

struct Cost {
    uint64_t cv = 0;
    uint64_t cp = 0;
    bool readable = true;
    bool rsc = true;
};

struct Cand {
    std::string slot;
    std::string full;
    Cost c;
};

struct StrEntry {
    uint32_t handle;
    uint32_t flags;
};

struct StrMgr {
    StrEntry* entries;
    char pad[16];
    int numEntries;
};

struct RawEntryView {
    uint64_t packedFileEntry;
    uint32_t virtFlags, physFlags;
    uint64_t timestamp;
    const char* fileName;
};
static_assert(sizeof(RawEntryView) == 32, "FiveM RawEntry layout changed");

struct RawEntriesView {
    RawEntryView* memory[64];
    uint32_t count;
};

struct PlPreset {
    uint32_t hash;
    float v[5]; // v = uvX uvY scaleX scaleY rotation
};

struct PlColl {
    std::string name, src;                        // collection name, source xml (for the log)
    uint32_t hash = 0;
    std::vector<PlPreset> presets;                // in file order == in parse order in memory
    uint32_t arrOff = 0, nameOff = 0, stride = 0, uvOff = 0;   // solved layout
    bool solved = false, dead = false;
    long writes = 0;
};

struct LiveOp {
    int kind; // 0 = register, 1 = re-stat
    Ov ov;
    uint32_t handle;
};

struct Snap {
    uint64_t wt;
    uint64_t size;
};

enum SlotWhy {
    SLOT_OK = 0,
    SLOT_NO_EXPORTS,
    SLOT_BAD_NAME,
    SLOT_NO_MANAGER,
    SLOT_NO_MODULE,
    SLOT_NO_NAME
};

enum OccupiedResult {
    OCCUPIED_FAILED = 0,
    OCCUPIED_ATTACHED = 1,
    OCCUPIED_WAITING = 2
};

typedef uint32_t* (*RegRaw_t)(uint32_t*, const char*, bool, const char*, bool);
typedef void* (*GetStreamingManager_t)();
typedef void* (*GetStreamingModule_t)(void*, const char*);
typedef const RawEntriesView* (*GetRawEntries_t)();
typedef uint32_t* (*FindModuleSlot_t)(void*, uint32_t*, const char*);
typedef void* (*GetRawStreamer_t)();
typedef uint8_t* (*RawGetEntry_t)(void*, uint16_t);
typedef BOOL (WINAPI* PeekMsg_t)(LPMSG, HWND, UINT, UINT, UINT);
