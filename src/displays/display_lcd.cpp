/*
 * display_lcd.cpp - LCD display module
 * Works with both EEPROM config and compile-time config
 */

#include "../config.h"
#include "../inputs/input.h"
#include "../lib/sensor_types.h"
#include "../lib/sensor_library.h"
#include "../lib/application_presets.h"
#include "../lib/units_registry.h"

#ifdef ENABLE_LCD

#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
byte currentLine = 0;

// Custom character icon definitions
enum ICONS{
    ICON_DEGREE,
    ICON_THERMOMETER,
    ICON_OIL_CAN,
    ICON_TURBO,
    ICON_BATTERY,
    ICON_TACHOMETER,
    ICON_COOLANT,
    ICON_OIL
};

byte degree_icon[8] = {0xc,0x12,0x12,0xc,0x0,0x0,0x0,0x0};
byte thermometer_icon[8] = {0x4,0xa,0xa,0xa,0x11,0x1f,0xe,0x0};
byte oil_can_icon[8] = {0x02,0x05,0x0E,0x11,0x11,0x11,0x1F,0x00};
//byte oil_icon[8] = {0x04,0x0E,0x1F,0x1D,0x1D,0x1B,0x0E,0x00}; //big drop
byte oil_icon[8] = {0x00,0x04,0x0E,0x1D,0x1D,0x1B,0x0E,0x00}; //small drop
byte turbo_icon[8] = {0x00,0x0F,0x1E,0x1B,0x15,0x1B,0x0E,0x00};
byte battery_icon[8] = {0x00,0x00,0x0A,0x1F,0x11,0x11,0x1F,0x00};
byte tachometer_icon[8] = {0x00,0x0E,0x11,0x11,0x15,0x11,0x0E,0x00};
byte coolant_icon[8] = {0x04,0x07,0x04,0x07,0x04,0x0E,0x0E,0x04};

// Forward declarations
void showConfigModeMessage();
byte getIconForApplication(uint8_t appIndex);

void initLCD() {
    lcd.init();
    lcd.backlight();
    lcd.createChar(ICON_DEGREE, degree_icon);
    lcd.createChar(ICON_THERMOMETER, thermometer_icon);
    lcd.createChar(ICON_OIL_CAN, oil_can_icon);
    lcd.createChar(ICON_TURBO, turbo_icon);
    lcd.createChar(ICON_BATTERY, battery_icon);
    lcd.createChar(ICON_TACHOMETER, tachometer_icon);
    lcd.createChar(ICON_COOLANT, coolant_icon);
    lcd.createChar(ICON_OIL, oil_icon);
    lcd.clear();
    Serial.println("LCD initialized");
}

/**
 * Get icon for application using registry-based pattern matching
 *
 * Uses application name patterns and measurement types to determine
 * the appropriate icon. This is data-driven - no code changes needed
 * when adding new applications.
 *
 * @param appIndex Index into APPLICATION_PRESETS array
 * @return Icon character code (custom LCD characters)
 */
byte getIconForApplication(uint8_t appIndex) {
    if (appIndex >= NUM_APPLICATION_PRESETS) return 32; // space

    // Get application info from registry
    const ApplicationPreset* preset = getApplicationByIndex(appIndex);
    if (!preset) return 32;

    // Read name from PROGMEM into RAM buffer
    const char* namePtr = (const char*)pgm_read_ptr(&preset->name);
    char nameBuf[32];
    strncpy_P(nameBuf, namePtr, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';

    MeasurementType measType = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);

    // Pattern matching on name (now in RAM)
    // Check specific patterns first, then fall back to measurement type

    if (strstr(nameBuf, "COOLANT") && strstr(nameBuf, "TEMP")) {
        return ICON_COOLANT;
    }
    if (strstr(nameBuf, "OIL")) {
        return ICON_OIL;
    }
    if (strstr(nameBuf, "FUEL")) {
        return ICON_OIL_CAN;
    }
    if (strstr(nameBuf, "BOOST")) {
        return ICON_TURBO;
    }
    if (strstr(nameBuf, "BATTERY")) {
        return ICON_BATTERY;
    }
    if (strstr(nameBuf, "RPM")) {
        return ICON_TACHOMETER;
    }

    // Fallback to measurement type
    switch (measType) {
        case MEASURE_TEMPERATURE:
            return ICON_THERMOMETER;
        case MEASURE_PRESSURE:
            return ICON_TURBO;  // Generic pressure icon
        case MEASURE_VOLTAGE:
            return ICON_BATTERY;
        case MEASURE_RPM:
            return ICON_TACHOMETER;
        default:
            return 32; // space
    }
}

