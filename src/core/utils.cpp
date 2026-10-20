#include "core/utils.h"
#include "core/state.h"
#include "core/logger.h"
#include <windows.h>
#include <share.h>
#include <cstdlib>
#include <cstring>
#include <cctype>

std::string lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string fwd(std::string s)
{
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string extOf(const char* key)
{
    const char* dot = key ? strrchr(key, '.') : nullptr;
    return (dot && dot[1]) ? lower(dot + 1) : std::string();
}

const char* rel(const char* file)
{
    size_t n = strlen(g_overrideDir);
    return strlen(file) > n ? file + n : file;
}

bool hasExt(const std::string& k, const char* e)
{
    size_t n = strlen(e);
    return k.size() > n && k.compare(k.size() - n, n, e) == 0;
}

// Control files (_off, _budget ...). One rule for all of them: the bare name works and so does
// the .txt form. Windows Explorer hides known extensions, so "create an empty file named _OFF, no
// extension" reliably produces _OFF.txt for some people, and "create _off.txt" reliably produces
// _off.txt.txt for others. Accepting both spellings makes either mistake a non-event, and costs
// nothing next to renaming files under every existing install. Casing needs no code: Win32 paths
// are case-insensitive, so _OFF.txt already IS _off.txt.
// Returns the path that exists, or an empty string when neither does.
std::string ctlPath(const char* name)
{
    std::string p = std::string(g_overrideDir) + name;
    if (fileExists(p)) return p;
    p += ".txt";
    if (fileExists(p)) return p;
    return std::string();
}

uint32_t joaat(const char* s, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n; ++i) { h += (uint8_t)tolower((unsigned char)s[i]); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15; return h;
}

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

std::string narrow(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::string modulePath(void* module)
{
    std::wstring buf(32768, L'\0');   // the long-path ceiling, so a deep install is never cut short
    DWORD n = GetModuleFileNameW((HMODULE)module, &buf[0], (DWORD)buf.size());
    if (!n || n >= buf.size()) return std::string();
    buf.resize(n);
    return narrow(buf.c_str());
}

FILE* openFile(const std::string& path, const char* mode)
{
    return _wfsopen(widen(path).c_str(), widen(mode).c_str(), _SH_DENYNO);
}

bool deleteFile(const std::string& path)
{
    return DeleteFileW(widen(path).c_str()) != 0;
}

bool fileExists(const std::string& path)
{
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void walkTree(const std::string& base, const std::string& sub, const std::function<bool(const WalkItem&)>& visit, int depth)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(base + sub + (sub.empty() ? "*" : "\\*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = narrow(fd.cFileName);
        if (name.empty() || name == "." || name == "..") continue;
        WalkItem it;
        it.name = name;
        it.childRel = sub.empty() ? name : sub + "\\" + name;
        it.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        it.writeTime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        it.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!visit(it) || !it.dir) continue;
        if (depth + 1 >= kMaxWalkDepth) {
            static bool said = false;
            if (!said) {
                said = true;
                LOG_WARN(LogCategory::Scan, "SKIP %s - folders nested %d deep (or a folder link that points back up the tree); not entered",
                         fwd(it.childRel).c_str(), kMaxWalkDepth);
            }
            continue;
        }
        walkTree(base, it.childRel, visit, depth + 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
