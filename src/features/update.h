#pragma once

#include <windows.h>
#include <string>

int verCmp(const char* a, const char* b);   // >0 when a is newer than b

// What the GitHub "latest release" answer says, scraped without a JSON library.
struct Release {
    std::string tag;   // "v0.8.29"; empty = the answer had none (offline, rate limited)
    std::string url;   // texoverride.asi's download URL
    std::string sha;   // its SHA-256, lowercase hex; empty = none found, so nothing is installed
};
Release parseRelease(const std::string& body);

DWORD WINAPI UpdateCheck(LPVOID);
