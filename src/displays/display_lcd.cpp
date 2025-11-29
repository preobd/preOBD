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
byte degree[8] = {140, 146, 146, 140, 128, 128, 128, 128};
byte currentLine = 0;

// Forward declarations
void showConfigModeMessage();

void initLCD() {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, degree);
    lcd.clear();
    Serial.println("LCD initialized");
}

void displaySensor(Input *ptr, byte line) {
    // Calculate column position (0-9 for left, 10-19 for right)
    byte col = (line >= 4) ? 10 : 0;
    byte row = (line >= 4) ? line - 4 : line;

    lcd.setCursor(col, row);

    // Print sensor abbreviation
    lcd.print(ptr->abbrName);
    lcd.print(":");

    // If sensor is not enabled, show "CFG"
    if (!ptr->flags.isEnabled) {
        lcd.print("CFG");
        return;
    }

    // If display flag is off, don't show
    if (!ptr->flags.display) {
        return;
    }

    // Check for invalid values (NaN or exactly 0.0 which likely means unread in CONFIG mode)
    if (isnan(ptr->value)) {
        lcd.print("ERR");
        return;
    }

    // If value is exactly 0.0, show "---" (likely in CONFIG mode, not yet read)
    // This handles the case where sensor is enabled but system hasn't read it yet
    if (ptr->value == 0.0f) {
        lcd.print("---");
        return;
    }

    // Convert to display units
    float displayValue = getDisplayConvertFunc(ptr->measurementType)(ptr->value, ptr->displayUnits);

    // Print value with appropriate precision
    if (ptr->displayUnits == CELSIUS || ptr->displayUnits == FAHRENHEIT) {
        lcd.print(displayValue, 0);  // No decimal for temperature
        lcd.write((byte)0);          // Degree symbol
    } else if (ptr->displayUnits == VOLTS) {
        lcd.print(displayValue, 1);  // 1 decimal for voltage
        lcd.print("V");
    } else if (ptr->displayUnits == BAR || ptr->displayUnits == PSI) {
        lcd.print(displayValue, 1);  // 1 decimal for pressure
        if (ptr->displayUnits == PSI) {
            lcd.print("p");
        } else {
            lcd.print("b");
        }
    } else if (ptr->displayUnits == KPA) {
        lcd.print(displayValue, 1);
        lcd.print("k");
    } else if (ptr->displayUnits == INHG) {
        lcd.print(displayValue, 2);
        lcd.print("");
    } else if (ptr->displayUnits == PERCENT) {
        lcd.print(displayValue, 0);  // No decimal for humidity
        lcd.print("%");
    } else if (ptr->displayUnits == METERS) {
        lcd.print(displayValue, 0);  // No decimal for elevation
        lcd.print("m");
    } else if (ptr->displayUnits == FEET) {
        lcd.print(displayValue, 0);  // No decimal for elevation
        lcd.print("ft");
    } else if (ptr->displayUnits == RPM) {
        lcd.print(displayValue, 0);  // No decimal for RPM
    }
    // Clear remaining characters in field (assuming 9 chars per field)
    //lcd.print("   ");
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
