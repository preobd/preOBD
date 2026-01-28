/*
 * log_macros.h - Logging Convenience Macros
 *
 * Provides short macro shortcuts for structured logging with levels and tags.
 *
 * Usage:
 *   LOG_ERROR(TAG_SD, "Mount failed");
 *   LOG_INFO(TAG_SD, "Card size: %lu MB", cardSize);
 *   LOG_DEBUG(TAG_ADC, "Channel %d value: %d", channel, value);
 *
 * These macros route to msg.debug plane and compile to no-ops when
 * DISABLE_DEBUG_MESSAGES is defined.
 */

#ifndef LOG_MACROS_H
#define LOG_MACROS_H

#include "message_api.h"  // For msg global instance
#include "log_tags.h"     // For TAG_* constants
#include "log_filter.h"   // For LogLevel enum

// Macro API - routes to msg.debug plane with printf-style variadic arguments
// Uses ##__VA_ARGS__ to handle both simple strings and formatted output
#define LOG_ERROR(tag, fmt, ...) msg.debug.error(tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  msg.debug.warn(tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  msg.debug.info(tag, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) msg.debug.debug(tag, fmt, ##__VA_ARGS__)

// All macros compile to no-ops when debug messages disabled
#ifdef DISABLE_DEBUG_MESSAGES
    #undef LOG_ERROR
    #undef LOG_WARN
    #undef LOG_INFO
    #undef LOG_DEBUG
    #define LOG_ERROR(tag, fmt, ...) ((void)0)
    #define LOG_WARN(tag, fmt, ...)  ((void)0)
    #define LOG_INFO(tag, fmt, ...)  ((void)0)
    #define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

// Optional: Check if level/tag enabled (for expensive operations)
// Use this to avoid expensive computations when log would be filtered anyway
//
// Example:
//   if (LOG_IS_ENABLED(LOG_LEVEL_DEBUG, TAG_ID_ADC)) {
//       char buf[256];
//       formatExpensiveDebugInfo(buf, sizeof(buf));
//       LOG_DEBUG(TAG_ADC, "%s", buf);
//   }
#ifndef DISABLE_DEBUG_MESSAGES
    #define LOG_IS_ENABLED(level, tagId) (router.getLogFilter().shouldLog(PLANE_DEBUG, level, tagId))
#else
    #define LOG_IS_ENABLED(level, tagId) (false)
#endif

#endif // LOG_MACROS_H
