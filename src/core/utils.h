#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// Round robin over a list: is index `n` inside the `budget`-wide window starting at `from`?
// The window wraps, so one that runs off the end continues from the start and every index is
// still visited exactly once per sweep. Header-only so tools\test_reval.cpp can exercise the
// same expression the beat does rather than a copy of it.
inline bool inSweepSlice(size_t n, size_t count, size_t from, size_t budget)
{
    return count != 0 && ((n + count - (from % count)) % count) < budget;
}

std::string lower(std::string s);
std::string fwd(std::string s);
std::string trim(const std::string& s);
std::string extOf(const char* key);   // lowercase extension without the dot; "" when there is none
const char* rel(const char* file);
bool hasExt(const std::string& k, const char* e);
std::string ctlPath(const char* name);
uint32_t joaat(const char* s, size_t n);   // GTA's case-insensitive hash

// ---- paths ----
// Every path inside the plugin is UTF-8, and the wide Win32 calls are made only here at the
// edge. That is the form FiveM reads every path it is handed in (Cfx's ToWide runs
// utf8::replace_invalid over it), so what we hand the game and what we open ourselves are the
// same string. The ANSI calls this replaced lost any letter outside the system code page: a
// Cyrillic name on a Western Windows came back from GetModuleFileNameA as '?', and the plugin
// could not find its own folder. 0.8.3 converted ANSI to UTF-8 for the game, which fixed what
// the code page could spell and nothing else.
std::wstring widen(const std::string& utf8);
std::string narrow(const wchar_t* w);
std::string modulePath(void* module);   // HMODULE; null = the exe
FILE* openFile(const std::string& path, const char* mode);   // shared read/write, like the log
bool deleteFile(const std::string& path);
bool fileExists(const std::string& path);

// One recursive walk for both scans. visit gets the file or folder name, its path relative to
// base (backslashes), the attributes, write time and size; for a folder it returns whether to go
// in. Folders deeper than kMaxWalkDepth are not entered, which is what ends a junction that
// points back up the tree.
constexpr int kMaxWalkDepth = 16;
struct WalkItem {
    std::string name, childRel;
    bool dir;
    uint64_t writeTime, size;
};
void walkTree(const std::string& base, const std::string& sub, const std::function<bool(const WalkItem&)>& visit, int depth = 0);
