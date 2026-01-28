/*
 * sd_manager.cpp - SD Card Initialization and Management
 *
 * Centralized SD card initialization for use by:
 * - SD logging (output_sdlog.cpp)
 * - JSON config save/load (json_config.cpp)
 */

#include "../config.h"

#if defined(ENABLE_SD_LOGGING) || defined(ENABLE_JSON_CONFIG)

#include "sd_manager.h"
#include "system_config.h"
#include "message_api.h"
#include "log_tags.h"
#include "watchdog.h"
#include <SPI.h>
#include <SD.h>

// Use Arduino SD library across all platforms for consistency
// SD object is provided globally by SD.h

// Define BUILTIN_SDCARD if not already defined (for Teensy 4.x)
#ifndef BUILTIN_SDCARD
    #define BUILTIN_SDCARD 254
#endif

// Track if SD has been successfully initialized
static bool sdInitialized = false;

/**
 * Initialize SD card
 * Called once during setup(), used by both SD logging and JSON config
 */
void initSD() {
    msg.debug.info(TAG_SD, "Initializing SD card...");

    // Extend watchdog timeout - multiple SD.begin() attempts can take >10 seconds total
    // Note: Teensy 4.x cannot disable watchdog, only extend timeout
    // With 5 retry attempts, each potentially taking 2-3 seconds, we need at least 15 seconds
    watchdogEnable(20000);  // 20 second timeout

    bool initSuccess = false;

    if (systemConfig.sdCSPin == 254) {
        // Teensy 4.1 built-in SD uses 4-bit SDIO interface
        // Other platforms may use this as a special pin designation
        msg.debug.info(TAG_SD, "Using built-in SD (BUILTIN_SDCARD)");
        msg.debug.debug(TAG_SD, "Calling SD.begin(BUILTIN_SDCARD)...");
        delay(100);  // Let hardware stabilize
        initSuccess = SD.begin(BUILTIN_SDCARD);
    } else {
        // External SD card uses SPI with specified CS pin
        pinMode(systemConfig.sdCSPin, OUTPUT);
        msg.debug.info(TAG_SD, "Using external SD, CS Pin: %d", systemConfig.sdCSPin);
        delay(100);  // Let hardware stabilize
        initSuccess = SD.begin(systemConfig.sdCSPin);
    }

    // Restore normal watchdog timeout
    watchdogEnable(2000);

    if (initSuccess) {
        msg.debug.info(TAG_SD, "SD card initialized successfully");
        msg.debug.info(TAG_SD, "Card detected and ready");
        sdInitialized = true;
    } else {
        msg.debug.warn(TAG_SD, "SD card initialization failed");
        msg.debug.warn(TAG_SD, "Check: Is SD card inserted? Is it formatted FAT32?");
        sdInitialized = false;
    }
}

/**
 * Check if SD card is initialized and ready
 */
bool isSDInitialized() {
    return sdInitialized;
}

#endif // ENABLE_SD_LOGGING || ENABLE_JSON_CONFIG
