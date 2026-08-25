#include "core/logger.h"
#include "core/state.h"
#include <cstdio>
#include <share.h>
#include <cstdarg>
#include <ctime>

const char* levelToString(LogLevel lvl)
{
    switch (lvl) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    default:              return "INFO";
    }
}

const char* categoryToString(LogCategory cat)
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

void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...)
{
    if (level < g_minLogLevel) return;
    if (!g_logPath[0]) return;

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

    if (g_logCsInit) EnterCriticalSection(&g_logCs);
    // Opened ONCE and kept. It used to be fopen/fprintf/fclose per line: an NTFS open, an
    // append and a close (which is where Defender scans the file) for every line, inside this
    // lock, while the hook held g_cs and the game's main thread waited on it. Startup writes
    // ~700 lines. _SH_DENYNO so a tail, an editor or a Discord upload can still read it.
    // Setup rotates the previous log BEFORE the first line, so the lazy open never fights it.
    static FILE* f = nullptr;
    if (!f) f = _fsopen(g_logPath, "a", _SH_DENYNO);
    if (f) {
        fprintf(f, "[%s] [%s] %s %s\n", ts, levelToString(level), categoryToString(cat), buf);
        fflush(f);   // every line: a crash log with the last lines missing is worthless
    }
    if (g_logCsInit) LeaveCriticalSection(&g_logCs);
}
