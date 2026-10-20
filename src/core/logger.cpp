#include "core/logger.h"
#include "core/utils.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>

// Every level is written. Since 0.8.26 the log always carries DEBUG: it is a support tool, and
// "turn debug on and send it again" cost a round trip on every single report.
static std::string s_path;
static CRITICAL_SECTION s_cs;   // the hook runs on more than one thread
static bool s_init = false;

static const char* levelToString(LogLevel lvl)
{
    switch (lvl) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    default:              return "INFO";
    }
}

static const char* categoryToString(LogCategory cat)
{
    switch (cat) {
    case LogCategory::Core:       return "[CORE]";
    case LogCategory::Scan:       return "[SCAN]";
    case LogCategory::Collection: return "[COLLECTION]";
    case LogCategory::Audit:      return "[AUDIT]";
    case LogCategory::Claim:      return "[CLAIM]";
    case LogCategory::Verify:     return "[VERIFY]";
    case LogCategory::Live:       return "[LIVE]";
    case LogCategory::Tattoo:     return "[TATTOO]";
    case LogCategory::Update:     return "[UPDATE]";
    default:                      return "[CORE]";
    }
}

// Fresh log every launch, but keep one previous generation: after a crash the next launch used
// to destroy the exact log that showed what the crashed session was doing.
void logInit(const std::string& path)
{
    s_path = path;
    InitializeCriticalSection(&s_cs);
    s_init = true;
    std::wstring w = widen(path), old = widen(path + ".old");
    if (!MoveFileExW(w.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(w.c_str());   // rotation blocked (file held open): keep "fresh log" true
}

void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...)
{
    if (!s_init) return;

    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);

    EnterCriticalSection(&s_cs);
    // Opened ONCE and kept. It used to be fopen/fprintf/fclose per line: an NTFS open, an
    // append and a close (which is where Defender scans the file) for every line, inside this
    // lock, while the hook held g_cs and the game's main thread waited on it. Startup writes
    // ~700 lines. Shared so a tail, an editor or a Discord upload can still read it.
    // logInit rotates the previous log BEFORE the first line, so the lazy open never fights it.
    static FILE* f = nullptr;
    if (!f) f = openFile(s_path, "a");
    if (f) {
        fprintf(f, "[%s] [%s] %s %s\n", ts, levelToString(level), categoryToString(cat), buf);
        fflush(f);   // every line: a crash log with the last lines missing is worthless
    }
    LeaveCriticalSection(&s_cs);
}
