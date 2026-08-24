#pragma once

#include "core/types.h"

const char* levelToString(LogLevel lvl);
const char* categoryToString(LogCategory cat);
void logMessage(LogLevel level, LogCategory cat, const char* fmt, ...);

#define LOG_DEBUG(cat, fmt, ...) logMessage(LogLevel::Debug, cat, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  logMessage(LogLevel::Info,  cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  logMessage(LogLevel::Warn,  cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) logMessage(LogLevel::Error, cat, fmt, ##__VA_ARGS__)
