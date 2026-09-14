/*
 * output_serial.cpp - Serial CSV output for data plane
 */

#include "output_base.h"
#include "../config.h"
#include "../sensors/sensor_library.h"
#include "../sensors/units_registry.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "output_format.h"

#if INCLUDE_SERIAL_OUTPUT

#include "../lib/system_mode.h"

void initSerialOutput() {
    msg.debug.info(TAG_SERIAL, "Serial output initialized");
}

void sendSerialOutput(Input *ptr) {
    // In CONFIG mode, suppress serial output
    if (isInConfigMode()) {
        return;
    }

    // Shared with the SD logger so the two recording outputs can't drift on how
    // a reading renders (see output_format.h).
    char valBuf[VALUE_FIELD_LEN];
    formatValueField(valBuf, sizeof(valBuf),
                     convertFromBaseUnits(ptr->value, ptr->unitsIndex));

    char unitBuf[UNIT_FIELD_LEN];
    formatUnitSymbol(unitBuf, sizeof(unitBuf), getUnitStringByIndex(ptr->unitsIndex));

    msg.data.print(ptr->abbrName);
    msg.data.print(",");
    msg.data.print(valBuf);
    msg.data.print(",");
    msg.data.print(unitBuf);
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
