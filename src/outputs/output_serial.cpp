/*
 * output_serial.cpp - Serial CSV output for data plane
 */

#include "output_base.h"
#include "../config.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include "../lib/message_api.h"

#ifdef ENABLE_SERIAL_OUTPUT

#include "../lib/system_mode.h"

void initSerialOutput() {
    msg.data.println("✓ Serial output initialized");
}

void sendSerialOutput(Input *ptr) {
    // In CONFIG mode, suppress serial output
    if (isInConfigMode()) {
        return;
    }

    msg.data.print(ptr->abbrName);
    msg.data.print(",");

    if (isnan(ptr->value)) {
        msg.data.print("ERROR");
    } else {
        // Display in human-readable format
        float displayValue = convertFromBaseUnits(ptr->value, ptr->unitsIndex);
        msg.data.print(displayValue, 2);
    }

    msg.data.print(",");
    msg.data.print((__FlashStringHelper*)getUnitStringByIndex(ptr->unitsIndex));
    msg.data.println();
}

void updateSerialOutput() {
    // Can add header row every N seconds if desired
}

#else

void initSerialOutput() {}
void sendSerialOutput(Input *ptr) {}
void updateSerialOutput() {}

#endif
