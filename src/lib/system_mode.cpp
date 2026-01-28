/*
 * system_mode.cpp - System mode management implementation
 */

#include "system_mode.h"
#include "../config.h"
#include "watchdog.h"
#include "message_api.h"
#include "log_tags.h"

#ifdef ENABLE_LCD
extern void showConfigModeMessage();
extern void clearLCD();
#endif

// Current system mode
static SystemMode currentMode = MODE_RUN;

// Timing constants
#define BOOT_DETECT_DELAY_MS 10  // Stabilization delay for boot detection

void initSystemMode() {
    pinMode(MODE_BUTTON, INPUT_PULLUP);  // Internal pullup - button pulls to GND when pressed
    currentMode = MODE_RUN;  // Default to RUN mode for safety
}

SystemMode getCurrentMode() {
    return currentMode;
}

void setMode(SystemMode newMode) {
    SystemMode oldMode = currentMode;
    currentMode = newMode;

    // Handle mode transitions
    if (oldMode != newMode) {
        msg.control.println();
        if (newMode == MODE_CONFIG) {
            // Disable watchdog when entering CONFIG mode
            watchdogDisable();

            msg.control.println(F("========================================"));
            msg.control.println(F("  ENTERED CONFIG MODE"));
            msg.control.println(F("  Sensors paused, configuration unlocked"));
            msg.control.println(F("  Watchdog disabled"));
            msg.control.println(F("  Type RUN to resume normal operation"));
            msg.control.println(F("========================================"));

            #ifdef ENABLE_LCD
            // Clear LCD (message will be shown by loop if no sensors)
            extern void clearLCD();
            clearLCD();
            #endif
        } else {
            // Enable watchdog when entering RUN mode
            watchdogEnable(2000);

            msg.control.println(F("========================================"));
            msg.control.println(F("  ENTERED RUN MODE"));
            msg.control.println(F("  Sensors active, configuration locked"));
            msg.control.println(F("  Watchdog enabled (2s timeout)"));
            msg.control.println(F("  Type CONFIG to modify configuration"));
            msg.control.println(F("========================================"));

            #ifdef ENABLE_LCD
            // Clear LCD when entering RUN mode (sensors will update it)
            clearLCD();
            #endif
        }
        msg.control.println();
    }
}

bool isInConfigMode() {
    return currentMode == MODE_CONFIG;
}

bool isInRunMode() {
    return currentMode == MODE_RUN;
}

SystemMode detectBootMode(bool eepromValid) {
    // If EEPROM invalid/empty, automatically enter CONFIG mode
    if (!eepromValid) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  NO CONFIGURATION FOUND"));
        msg.control.println(F("  Automatically entering CONFIG mode"));
        msg.control.println(F("========================================"));
        msg.control.println();
        return MODE_CONFIG;
    }

    // Check if button held during boot (LOW = active)
    delay(BOOT_DETECT_DELAY_MS);  // Allow pin to stabilize
    if (digitalRead(MODE_BUTTON) == LOW) {
        msg.debug.info(TAG_SYSTEM, "CONFIG button detected - entering CONFIG mode");
        return MODE_CONFIG;
    }

    // Default: RUN mode
    msg.debug.info(TAG_SYSTEM, "Starting in RUN mode (config locked)");
    return MODE_RUN;
}
