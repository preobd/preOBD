/*
 * message_api.cpp - High-Level Messaging API Implementation
 */

#include "message_api.h"
#include <stdio.h>   // For vsnprintf

// Global message API instance
MessageAPI msg;

// ========== Level-Based Logging Implementation ==========

#ifndef DISABLE_DEBUG_MESSAGES

// Helper method to format and output with level/tag prefix
size_t MessageStream::logWithLevel(LogLevel level, const char* tag, const char* msg) {
    // Build prefix: [LEVEL][TAG]
    print("[");
    print(router.getLogFilter().getLevelName(level));
    print("][");
    print(tag);
    print("] ");

    // Output the message
    return println(msg);
}

// Error level logging
size_t MessageStream::error(const char* tag, const char* fmt, ...) {
    // Get tag ID for filtering
    uint8_t tagId = getTagID(tag);

    // Check filter first (early exit if filtered)
    if (!router.getLogFilter().shouldLog(plane, LOG_LEVEL_ERROR, tagId)) {
        return 0;
    }

    // Format the message using vsnprintf
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Output with level/tag prefix
    return logWithLevel(LOG_LEVEL_ERROR, tag, buffer);
}

// Warning level logging
size_t MessageStream::warn(const char* tag, const char* fmt, ...) {
    // Get tag ID for filtering
    uint8_t tagId = getTagID(tag);

    // Check filter first (early exit if filtered)
    if (!router.getLogFilter().shouldLog(plane, LOG_LEVEL_WARN, tagId)) {
        return 0;
    }

    // Format the message using vsnprintf
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Output with level/tag prefix
    return logWithLevel(LOG_LEVEL_WARN, tag, buffer);
}

// Info level logging
size_t MessageStream::info(const char* tag, const char* fmt, ...) {
    // Get tag ID for filtering
    uint8_t tagId = getTagID(tag);

    // Check filter first (early exit if filtered)
    if (!router.getLogFilter().shouldLog(plane, LOG_LEVEL_INFO, tagId)) {
        return 0;
    }

    // Format the message using vsnprintf
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Output with level/tag prefix
    return logWithLevel(LOG_LEVEL_INFO, tag, buffer);
}

// Debug level logging
size_t MessageStream::debug(const char* tag, const char* fmt, ...) {
    // Get tag ID for filtering
    uint8_t tagId = getTagID(tag);

    // Check filter first (early exit if filtered)
    if (!router.getLogFilter().shouldLog(plane, LOG_LEVEL_DEBUG, tagId)) {
        return 0;
    }

    // Format the message using vsnprintf
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Output with level/tag prefix
    return logWithLevel(LOG_LEVEL_DEBUG, tag, buffer);
}

#endif // DISABLE_DEBUG_MESSAGES