void displaySensor(Input *ptr, byte line) {
    // Calculate column position (0-9 for left, 10-19 for right)
    byte col = (line >= 4) ? 10 : 0;
    byte row = (line >= 4) ? line - 4 : line;

    lcd.setCursor(col, row);

    int charsPrinted = 0;

    if (ptr->flags.display) {
        // Print icon
        lcd.write(getIconForApplication(ptr->applicationIndex));
        charsPrinted++;

        // Print sensor abbreviation
        charsPrinted += lcd.print(ptr->abbrName);
        charsPrinted += lcd.print(":");

        // If sensor is not enabled, show "CFG"
        if (!ptr->flags.isEnabled) {
            charsPrinted += lcd.print("CFG");
        }
        // Check for invalid values (NaN)
        else if (isnan(ptr->value)) {
            charsPrinted += lcd.print("ERR");
        }
        // If value is exactly 0.0, show "---" (likely in CONFIG mode, not yet read)
        else if (ptr->value == 0.0f) {
            charsPrinted += lcd.print("---");
        } else {
            // Convert to display units
            float displayValue = getDisplayConvertFunc(ptr->measurementType)(ptr->value, ptr->unitsIndex);

            // Get unit info from registry
            const UnitsInfo* unitInfo = getUnitsByIndex(ptr->unitsIndex);
            MeasurementType measType = MEASURE_TEMPERATURE;
            if (unitInfo) {
                measType = (MeasurementType)pgm_read_byte(&unitInfo->measurementType);
            }

            // Determine decimal precision based on measurement type
            int decimals = 1;  // Default: 1 decimal place
            switch (measType) {
                case MEASURE_TEMPERATURE:
                case MEASURE_HUMIDITY:
                case MEASURE_ELEVATION:
                case MEASURE_RPM:
                case MEASURE_DIGITAL:
                    decimals = 0;  // No decimals for these
                    break;
                case MEASURE_PRESSURE:
                    decimals = (ptr->unitsIndex == INHG) ? 2 : 1;  // 2 decimals for inHg
                    break;
                case MEASURE_VOLTAGE:
                    decimals = 1;  // 1 decimal for voltage
                    break;
            }

            // Print value with appropriate precision
            charsPrinted += lcd.print(displayValue, decimals);

            // Print unit symbol from registry
            if (measType == MEASURE_TEMPERATURE) {
                // Temperature gets a degree symbol before the unit
                lcd.write((byte)ICON_DEGREE);
                charsPrinted++;
            } else if (unitInfo) {
                // Use symbol from registry
                const char* symPtr = (const char*)pgm_read_ptr(&unitInfo->symbol);
                if (symPtr) {
                    lcd.print((__FlashStringHelper*)symPtr);
                    charsPrinted += strlen_P(symPtr);
                }
            }
        }
    }

    // Pad remaining space in the 10-character block to clear old data
    for (int i = charsPrinted; i < 10; i++) {
        lcd.print(" ");
    }
}

void updateLCD(Input** inputs, int numInputs) {
    currentLine = 0;

    // If no sensors configured, show CONFIG MODE message
    if (numInputs == 0) {
        static bool configMsgShown = false;
        if (!configMsgShown) {
            showConfigModeMessage();
            configMsgShown = true;
        }
        return;
    }

    // Clear the CONFIG MODE message when first sensor is configured
    static bool firstSensorShown = false;
    if (!firstSensorShown) {
        lcd.clear();
        firstSensorShown = true;
    }

    // Display all sensors (enabled will show values, disabled will show "CFG")
    for (int i = 0; i < numInputs; i++) {
        displaySensor(inputs[i], currentLine);
        currentLine++;
    }
}

void clearLCD() {
    lcd.clear();
}

void showConfigModeMessage() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CONFIG MODE");
    lcd.setCursor(0, 1);
    lcd.print("Use serial console");
}

void enableLCD() {
    lcd.backlight();
}

void disableLCD() {
    lcd.noBacklight();
    lcd.clear();
}

#else

void initLCD() {}
void displaySensor(Input *ptr, byte line) {}
void updateLCD(Input** inputs, int numInputs) {}
void clearLCD() {}
void showConfigModeMessage() {}
void enableLCD() {}
void disableLCD() {}

#endif
