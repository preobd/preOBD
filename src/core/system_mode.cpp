/*
 * system_mode.cpp - System mode management implementation
 */

#include "system_mode.h"
#include "watchdog.h"
#include "message_api.h"
#include "log_tags.h"

// Current system mode
static SystemMode currentMode = MODE_RUN;

// Mode-transition observer, registered by the composition root (main.cpp).
// See system_mode.h for the contract.
static SystemModeObserver modeObserver = nullptr;

void setSystemModeObserver(SystemModeObserver observer) {
    modeObserver = observer;
}

// Timing constants
#define BOOT_DETECT_DELAY_MS 10  // Stabilization delay for boot detection

void initSystemMode() {
#if INCLUDE_MODE_BUTTON
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
#endif
    currentMode = MODE_RUN;  // Default to RUN mode for safety
}

SystemMode getCurrentMode() {
    return currentMode;
}

void setMode(SystemMode newMode) {
    SystemMode oldMode = currentMode;
    currentMode = newMode;

    // Every call, not only on a change: the boot-time setMode() can find the
    // mode already at its RUN default and still has to end the BOOT phase.
    watchdogSetConfigMode(newMode == MODE_CONFIG);

    // Handle mode transitions
    if (oldMode != newMode) {
        msg.control.println();
        if (newMode == MODE_CONFIG) {
            // The watchdog keeps running in CONFIG on every board (#540/#541),
            // relaxed to 8 s. The loop feeds it on every pass, so it only
            // fires if the firmware itself hangs, never on an idle prompt.
            msg.control.println(F("========================================"));
            msg.control.println(F("  ENTERED CONFIG MODE"));
            msg.control.println(F("  Configuration unlocked; sensors keep reading"));
            msg.control.println(F("  Actions, buzzer, CSV and SD logging held"));
            msg.control.println(F("  Watchdog relaxed to 8s (resets only on a firmware hang)"));
            msg.control.println(F("  Type RUN to resume normal operation"));
            msg.control.println(F("========================================"));
        } else {
            msg.control.println(F("========================================"));
            msg.control.println(F("  ENTERED RUN MODE"));
            msg.control.println(F("  Sensors active, configuration locked"));
            msg.control.println(F("  Watchdog enabled (2s timeout)"));
            msg.control.println(F("  Type CONFIG to modify configuration"));
            msg.control.println(F("========================================"));
        }

        if (modeObserver) {
            modeObserver(oldMode, newMode);
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
#if INCLUDE_MODE_BUTTON
    delay(BOOT_DETECT_DELAY_MS);
    if (digitalRead(MODE_BUTTON_PIN) == LOW) {
        msg.debug.info(TAG_SYSTEM, "CONFIG button detected - entering CONFIG mode");
        return MODE_CONFIG;
    }
#endif

    // Default: RUN mode
    msg.debug.info(TAG_SYSTEM, "Starting in RUN mode (config locked)");
    return MODE_RUN;
}
