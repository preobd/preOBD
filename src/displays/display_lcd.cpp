/*
 * display_lcd.cpp - LCD display module
 */

#include "../config.h"
#include "../inputs/input.h"
#include "../lib/sensor_types.h"
#include "../lib/units_registry.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#ifdef USE_STATIC_CONFIG
#include "../lib/generated/application_presets_static.h"
#include "../lib/generated/sensor_library_static.h"
#else
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#endif

#ifdef ENABLE_LCD

#include <LiquidCrystal_I2C.h>

// LCD display configuration
#define LCD_COLS 20
#define LCD_ROWS 4
#define LCD_COLUMNS_PER_SENSOR (LCD_COLS / 2)  // Split display into 2 columns

LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);
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
    msg.debug.info(TAG_DISPLAY, "LCD initialized");
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
            return 32;  // Generic pressure icon
        case MEASURE_VOLTAGE:
            return ICON_BATTERY;
        case MEASURE_RPM:
            return ICON_TACHOMETER;
        default:
            return 32; // space
    }
}

void displaySensor(Input *ptr, byte line) {
    // Calculate column position based on LCD_COLUMNS_PER_SENSOR
    byte col = (line >= LCD_ROWS) ? LCD_COLUMNS_PER_SENSOR : 0;
    byte row = (line >= LCD_ROWS) ? line - LCD_ROWS : line;

    lcd.setCursor(col, row);

    int charsPrinted = 0;

    if (ptr->flags.display) {
        // Print icon
        if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
            lcd.write(getIconForApplication(ptr->applicationIndex));
            charsPrinted++;
        }

        // Print sensor abbreviation
        if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
            int abbrLen = strlen(ptr->abbrName);
            int charsAvailable = LCD_COLUMNS_PER_SENSOR - charsPrinted;
            if (abbrLen > charsAvailable) abbrLen = charsAvailable;
            for (int i = 0; i < abbrLen; i++) {
                lcd.print(ptr->abbrName[i]);
            }
            charsPrinted += abbrLen;
        }

        // Print colon
        if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
            lcd.print(":");
            charsPrinted++;
        }

        // If sensor is not enabled, show "CFG"
        if (!ptr->flags.isEnabled) {
            if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
                int remaining = LCD_COLUMNS_PER_SENSOR - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("CFG");
                } else {
                    // Print as much as we can fit
                    const char* msg = "CFG";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
        }
        // Check for invalid values (NaN)
        else if (isnan(ptr->value)) {
            if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
                int remaining = LCD_COLUMNS_PER_SENSOR - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("ERR");
                } else {
                    const char* msg = "ERR";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
        }
        // If value is exactly 0.0, show "---" (likely in CONFIG mode, not yet read)
        else if (ptr->value == 0.0f) {
            if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
                int remaining = LCD_COLUMNS_PER_SENSOR - charsPrinted;
                if (remaining >= 3) {
                    charsPrinted += lcd.print("---");
                } else {
                    const char* msg = "---";
                    for (int i = 0; i < remaining; i++) {
                        lcd.print(msg[i]);
                        charsPrinted++;
                    }
                }
            }
        } else {
            // Convert to display units
            float displayValue = convertFromBaseUnits(ptr->value, ptr->unitsIndex);

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
                case MEASURE_SPEED:
                case MEASURE_DIGITAL:
                    decimals = 0;  // No decimals for these
                    break;
                case MEASURE_PRESSURE:
                    // inHg uses 2 decimals, others use 1
                    decimals = (ptr->unitsIndex == getUnitsIndexByName("INHG")) ? 2 : 1;
                    break;
                case MEASURE_VOLTAGE:
                    decimals = 1;  // 1 decimal for voltage
                    break;
            }

            // Print value with appropriate precision (if space available)
            if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
                // Format the value to a string first to check length
                char valBuffer[12];
                dtostrf(displayValue, 1, decimals, valBuffer);
                int valLen = strlen(valBuffer);
                int charsAvailable = LCD_COLUMNS_PER_SENSOR - charsPrinted;

                // If value + unit symbols won't fit, truncate
                if (valLen > charsAvailable) {
                    valLen = charsAvailable;
                }

                for (int i = 0; i < valLen; i++) {
                    lcd.print(valBuffer[i]);
                    charsPrinted++;
                }
            }

            // Print unit symbol from registry (if space available)
            if (charsPrinted < LCD_COLUMNS_PER_SENSOR) {
                if (measType == MEASURE_TEMPERATURE) {
                    // Temperature gets a degree symbol before the unit
                    lcd.write((byte)ICON_DEGREE);
                    charsPrinted++;
                } else if (unitInfo) {
                    // Use symbol from registry
                    const char* symPtr = (const char*)pgm_read_ptr(&unitInfo->symbol);
                    if (symPtr) {
                        int symLen = strlen_P(symPtr);
                        int charsAvailable = LCD_COLUMNS_PER_SENSOR - charsPrinted;
                        if (symLen > charsAvailable) symLen = charsAvailable;

                        for (int i = 0; i < symLen; i++) {
                            char c = pgm_read_byte(symPtr + i);
                            lcd.print(c);
                            charsPrinted++;
                        }
                    }
                }
            }
        }
    }

    // Pad remaining space in the column to clear old data
    for (int i = charsPrinted; i < LCD_COLUMNS_PER_SENSOR; i++) {
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
