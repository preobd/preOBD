/*
 * system_mode.cpp - System mode management implementation
 */

#include "system_mode.h"
#include "../config.h"

// Current system mode
static SystemMode currentMode = MODE_RUN;

// Timing constants
#define BOOT_DETECT_DELAY_MS 10  // Stabilization delay for boot detection

void initSystemMode() {
    pinMode(MODE_BUTTON, INPUT);  // No pullup - external pullup assumed
    currentMode = MODE_RUN;  // Default to RUN mode for safety
}

SystemMode getCurrentMode() {
    return currentMode;
}

void setMode(SystemMode newMode) {
    SystemMode oldMode = currentMode;
    currentMode = newMode;

    // Print mode transition message
    if (oldMode != newMode) {
        Serial.println();
        if (newMode == MODE_CONFIG) {
            Serial.println(F("========================================"));
            Serial.println(F("  ENTERED CONFIG MODE"));
            Serial.println(F("  Sensors paused, configuration unlocked"));
            Serial.println(F("  Type RUN to resume normal operation"));
            Serial.println(F("========================================"));
        } else {
            Serial.println(F("========================================"));
            Serial.println(F("  ENTERED RUN MODE"));
            Serial.println(F("  Sensors active, configuration locked"));
            Serial.println(F("  Type CONFIG to modify configuration"));
            Serial.println(F("========================================"));
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
