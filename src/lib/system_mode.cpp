/*
 * system_mode.cpp - System mode management implementation
 */

#include "system_mode.h"
#include "../config.h"
#include "watchdog.h"

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
        Serial.println();
        if (newMode == MODE_CONFIG) {
            // Disable watchdog when entering CONFIG mode
            watchdogDisable();

            Serial.println(F("========================================"));
            Serial.println(F("  ENTERED CONFIG MODE"));
            Serial.println(F("  Sensors paused, configuration unlocked"));
            Serial.println(F("  Watchdog disabled"));
            Serial.println(F("  Type RUN to resume normal operation"));
            Serial.println(F("========================================"));

            #ifdef ENABLE_LCD
            // Clear LCD (message will be shown by loop if no sensors)
            extern void clearLCD();
            clearLCD();
            #endif
        } else {
            // Enable watchdog when entering RUN mode
            watchdogEnable(2000);

            Serial.println(F("========================================"));
            Serial.println(F("  ENTERED RUN MODE"));
            Serial.println(F("  Sensors active, configuration locked"));
            Serial.println(F("  Watchdog enabled (2s timeout)"));
            Serial.println(F("  Type CONFIG to modify configuration"));
            Serial.println(F("========================================"));

            #ifdef ENABLE_LCD
            // Clear LCD when entering RUN mode (sensors will update it)
            clearLCD();
            #endif
        }
        Serial.println();
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
        Serial.println();
        Serial.println(F("========================================"));
        Serial.println(F("  NO CONFIGURATION FOUND"));
        Serial.println(F("  Automatically entering CONFIG mode"));
        Serial.println(F("========================================"));
        Serial.println();
        return MODE_CONFIG;
    }

    // Check if button held during boot (LOW = active)
    delay(BOOT_DETECT_DELAY_MS);  // Allow pin to stabilize
    if (digitalRead(MODE_BUTTON) == LOW) {
        Serial.println();
        Serial.println(F("========================================"));
        Serial.println(F("  CONFIG BUTTON DETECTED"));
        Serial.println(F("  Entering CONFIG mode"));
        Serial.println(F("========================================"));
        Serial.println();
        return MODE_CONFIG;
    }

    // Default: RUN mode
    Serial.println(F("Starting in RUN mode (config locked)"));
    return MODE_RUN;
}
