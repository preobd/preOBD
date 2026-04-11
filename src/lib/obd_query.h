/*
 * obd_query.h — Platform-agnostic OBD-II Mode 01 query layer
 *
 * Shared between output_can.cpp (CAN path) and output_elm327.cpp (BLE path).
 * Extracted from output_can.cpp static functions so both output modules can
 * serve the same PID data without duplicating the lookup table.
 *
 * Call obdQuery_buildLookupTable() once after initOutputModules() in setup(),
 * and again after any SAVE command that modifies inputs.
 */

#ifndef OBD_QUERY_H
#define OBD_QUERY_H

#include <stdint.h>

struct Input;  // Forward declaration

// Build (or rebuild) the PID lookup table from currently enabled inputs.
void obdQuery_buildLookupTable();

// Resolve a PID to an Input pointer. Returns nullptr if not found.
Input* obdQuery_findByPID(uint8_t pid);

// Fill a 4-byte bitmap of supported PIDs 0x01–0x20.
void obdQuery_getSupportedPIDBitmap(uint8_t* bitmap4);

// Resolve a Mode 01 OBD-II query to a response payload.
// Handles mode check, PID 00 special case, lookup, and frame encoding.
// On success: fills out[] with response bytes (0x41, pid, data...) and sets *outLen.
// Returns false if mode/PID unsupported or no valid data. out[] must be >= 7 bytes.
bool obdQuery_resolve(uint8_t mode, uint8_t pid, uint8_t* out, uint8_t* outLen);

#endif // OBD_QUERY_H
