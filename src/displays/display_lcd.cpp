/*
 * display_lcd.cpp - LCD display module
 * Works with both EEPROM config and compile-time config
 */

#include "../config.h"
#include "../inputs/input.h"
#include "../lib/sensor_types.h"
#include "../lib/sensor_library.h"

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
byte getIconForApplication(Application app);

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

byte getIconForApplication(Application app) {
    switch (app) {
        case CHT:
        case EGT:
        case TCASE_TEMP:
        case AMBIENT_TEMP:
            return ICON_THERMOMETER;
        case COOLANT_TEMP:
            return ICON_COOLANT;
        case OIL_TEMP:
        case OIL_PRESSURE:
            return ICON_OIL;
        case FUEL_PRESSURE:
            return ICON_OIL_CAN;
        case BOOST_PRESSURE:
            return ICON_TURBO;
        case PRIMARY_BATTERY:
        case AUXILIARY_BATTERY:
            return ICON_BATTERY;
        case ENGINE_RPM:
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
        lcd.write(getIconForApplication(ptr->application));
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
            float displayValue = getDisplayConvertFunc(ptr->measurementType)(ptr->value, ptr->displayUnits);

            // Print value with appropriate precision
            if (ptr->displayUnits == CELSIUS || ptr->displayUnits == FAHRENHEIT) {
                charsPrinted += lcd.print(displayValue, 0);  // No decimal for temperature
                lcd.write((byte)ICON_DEGREE);
                charsPrinted++;
            } else if (ptr->displayUnits == VOLTS) {
                charsPrinted += lcd.print(displayValue, 1);  // 1 decimal for voltage
                charsPrinted += lcd.print("V");
            } else if (ptr->displayUnits == BAR || ptr->displayUnits == PSI) {
                charsPrinted += lcd.print(displayValue, 1);  // 1 decimal for pressure
                if (ptr->displayUnits == PSI) {
                    charsPrinted += lcd.print("p");
                } else {
                    charsPrinted += lcd.print("b");
                }
            } else if (ptr->displayUnits == KPA) {
                charsPrinted += lcd.print(displayValue, 1);
                charsPrinted += lcd.print("k");
            } else if (ptr->displayUnits == INHG) {
                charsPrinted += lcd.print(displayValue, 2);
            } else if (ptr->displayUnits == PERCENT) {
                charsPrinted += lcd.print(displayValue, 0);  // No decimal for humidity
                charsPrinted += lcd.print("%");
            } else if (ptr->displayUnits == METERS) {
                charsPrinted += lcd.print(displayValue, 0);  // No decimal for elevation
                charsPrinted += lcd.print("m");
            } else if (ptr->displayUnits == FEET) {
                charsPrinted += lcd.print(displayValue, 0);  // No decimal for elevation
                charsPrinted += lcd.print("ft");
            } else if (ptr->displayUnits == RPM) {
                charsPrinted += lcd.print(displayValue, 0);  // No decimal for RPM
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

#else

void initLCD() {}
void displaySensor(Input *ptr, byte line) {}
void updateLCD(Input** inputs, int numInputs) {}
void clearLCD() {}
void showConfigModeMessage() {}

#endif
