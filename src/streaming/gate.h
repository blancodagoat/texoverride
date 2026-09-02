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
// A folder named disabled* (any case) is skipped whole by both scans; rename to switch a pack.
bool isDisabledFolder(const std::string& name);

// The streaming key a file registers under, from its path inside tex_overrides (forward
// slashes, lowercase). The collection is the file's own parent folder; any folders above it
// are the user's tidiness and mean nothing. Returns "" for a file with no place and sets *why
// to the message to log.
std::string slotKeyFor(const std::string& rel, const char** why);
