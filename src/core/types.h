#pragma once

#include <cstdint>
#include <string>

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
    const char* slot = nullptr;   // the streaming key, lowercase
    const char* file = nullptr;   // the file on disk, forward slashes, UTF-8: the form the game takes
    uint32_t id = 0xFFFFFFFF;     // global streaming index our claim landed on
    uint32_t altId = 0xFFFFFFFF;  // the index the STORE resolves this name to
    uint32_t handle = 0;          // the handle value that points at OUR file
    uint8_t loadedSeen = 0;       // 1 once the first LOADED has been logged for this slot
    // Whose file the game reads is decided when the load STARTS, not when a beat first notices
    // it finished. These three let LOADED say which, instead of reading the handle a second
    // later and calling whatever is there the winner.
    uint32_t loadHandle = 0;      // handle in the entry while the load was in flight
    uint8_t loadEdgeSeen = 0;     // 1 if a beat caught this slot mid-load, so loadHandle is real
    uint8_t reclaimedEarly = 0;   // 1 if the slot was reclaimed before its first LOADED
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

// A change the watcher found, waiting for the game's main thread.
struct LiveOp {
    enum Kind { Register, Restat } kind;
    std::string slot;
    std::string file;      // Register: the file on disk
    uint32_t handle = 0;   // Restat: the raw-streamer handle to refresh
};

typedef uint32_t* (*RegRaw_t)(uint32_t*, const char*, bool, const char*, bool);
