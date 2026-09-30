/*
 * obd_output_pids.cpp - see obd_output_pids.h (#516)
 */

#include "obd_output_pids.h"
#include "standard_pids.h"
#include <string.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static const char PSTR_O_C[] PROGMEM = "C";
static const char PSTR_O_KPA[] PROGMEM = "KPA";
static const char PSTR_O_V[] PROGMEM = "V";
static const char PSTR_O_PERCENT[] PROGMEM = "PERCENT";
static const char PSTR_O_M[] PROGMEM = "M";

// Sorted by PID. preOBD rows: temperatures use J1979's own wide temperature
// layout (catalyst temperature 0x3C, (256A+B)/10-40, -40..6513 degC); gauge
// pressures 1 kPa steps from -100 (vacuum) to 65435 kPa, enough for brake
// and hydraulic pressure.
const PROGMEM ObdOutputPid OBD_OUTPUT_TABLE[] = {
    //  PID  len sup off vlen flags             type                 scale      offset  unit
    {0x70, 10, 0x02, 3, 2, OBD_OUT_ABSOLUTE, MEASURE_PRESSURE,    0.03125f,    0.0f, PSTR_O_KPA},     // boost sensor A, (256D+E)/32 kPa abs
    {0x78,  9, 0x01, 1, 2, 0,                MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // EGT B1S1, (256B+C)/10-40
    {0xAA,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // CHT
    {0xAB,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // transfer case temp
    {0xAC,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // transmission temp
    {0xAD,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // differential temp
    {0xAE,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // gear oil temp
    {0xAF,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_TEMPERATURE, 0.1f,      -40.0f, PSTR_O_C},       // intercooler temp
    {0xB0,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_PRESSURE,    1.0f,     -100.0f, PSTR_O_KPA},     // oil pressure (gauge)
    {0xB1,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_PRESSURE,    1.0f,     -100.0f, PSTR_O_KPA},     // hydraulic pressure
    {0xB2,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_PRESSURE,    1.0f,     -100.0f, PSTR_O_KPA},     // brake pressure
    {0xB3,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_VOLTAGE,     0.001f,      0.0f, PSTR_O_V},       // auxiliary battery
    {0xB4,  1, 0,    0, 1, OBD_OUT_PREOBD,   MEASURE_HUMIDITY,    0.392157f,   0.0f, PSTR_O_PERCENT}, // humidity, 100A/255
    {0xB5,  2, 0,    0, 2, OBD_OUT_PREOBD,   MEASURE_ELEVATION,   1.0f,     -500.0f, PSTR_O_M},       // altitude
    {0xB6,  1, 0,    0, 1, OBD_OUT_PREOBD,   MEASURE_DIGITAL,     (1.0f / 255.0f), 0.0f, nullptr},    // coolant level: 0 / 255
    {0xB7,  1, 0,    0, 1, OBD_OUT_PREOBD,   MEASURE_DIGITAL,     (1.0f / 255.0f), 0.0f, nullptr},    // oil level: 0 / 255
};

static_assert(sizeof(OBD_OUTPUT_TABLE) / sizeof(OBD_OUTPUT_TABLE[0]) == NUM_OBD_OUTPUT_PIDS,
              "NUM_OBD_OUTPUT_PIDS (obd_output_pids.h) has drifted from OBD_OUTPUT_TABLE");

// J1979 absolute pressures among the single-value rows: a boost (gauge)
// input sent on them has barometric pressure added
FLASHMEM static bool isAbsolutePressurePid(uint8_t pid) {
    return pid == 0x0B || pid == 0x33;
}

FLASHMEM bool obdOutputSpec(uint8_t pid, ObdOutputPid& spec) {
    for (uint8_t i = 0; i < NUM_OBD_OUTPUT_PIDS; i++) {
        if (pgm_read_byte(&OBD_OUTPUT_TABLE[i].pid) == pid) {
            memcpy_P(&spec, &OBD_OUTPUT_TABLE[i], sizeof(spec));
            return true;
        }
    }
    const StandardPIDInfo* row = lookupStandardPID(pid);
    if (row == nullptr) return false;
    StandardPIDInfo r;
    memcpy_P(&r, row, sizeof(r));
    // A multi-sensor import row (0x66, 0x67) describes one sensor of a longer
    // reply: not answered from here (it would be truncated)
    if (r.data_offset != 0) return false;
    spec.pid = r.pid;
    spec.length = r.data_length;
    spec.support = 0;
    spec.valueOffset = 0;
    spec.valueLength = r.data_length;
    spec.flags = isAbsolutePressurePid(pid) ? OBD_OUT_ABSOLUTE : 0;
    spec.measurementType = r.measurementType;
    spec.scale = r.scale_factor;
    spec.offset = r.offset;
    spec.units = r.units;
    return true;
}

FLASHMEM uint8_t obdLengthFor(uint8_t pid, uint8_t current) {
    ObdOutputPid spec;
    if (pid != 0 && obdOutputSpec(pid, spec)) return spec.length;
    // A PID of the user's own takes 1-5 bytes; coming from a table PID
    // (boost 0x70 is 10) that length would leave the input silent
    return (current == 0 || current > 5) ? 1 : current;
}
