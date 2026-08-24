#pragma once

#include <string>

const char* classifyCollection(const std::string& coll);
bool isPedCollection(const std::string& coll);
bool isBlockedCollection(const std::string& coll);
bool isPedComponentFile(const std::string& file);
std::string collectionOf(const std::string& key);
bool isOverrideExt(const std::string& ln);
bool isVanillaAnimalYmt(const std::string& key);
bool isAllowedKey(const std::string& key);
bool isIgnoredType(const std::string& ln, const std::string& rel, bool announce);
