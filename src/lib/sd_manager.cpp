/*
 * sd_manager.cpp - SD Card Initialization and Management
 *
 * Centralized SD card initialization for use by:
 * - SD logging (output_sdlog.cpp)
 * - JSON config save/load (json_config.cpp)
 */

#include "../config.h"

#if SUPPORTS_SD && !defined(SD_PIN)
#error "SD_PIN must be defined in the board profile when SUPPORTS_SD is set"
#endif
#if ENABLE_SD_LOGGING && !SUPPORTS_SD
#error "ENABLE_SD_LOGGING requires SUPPORTS_SD=1 in the board profile"
#endif

#if SUPPORTS_SD

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

// Mount attempts before giving up. Each can take 2-3 s, so this must stay well
// inside the 20 s watchdog window opened in initSD().
#define SD_INIT_ATTEMPTS 3

// Track if SD has been successfully initialized
static bool sdInitialized = false;

/**
 * Initialize SD card
 * Called once during setup(), used by both SD logging and JSON config
 */
void initSD() {
    msg.debug.info(TAG_SD, "Initializing SD card...");

    // Extend watchdog timeout - SD.begin() can take 2-3 seconds per attempt and
    // we make several. Note: Teensy 4.x cannot disable watchdog, only extend it.
    watchdogEnable(20000);  // 20 second timeout

    bool initSuccess = false;

#if SD_PIN == 254
        msg.debug.info(TAG_SD, "Using built-in SD (BUILTIN_SDCARD)");
#else
        pinMode(SD_PIN, OUTPUT);
        msg.debug.info(TAG_SD, "Using external SD, CS pin: %d", SD_PIN);
#endif

    // Retry: a card that is still powering up, or a slot with marginal contact,
    // frequently fails the first begin() and succeeds on a later one. The
    // comment here has always claimed retries; the code only ever made one
    // attempt, so a cold card came up unmounted for the whole session.
    for (uint8_t attempt = 1; attempt <= SD_INIT_ATTEMPTS && !initSuccess; attempt++) {
        msg.debug.debug(TAG_SD, "SD.begin() attempt %u/%u", attempt, SD_INIT_ATTEMPTS);
        delay(100);
#if SD_PIN == 254
        initSuccess = SD.begin(BUILTIN_SDCARD);
#else
        initSuccess = SD.begin(SD_PIN);
#endif
        watchdogReset();
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

#endif // SUPPORTS_SD
