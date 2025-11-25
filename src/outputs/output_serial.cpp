/*
 * output_serial.cpp - Serial output for debugging
 */

#include "output_base.h"
#include "../config.h"
#include "../sensor_library.h"

#ifdef ENABLE_SERIAL_OUTPUT

void initSerialOutput() {
    Serial.println("Serial output initialized");
    Serial.println("Sensor,Value,Units");
}

void sendSerialOutput(Input *ptr) {
    Serial.print(ptr->abbrName);
    Serial.print(",");

    if (isnan(ptr->value)) {
        Serial.print("ERROR");
    } else {
        // Display in human-readable format
        float displayValue = getDisplayConvertFunc(ptr->measurementType)(ptr->value, ptr->displayUnits);
        Serial.print(displayValue, 2);
    }

    Serial.print(",");
    Serial.print(getUnitString(ptr->displayUnits));
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
