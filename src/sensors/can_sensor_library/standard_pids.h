/*
 * standard_pids.h - Standard OBD-II PID Database
 *
 * PROGMEM table of OBD-II Service 01 PIDs that SET CAN can import, with the
 * name, decode (byte offset/length, scale, offset) and unit of each. Also
 * names PIDs in SCAN output and feeds LIST PIDS JSON.
 *
 * SOURCES. Every row was checked against three independent tabulations of
 * SAE J1979 on 2026-09-30 (#509), after the original table turned out to be
 * wrong in 16 of 31 rows:
 *   - Wikipedia, "OBD-II PIDs", Service 01 table
 *   - python-OBD, obd/commands.py + obd/UnitsAndScaling.py (brendan-w)
 *   - CSS Electronics, "OBD2 PID table" (J1979 DBC: bit start/length, scale)
 * A PID goes in only when the sources agree on its layout. Left out on
 * purpose: non-scalar data (0xA4 gear ratio), 0x6F (no source gives its byte
 * layout), and anything that is not a J1979 PID. Replies longer than one CAN
 * frame (0x70, 0x78, 0x79, 0x7F) are in since CAN input reassembles them
 * (#527).
 *
 * MULTI-SENSOR PIDs. Some PIDs carry several values of one kind after a
 * sensor-support byte (0x78: EGT sensors 1-4). The table row is sensor 1;
 * STANDARD_PID_SENSORS lists the others, which differ from it only in where
 * the value starts and what it is called (`SET CAN <pid> SENSOR <n>`, #535).
 * Byte A says which of them the ECU has: `support_bit` is the value's bit
 * there, and CAN input reads a value whose bit is clear as no value, not as
 * whatever the ECU leaves in its place.
 *
 * Rows are in the PID's J1979 unit (kPa, min, L/h ...). SET CAN converts the
 * scale and offset to the input's base unit through the units registry, so
 * `units` must be a registry name or alias of the row's measurement type
 * (test_standard_pids checks that, and that it fits SET CAN's buffer).
 *
 * The table is defined once, in standard_pids.cpp (see units_registry.h for
 * why: a PROGMEM table in a header is duplicated per including file).
 */

#ifndef STANDARD_PIDS_H
#define STANDARD_PIDS_H

#include <Arduino.h>
#include "../calibration_types.h"

// ===== STANDARD PID INFO STRUCTURE =====

struct StandardPIDInfo {
    uint8_t pid;                    // OBD-II PID number (0x00-0xFF)
    const char* name;               // Full name (e.g., "Engine RPM") (PROGMEM)
    const char* abbr;               // Abbreviation for display (e.g., "RPM") (PROGMEM)
    uint8_t data_offset;            // First value byte, counted from A (1 skips a sensor-support byte)
    uint8_t data_length;            // Value bytes (1, 2 or 4)
    MeasurementType measurementType; // Physical quantity type
    float scale_factor;             // J1979 scale: value = raw * scale + offset, in `units`
    float offset;                   // J1979 offset
    const char* units;              // Registry name or alias of the J1979 unit ("KPA", "MIN") (PROGMEM)
    uint8_t support_bit;            // Bit in byte A that says the value is there; NO_SUPPORT_BIT if none
};

#define NO_SUPPORT_BIT 0xFF

// Manifest constant: an extern array can't be sized here. standard_pids.cpp
// static_asserts it against the table.
#define NUM_STANDARD_PIDS 31

extern const PROGMEM StandardPIDInfo STANDARD_PID_TABLE[];

// Sensor 2+ of a multi-sensor PID. Length, type, scale and unit are the PID
// row's; only the value's position and the name change.
struct StandardPIDSensor {
    uint8_t pid;                    // A PID in STANDARD_PID_TABLE
    uint8_t sensor;                 // 2..; sensor 1 is the PID's own row
    uint8_t data_offset;            // First value byte, counted from A
    const char* name;               // (PROGMEM)
    const char* abbr;               // (PROGMEM)
    uint8_t support_bit;            // Bit in byte A that says this sensor is there
};

#define NUM_STANDARD_PID_SENSORS 11

