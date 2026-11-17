/*
 * output_serial.cpp - CSV lines on a stream (#612: the output's own transport)
 */

#include "../table/output_base.h"
#include "../../config.h"
#include "../../sensors/sensor_library.h"
#include "../../sensors/units_registry.h"
#include "output_format.h"

#if INCLUDE_SERIAL_OUTPUT

// One input as `ABBR,value,units`. Shared with the SD logger's rendering so
// the two recording outputs can't drift on how a reading looks
// (output_format.h). The caller paces it and keeps it quiet in CONFIG.
void sendCsvLine(Input* ptr, Print& out) {
    char valBuf[VALUE_FIELD_LEN];
    formatValueField(valBuf, sizeof(valBuf),
                     convertFromBaseUnits(ptr->value, ptr->unitsIndex));

    char unitBuf[UNIT_FIELD_LEN];
    formatUnitSymbol(unitBuf, sizeof(unitBuf), getUnitStringByIndex(ptr->unitsIndex));

    out.print(ptr->abbrName);
    out.print(',');
    out.print(valBuf);
    out.print(',');
    out.print(unitBuf);
    out.println();
}

#else

void sendCsvLine(Input*, Print&) {}

#endif
