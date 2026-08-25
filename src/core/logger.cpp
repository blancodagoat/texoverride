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
    // _SH_DENYNO, not fopen_s: fopen_s opens with sharing DENIED, so while anything else holds
    // the log open (a tail, an editor, a Discord upload in progress) every open here fails and
    // the line is dropped without a trace. Found 2026-08-25 when a tail -f silenced a whole session.
    FILE* f = _fsopen(g_logPath, "a", _SH_DENYNO);
    if (f) {
        fprintf(f, "[%s] [%s] %s %s\n", ts, levelToString(level), categoryToString(cat), buf);
        if (level >= LogLevel::Warn) fflush(f);
        fclose(f);
    }
    if (g_logCsInit) LeaveCriticalSection(&g_logCs);
}
