/*
 * obd_output_pids.h - How preOBD answers each OBD-II Service 01 PID (#516)
 *
 * The point of the OBD-II output is that a standard tool (Torque, a handheld
 * scanner, Car Scanner) shows an analog sensor wired to preOBD with no setup.
 * So every preOBD application answers on the SAE J1979 PID that means the
 * same thing, with J1979's exact encoding, wherever one exists; only the
 * sensors J1979 has no PID for use preOBD's own block.
 *
 * A PID's encoding is resolved in this order:
 *  1. OBD_OUTPUT_TABLE: J1979 PIDs whose reply is more than a plain value
 *     (a sensor-support byte, several sensors, multi-frame: 0x70, 0x78) and
 *     the preOBD block 0xAA-0xB7.
 *  2. STANDARD_PID_TABLE (standard_pids.h, #509): every single-value J1979
 *     PID, encoded as the exact inverse of how SET CAN decodes it, so the two
 *     directions cannot drift apart.
 *  3. Neither: a PID the user picked themselves (SET <pin> OBD 0xE5). It keeps
 *     preOBD's legacy per-measurement encoding and the input's obd2length.
 *
 * SOURCES for the J1979 rows here, checked 2026-09-30 (#516):
 *   0x70 boost pressure control: 10 bytes; D-E = boost pressure sensor A,
 *        (256D+E)/32 kPa absolute (DashLogic J1979 listing; Wikipedia, whose
 *        range 0-2047.97 kPa shows the formula cell's "/0.03125" is a typo
 *        for x0.03125). A = support, bit 1 = sensor A (DashLogic only; a
 *        tool reading D-E gets the value either way). J = control status,
 *        0 (none reported).
 *   0x78 EGT bank 1: 9 bytes; A = support (bit 0 = sensor 1), B-C = sensor 1,
 *        (256B+C)/10 - 40 degC. Wikipedia (PID 78/79 section), DashLogic.
 * Left out on purpose: 0x77 charge-air cooler temperature (only one source
 * gives its layout), 0x6F (turbo compressor INLET pressure, not boost).
 *
 * preOBD block 0xAA-0xBF: unassigned in the J1979 tables of both Wikipedia
 * and CSS Electronics. A tool needs a custom-PID entry for these; the
 * formulas are in docs/reference/OBD2_PID_REFERENCE.md.
 */

#ifndef OBD_OUTPUT_PIDS_H
#define OBD_OUTPUT_PIDS_H

#include <Arduino.h>
#include "../calibration_types.h"

#define OBD_OUT_PREOBD   0x01   // preOBD-defined PID (0xAA-0xBF), not J1979
#define OBD_OUT_ABSOLUTE 0x02   // absolute pressure: a boost (gauge) input adds barometric

#define OBD_OUT_MAX_LENGTH 10   // longest reply payload (0x70), bytes after mode + PID

struct ObdOutputPid {
    uint8_t pid;
    uint8_t length;          // payload bytes after mode + PID (>5 needs a multi-frame reply)
    uint8_t support;         // byte A when the PID leads with a sensor-support byte, else 0
    uint8_t valueOffset;     // first byte of the value within the payload
    uint8_t valueLength;     // 1, 2 or 4
    uint8_t flags;           // OBD_OUT_*
    MeasurementType measurementType;
    float scale;             // value = raw * scale + offset, in `units`
    float offset;
    const char* units;       // registry name/alias (PROGMEM); nullptr: no unit (DIGITAL 0/1)
};

#define NUM_OBD_OUTPUT_PIDS 16
extern const PROGMEM ObdOutputPid OBD_OUTPUT_TABLE[];

/**
 * The encoding of `pid`, resolved as described above, into `spec` (RAM).
 * False when neither table defines the PID (the legacy case).
 */
bool obdOutputSpec(uint8_t pid, ObdOutputPid& spec);

/**
 * May an input measuring `type` answer on a PID with this spec? The type must
 * match exactly: a float switch (DIGITAL, 0/1) is not a percentage.
 */
inline bool obdSpecAccepts(const ObdOutputPid& spec, MeasurementType type) {
    return spec.measurementType == type;
}

/**
 * The reply length to store with a PID: the table's for a table-defined PID,
 * otherwise `current` (the user's own OBD_LENGTH; 1 when it is 0 or above 5).
 */
uint8_t obdLengthFor(uint8_t pid, uint8_t current);

#endif // OBD_OUTPUT_PIDS_H
