/*
 * output_serial.cpp - Serial output for debugging
 */

#include "output_base.h"
#include "../config.h"

#ifdef ENABLE_SERIAL_OUTPUT

void initSerialOutput() {
    Serial.begin(115200);
    while (!Serial) {};
    Serial.println("Serial output initialized");
    Serial.println("Sensor,Value,Units");
}

void sendSerialOutput(Sensor *ptr) {
    Serial.print(ptr->abbrName);
    Serial.print(",");
    
    if (isnan(ptr->value)) {
        Serial.print("ERROR");
    } else {
        // Display in human-readable format
        float displayValue = ptr->displayConvert(ptr->value, ptr->displayUnits);
        Serial.print(displayValue, 2);
    }
    
    Serial.print(",");
    
    // Print units
    switch (ptr->displayUnits) {
        case CELSIUS: Serial.print("C"); break;
        case FAHRENHEIT: Serial.print("F"); break;
        case BAR: Serial.print("bar"); break;
        case PSI: Serial.print("psi"); break;
        case KPA: Serial.print("kPa"); break;
        case VOLTS: Serial.print("V"); break;
        case PERCENT: Serial.print("%"); break;
        case METERS: Serial.print("m"); break;
        case FEET: Serial.print("ft"); break;
    }
    
    Serial.println();
}

void updateSerialOutput() {
    // Can add header row every N seconds if desired
}

#else

void initSerialOutput() {}
void sendSerialOutput(Sensor *ptr) {}
void updateSerialOutput() {}

#endif
