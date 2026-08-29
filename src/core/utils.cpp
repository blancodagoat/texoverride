#include "core/utils.h"
#include "core/state.h"
#include <windows.h>
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

const char* rel(const char* file)
{
    size_t n = strlen(g_overrideDir);
    return strlen(file) > n ? file + n : file;
}

// FiveM reads EVERY narrow path it is handed as UTF-8: Cfx's ToWide (client/shared/Utils.cpp)
// runs utf8::replace_invalid over it before touching the disk. Our scan builds paths with the
// ANSI Win32 APIs instead. For an ASCII path those are the same bytes, which is why this went
// unnoticed. Put one non-ASCII letter in the Windows username and the ANSI bytes are invalid
// UTF-8, get swapped for U+FFFD, the path no longer exists, and every single claim comes back
// 0xFFFFFFFF with nothing on the ped and nothing in the log to say why. Reported on a Turkish
// username (issue #2) and proven there: same files, same build, ASCII account, worked at once.
// ANSI stays for our own file I/O, which is correct on the user's own code page.
const char* toUtf8(const char* ansi)
{
    for (const unsigned char* p = (const unsigned char*)ansi; ; ++p) {
        if (!*p) return ansi;             // pure ASCII: identical in both, share the string
        if (*p > 0x7F) break;
    }
    int wn = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (wn <= 0) return ansi;
    std::wstring w((size_t)wn, L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, &w[0], wn)) return ansi;
    int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (un <= 0) return ansi;
    char* out = (char*)malloc((size_t)un);
    if (!out) return ansi;
    if (!WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out, un, nullptr, nullptr)) { free(out); return ansi; }
    return out;                           // leaked on purpose, like every other path here
}

bool hasExt(const std::string& k, const char* e)
{
    return k.size() > 4 && k.compare(k.size() - 4, 4, e) == 0;
}

// Control files (_off, _debug, _budget ...). One rule for all of them: the bare name works and
// so does the .txt form. Windows Explorer hides known extensions, so "create an empty file named
// _OFF, no extension" reliably produces _OFF.txt for some people, and "create _debug.txt"
// reliably produces _debug.txt.txt for others. Accepting both spellings makes either mistake a
// non-event, and costs nothing next to renaming files under every existing install. Casing needs
// no code: Win32 paths are case-insensitive, so _OFF.txt already IS _off.txt.
// Returns the path that exists, or an empty string when neither does.
std::string ctlPath(const char* name)
{
    std::string p = std::string(g_overrideDir) + name;
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    p += ".txt";
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    return std::string();
}
