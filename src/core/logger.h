#pragma once

#include "core/types.h"
#include <string>

// Setup() only, before anything logs: rotates the previous log to .old and arms the lock.
// Nothing is written before this has run, which is also what keeps the tests silent.
void logInit(const std::string& path);
void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...);

#define LOG_DEBUG(cat, fmt, ...) logMessage(LogLevel::Debug, cat, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  logMessage(LogLevel::Info,  cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  logMessage(LogLevel::Warn,  cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) logMessage(LogLevel::Error, cat, fmt, ##__VA_ARGS__)

// Developer tracing. Compiled in ONLY by `build.bat dev`, which defines TEXOVERRIDE_DEV; the
// release build does not, so these lines cannot reach a shipped .asi even by accident.
#ifdef TEXOVERRIDE_DEV
#define LOG_DEV(cat, fmt, ...) logMessage(LogLevel::Info, cat, "DEV " fmt, ##__VA_ARGS__)
#else
#define LOG_DEV(cat, fmt, ...) ((void)0)
#endif