extern const PROGMEM StandardPIDSensor STANDARD_PID_SENSORS[];

// ===== HELPER FUNCTIONS =====

/**
 * Lookup standard PID info by PID number
 * Linear search through PROGMEM table
 *
 * @param pid   OBD-II PID to lookup
 * @return      Pointer to StandardPIDInfo in PROGMEM, or nullptr if not found
 */
inline const StandardPIDInfo* lookupStandardPID(uint8_t pid) {
    for (uint8_t i = 0; i < NUM_STANDARD_PIDS; i++) {
        uint8_t table_pid = pgm_read_byte(&STANDARD_PID_TABLE[i].pid);
        if (table_pid == pid) {
            return &STANDARD_PID_TABLE[i];
        }
    }
    return nullptr;  // Not found
}

/**
 * Sensor `sensor` (2..) of a multi-sensor PID, or nullptr if the PID has no
 * such sensor. Returns a PROGMEM pointer.
 */
inline const StandardPIDSensor* lookupStandardPIDSensor(uint8_t pid, uint8_t sensor) {
    for (uint8_t i = 0; i < NUM_STANDARD_PID_SENSORS; i++) {
        if (pgm_read_byte(&STANDARD_PID_SENSORS[i].pid) == pid &&
            pgm_read_byte(&STANDARD_PID_SENSORS[i].sensor) == sensor) {
            return &STANDARD_PID_SENSORS[i];
        }
    }
    return nullptr;
}

/**
 * The support-byte bit of the value at `dataOffset` of a multi-sensor PID,
 * or NO_SUPPORT_BIT when the PID has no support byte or no table value
 * starts there (a custom JSON offset).
 */
inline uint8_t standardPidSupportBit(uint8_t pid, uint8_t dataOffset) {
    const StandardPIDInfo* row = lookupStandardPID(pid);
    if (!row) return NO_SUPPORT_BIT;
    if (pgm_read_byte(&row->data_offset) == dataOffset) return pgm_read_byte(&row->support_bit);
    for (uint8_t i = 0; i < NUM_STANDARD_PID_SENSORS; i++) {
        if (pgm_read_byte(&STANDARD_PID_SENSORS[i].pid) == pid &&
            pgm_read_byte(&STANDARD_PID_SENSORS[i].data_offset) == dataOffset) {
            return pgm_read_byte(&STANDARD_PID_SENSORS[i].support_bit);
        }
    }
    return NO_SUPPORT_BIT;
}

/** How many sensors a PID in the table carries: 1, or more for 0x78 etc. */
inline uint8_t standardPIDSensorCount(uint8_t pid) {
    uint8_t n = 1;
    for (uint8_t i = 0; i < NUM_STANDARD_PID_SENSORS; i++) {
        if (pgm_read_byte(&STANDARD_PID_SENSORS[i].pid) == pid) n++;
    }
    return n;
}

/**
 * Read PID name from PROGMEM into RAM buffer
 *
 * @param pid_info  Pointer to StandardPIDInfo in PROGMEM
 * @param buffer    Destination buffer (at least 32 bytes)
 * @param max_len   Maximum buffer length
 */
inline void getStandardPIDName(const StandardPIDInfo* pid_info, char* buffer, uint8_t max_len) {
    if (!pid_info || !buffer) return;
    const char* name = (const char*)pgm_read_ptr(&pid_info->name);
    strncpy_P(buffer, name, max_len - 1);
    buffer[max_len - 1] = '\0';
}

/**
 * Read PID abbreviation from PROGMEM into RAM buffer
 *
 * @param pid_info  Pointer to StandardPIDInfo in PROGMEM
 * @param buffer    Destination buffer (at least 8 bytes)
 * @param max_len   Maximum buffer length
 */
inline void getStandardPIDAbbr(const StandardPIDInfo* pid_info, char* buffer, uint8_t max_len) {
    if (!pid_info || !buffer) return;
    const char* abbr = (const char*)pgm_read_ptr(&pid_info->abbr);
    strncpy_P(buffer, abbr, max_len - 1);
    buffer[max_len - 1] = '\0';
}

#endif // STANDARD_PIDS_H
