#pragma once

#include "core/types.h"

const char* levelToString(LogLevel lvl);
const char* categoryToString(LogCategory cat);
void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...);

#define LOG_DEBUG(cat, fmt, ...) logMessage(LogLevel::Debug, cat, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  logMessage(LogLevel::Info,  cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  logMessage(LogLevel::Warn,  cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) logMessage(LogLevel::Error, cat, fmt, ##__VA_ARGS__)

// Developer tracing. Compiled in ONLY by builddev.bat, which defines TEXOVERRIDE_DEV; the release
// build.bat does not, so these lines cannot reach a shipped .asi even by accident. They log at
// INFO so a dev build shows them without needing debug turned on as well.
#ifdef TEXOVERRIDE_DEV
#define LOG_DEV(cat, fmt, ...) logMessage(LogLevel::Info, cat, "DEV " fmt, ##__VA_ARGS__)
#else
#define LOG_DEV(cat, fmt, ...) ((void)0)
#endif
