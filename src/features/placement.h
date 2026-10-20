#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PlPreset {
    uint32_t hash;
    float v[5]; // v = uvX uvY scaleX scaleY rotation
};

struct PlColl {
    std::string name, src;                        // collection name, source xml (for the log)
    uint32_t hash = 0;
    std::vector<PlPreset> presets;                // in file order == in parse order in memory
    uint32_t arrOff = 0, nameOff = 0, stride = 0, uvOff = 0;   // solved layout
    bool solved = false, dead = false, notedThin = false;
    long writes = 0;
};

void parsePlacementXml(const std::string& path, const char* fname, std::vector<PlColl>& out);
void loadPlacementFiles();                         // beat thread, at startup
void mergePlacement(std::vector<PlColl>& fresh);   // watcher thread, an edited .xml
void placementBeatSafe();                          // beat thread, under g_cs: apply and re-assert
