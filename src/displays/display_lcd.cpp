/*
 * display_lcd.cpp - LCD display module
 */

#include "../sensor_types.h"
#include "../config.h"

#ifdef ENABLE_LCD

#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
byte degree[8] = {140, 146, 146, 140, 128, 128, 128, 128};
byte currentLine = 0;

void initLCD() {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, degree);
    lcd.clear();
    Serial.println("LCD initialized");
}

void displaySensor(Sensor *ptr, byte line) {
    if (!ptr->isEnabled || !ptr->display) {
        return;
    }
    
    // Calculate column position (0-9 for left, 10-19 for right)
    byte col = (line >= 4) ? 10 : 0;
    byte row = (line >= 4) ? line - 4 : line;
    
    lcd.setCursor(col, row);
    
    // Print sensor abbreviation
    lcd.print(ptr->abbrName);
    lcd.print(":");
    
    if (isnan(ptr->value)) {
        lcd.print("ERR");
        return;
    }
    
    // Convert to display units
    float displayValue = ptr->displayConvert(ptr->value, ptr->displayUnits);
    
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
        lcd.print(displayValue, 1);  // No decimal for kPa
        lcd.print("k");
    }
    
    // Clear remaining characters in field (assuming 9 chars per field)
    lcd.print("   ");
}

void updateLCD(Sensor** sensors, int numSensors) {
    currentLine = 0;
    
    for (int i = 0; i < numSensors; i++) {
        if (sensors[i]->isEnabled && sensors[i]->display) {
            displaySensor(sensors[i], currentLine);
            currentLine++;
        }
    }
}

void clearLCD() {
    lcd.clear();
}

#else

void initLCD() {}
void displaySensor(Sensor *ptr, byte line) {}
void updateLCD(Sensor** sensors, int numSensors) {}
void clearLCD() {}

#endif
