#pragma once

#include "core/types.h"
#include <string>
#include <vector>

uint32_t joaat(const char* s, size_t n);
void parsePlacementXml(const std::string& path, const char* fname, std::vector<PlColl>& out);
void placementLocate();
bool placementSolveImpl(PlColl& pc, uint8_t* coll);
bool placementSolve(PlColl& pc, uint8_t* coll);
void placementBeat();
void placementBeatSafe();
void mergePlacement(std::vector<PlColl>& fresh);
void loadPlacementFiles();
