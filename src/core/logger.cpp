#include "core/logger.h"
#include "core/state.h"
#include <cstdio>
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
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fprintf(f, "[%s] [%s] %s %s\n", ts, levelToString(level), categoryToString(cat), buf);
        if (level >= LogLevel::Warn) fflush(f);
        fclose(f);
    }
    if (g_logCsInit) LeaveCriticalSection(&g_logCs);
}
