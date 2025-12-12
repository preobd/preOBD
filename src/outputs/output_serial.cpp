/*
 * output_serial.cpp - Serial output for debugging
 */

#include "output_base.h"
#include "../config.h"
#include "../lib/sensor_library.h"

#ifdef ENABLE_SERIAL_OUTPUT

#ifndef USE_STATIC_CONFIG
    #include "../lib/system_mode.h"
#endif

void initSerialOutput() {
    Serial.println("Serial output initialized");
    Serial.println("Sensor,Value,Units");
}

void sendSerialOutput(Input *ptr) {
    #ifndef USE_STATIC_CONFIG
        // In CONFIG mode, suppress serial output
        if (isInConfigMode()) {
            return;
        }
    #endif

    Serial.print(ptr->abbrName);
    Serial.print(",");

    if (isnan(ptr->value)) {
        Serial.print("ERROR");
    } else {
        // Display in human-readable format
        float displayValue = convertFromBaseUnits(ptr->value, ptr->unitsIndex);
        Serial.print(displayValue, 2);
    }

    Serial.print(",");
    Serial.print((__FlashStringHelper*)getUnitStringByIndex(ptr->unitsIndex));
    Serial.println();
}

void updateSerialOutput() {
    // Can add header row every N seconds if desired
}

#else

void initSerialOutput() {}
void sendSerialOutput(Input *ptr) {}
void updateSerialOutput() {}

#endif
