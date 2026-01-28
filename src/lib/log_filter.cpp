/*
 * log_filter.cpp - Log Level and Tag Filtering Implementation
 */

#include "log_filter.h"
#include "message_router.h"  // For MessagePlane enum (after log_filter.h to avoid circular dependency)
#include <string.h>

// Constructor - initialize with permissive defaults (show everything)
LogFilter::LogFilter() {
    // Start with DEBUG level (show all messages) for boot sequence
    for (int i = 0; i < MAX_MESSAGE_PLANES; i++) {
        levelThreshold[i] = LOG_LEVEL_DEBUG;
    }

    // Enable all tags by default
    enabledTags = 0xFFFFFFFF;  // All bits set = all tags enabled
}

// Check if a message should be logged
bool LogFilter::shouldLog(int plane, LogLevel level, uint8_t tagId) {
    // Check plane is valid
    if (plane >= MAX_MESSAGE_PLANES || plane < 0) {
        return false;
    }

    // Check log level threshold
    // Messages at or below the threshold level are shown
    // Example: threshold=INFO shows ERROR(1), WARN(2), INFO(3) but not DEBUG(4)
    if (level > levelThreshold[plane]) {
        return false;
    }

    // Check tag filter (if tag ID is valid)
    if (tagId < 32) {
        if (!(enabledTags & (1UL << tagId))) {
            return false;  // Tag is disabled
        }
    }
    // If tagId >= 32, skip tag filtering (unknown tag - allow through)

    return true;  // Both filters passed
}

// Set log level threshold for a specific plane
void LogFilter::setLevel(int plane, LogLevel level) {
    if (plane >= 0 && plane < MAX_MESSAGE_PLANES && level <= LOG_LEVEL_DEBUG) {
        levelThreshold[plane] = level;
    }
}

// Get current log level for a plane
LogLevel LogFilter::getLevel(int plane) {
    if (plane >= 0 && plane < MAX_MESSAGE_PLANES) {
        return (LogLevel)levelThreshold[plane];
    }
    return LOG_LEVEL_NONE;
}

// Enable or disable a specific tag
void LogFilter::enableTag(uint8_t tagId, bool enable) {
    if (tagId < 32) {
        if (enable) {
            enabledTags |= (1UL << tagId);   // Set bit
        } else {
            enabledTags &= ~(1UL << tagId);  // Clear bit
        }
    }
}

// Check if a tag is enabled
bool LogFilter::isTagEnabled(uint8_t tagId) {
    if (tagId < 32) {
        return (enabledTags & (1UL << tagId)) != 0;
    }
    return false;  // Unknown tags are disabled
}

// Enable all tags
void LogFilter::enableAllTags() {
    enabledTags = 0xFFFFFFFF;
}

// Disable all tags
void LogFilter::disableAllTags() {
    enabledTags = 0;
}

// Get level name as string
const char* LogFilter::getLevelName(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_NONE:  return "NONE";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default:              return "UNKNOWN";
    }
}

// Parse level name from string (case-insensitive)
LogLevel LogFilter::parseLevelName(const char* name) {
    if (!name) return LOG_LEVEL_NONE;

    if (strcasecmp(name, "NONE") == 0)  return LOG_LEVEL_NONE;
    if (strcasecmp(name, "ERROR") == 0) return LOG_LEVEL_ERROR;
    if (strcasecmp(name, "WARN") == 0)  return LOG_LEVEL_WARN;
    if (strcasecmp(name, "INFO") == 0)  return LOG_LEVEL_INFO;
    if (strcasecmp(name, "DEBUG") == 0) return LOG_LEVEL_DEBUG;

    return LOG_LEVEL_NONE;  // Not found
}

// ========== Tag Helper Functions (defined in log_tags.h) ==========

// Get tag ID from string name
uint8_t getTagID(const char* tagName) {
    if (!tagName) return NUM_LOG_TAGS;

    // Compare against each tag name
    for (uint8_t i = 0; i < NUM_LOG_TAGS; i++) {
        // Read tag name from PROGMEM
        const char* nameInProgmem = (const char*)pgm_read_ptr(&LOG_TAG_NAMES[i]);

        // Compare strings (case-sensitive for now)
        if (strcmp_P(tagName, nameInProgmem) == 0) {
            return i;
        }
    }

    return NUM_LOG_TAGS;  // Not found
}

// Get tag name from ID
const char* getTagName(uint8_t tagId) {
    if (tagId < NUM_LOG_TAGS) {
        // Read tag name pointer from PROGMEM array
        return (const char*)pgm_read_ptr(&LOG_TAG_NAMES[tagId]);
    }
    return nullptr;
}
