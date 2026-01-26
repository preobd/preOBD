/*
 * sd_manager.cpp - SD Card Initialization and Management
 *
 * Centralized SD card initialization for use by:
 * - SD logging (output_sdlog.cpp)
 * - JSON config save/load (json_config.cpp)
 */

#include "sd_manager.h"
#include "system_config.h"
#include "message_api.h"

// Suppress SdFat warning about FS.h conflict with Teensy's File class
#define DISABLE_FS_H_WARNING
#include <SdFat.h>

// Define BUILTIN_SDCARD if not already defined (for Teensy 4.x)
#ifndef BUILTIN_SDCARD
    #define BUILTIN_SDCARD 254
#endif

// Global SD object - shared by all SD card users
SdFat SD;

// Track if SD has been successfully initialized
static bool sdInitialized = false;

/**
 * Initialize SD card
 * Called once during setup(), used by both SD logging and JSON config
 */
void initSD() {
    msg.debug.println(F("[SD] Initializing SD card..."));

    bool initSuccess = false;

    if (systemConfig.sdCSPin == 254) {
        // Teensy 4.1 built-in SD card
        msg.debug.println(F("[SD] Using Teensy built-in SD (BUILTIN_SDCARD)"));
        initSuccess = SD.begin(BUILTIN_SDCARD);
    } else {
        // External SD card
        pinMode(systemConfig.sdCSPin, OUTPUT);
        msg.debug.print(F("[SD] Using external SD, CS Pin: "));
        msg.debug.println(systemConfig.sdCSPin);
        initSuccess = SD.begin(systemConfig.sdCSPin);
    }

    if (initSuccess) {
        msg.debug.println(F("[SD] ✓ SD card initialized successfully"));

        // Print card info
        if (SD.card() != nullptr) {
            msg.debug.print(F("[SD] Card type: "));
            msg.debug.println(SD.card()->type());
            if (SD.vol() != nullptr) {
                msg.debug.print(F("[SD] Volume: FAT"));
                msg.debug.println(SD.vol()->fatType());
            }
        }

        sdInitialized = true;
    } else {
        msg.debug.println(F("[SD] ⚠ SD card initialization failed"));
        sdInitialized = false;
    }
}

/**
 * Check if SD card is initialized and ready
 */
bool isSDInitialized() {
    return sdInitialized;
}
