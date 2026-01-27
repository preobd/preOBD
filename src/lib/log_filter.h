/*
 * log_filter.h - Log Level and Tag Filtering
 *
 * Provides runtime filtering of log messages by:
 * - Log level (ERROR, WARN, INFO, DEBUG)
 * - Tag (SD, BME280, CAN, etc.)
 *
 * Filtering decisions are made before message formatting to minimize overhead.
 *
 * Usage:
 *   LogFilter filter;
 *   filter.setLevel(PLANE_DEBUG, LOG_LEVEL_INFO);
 *   filter.enableTag(TAG_ID_SD, true);
 *   if (filter.shouldLog(PLANE_DEBUG, LOG_LEVEL_INFO, TAG_ID_SD)) {
 *       // Log the message
 *   }
 */

#ifndef LOG_FILTER_H
#define LOG_FILTER_H

#include <Arduino.h>
#include "log_tags.h"        // For LogTag enum and NUM_LOG_TAGS

// Use constants to avoid circular dependency with message_router.h
// These must match MessagePlane enum in message_router.h
#define MAX_MESSAGE_PLANES 3

// Log level enumeration (increasing verbosity)
enum LogLevel {
    LOG_LEVEL_NONE  = 0,  // Disable all logging
    LOG_LEVEL_ERROR = 1,  // Critical failures only
    LOG_LEVEL_WARN  = 2,  // Warnings and errors
    LOG_LEVEL_INFO  = 3,  // Informational messages + warnings + errors
    LOG_LEVEL_DEBUG = 4   // Everything (maximum verbosity)
};

// Log filter class
class LogFilter {
private:
    // Per-plane log level thresholds (messages at this level or below are shown)
    uint8_t levelThreshold[MAX_MESSAGE_PLANES];

    // Tag enable/disable bitmap (bit N = 1 means tag N is enabled)
    // Supports up to 32 tags using a single uint32_t
    uint32_t enabledTags;

public:
    // Constructor - initialize with permissive defaults
    LogFilter();

    // ========== Filtering Logic ==========

    // Check if a message should be logged
    // Returns true if both level and tag filters pass
    // plane parameter is int to avoid circular dependency (use MessagePlane enum values)
    bool shouldLog(int plane, LogLevel level, uint8_t tagId);

    // ========== Level Configuration ==========

    // Set log level threshold for a specific plane
    // Messages at this level or below (numerically) will be shown
    // plane parameter is int to avoid circular dependency (use MessagePlane enum values)
    void setLevel(int plane, LogLevel level);

    // Get current log level for a plane
    // plane parameter is int to avoid circular dependency (use MessagePlane enum values)
    LogLevel getLevel(int plane);

    // ========== Tag Configuration ==========

    // Enable or disable a specific tag
    void enableTag(uint8_t tagId, bool enable);

    // Check if a tag is enabled
    bool isTagEnabled(uint8_t tagId);

    // Enable all tags
    void enableAllTags();

    // Disable all tags
    void disableAllTags();

    // Get enabled tags bitmap (for persistence)
    uint32_t getEnabledTags() { return enabledTags; }

    // Set enabled tags bitmap (for loading from EEPROM)
    void setEnabledTags(uint32_t tags) { enabledTags = tags; }

    // ========== Utility Functions ==========

    // Get level name as string
    const char* getLevelName(LogLevel level);

    // Parse level name from string (case-insensitive)
    // Returns LOG_LEVEL_NONE if not found
    LogLevel parseLevelName(const char* name);
};

#endif // LOG_FILTER_H
