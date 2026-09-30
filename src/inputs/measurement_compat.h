/*
 * measurement_compat.h - Sensor/application measurement-type compatibility (#383)
 *
 * The rule ("a sensor that measures pressure cannot be paired with an
 * application that expects temperature") used to exist exactly once, inline
 * in cmd_set()'s combined-syntax branch (`SET <pin> <app> <sensor>`), and
 * nowhere else. Two other writers of the same pair bypassed it entirely:
 *   - JSON IMPORT calls setInputApplication()/setInputSensor() directly, so
 *     it never reached the inline check;
 *   - the CLI's own two-step form (`SET <pin> APPLICATION <x>` then
 *     `SET <pin> SENSOR <y>`) never ran it either -- set_application() does
 *     no compatibility check at all, and setInputSensor() only checked that
 *     the sensor index resolved.
 *
 * This predicate is the one place the rule lives now. input_manager.cpp
 * calls it from the admission phase of both setInputApplication() and
 * setInputSensor(), so every writer that routes through those two functions
 * -- both CLI forms and JSON import -- inherits it and cannot disagree.
 *
 * Deliberately message-free and Input-free (indices in, bool out) so it is
 * cheap to call from a messaging call site (cmd_set.cpp, for a specific
 * "measures X but Y expects Z" error) as well as from the admission phase
 * that actually enforces it.
 */

#ifndef MEASUREMENT_COMPAT_H
#define MEASUREMENT_COMPAT_H

#include <Arduino.h>
#include "../sensors/calibration_types.h"  // MeasurementType

// True iff `sensorIndex`'s physical measurement type is a valid match for
// what `applicationIndex` expects.
//
// Two non-obvious cases, deliberately answered DIFFERENTLY:
//
//   - SENSOR_NONE (0) / APP_NONE (0)  -> true. Nothing is paired yet, so
//     there is no constraint to violate. A genuine match.
//   - an index outside its table      -> FALSE. The answer is unknown, and
//     this predicate fails closed: an unverifiable pairing is exactly what
//     it exists to reject. Callers today resolve both indices first (via
//     getSensorByIndex()/getApplicationByIndex() returning nullptr), so this
//     branch is unreachable in current code — it is the default the next
//     caller inherits, and "yes" is the wrong default for a validator.
bool sensorMatchesApplication(uint8_t sensorIndex, uint8_t applicationIndex);

// The physical quantity an input measures: its sensor's type, or — with no
// sensor yet (SENSOR_NONE) — what its application expects.
MeasurementType inputMeasurementType(uint8_t sensorIndex, uint8_t applicationIndex);

// True iff `unitsIndex` is a registry unit for measurement type `type` (#458).
// HUMIDITY, PERCENT and DIGITAL form one family (they share the PERCENT unit).
// An index outside the table (UNITS_NOT_FOUND included) is false.
bool unitsMatchMeasurement(uint8_t unitsIndex, MeasurementType type);

struct Input;

// What an OBD-II CAN import measures (#496): the standard Mode 01 PID table's
// type when `sourceCanId` is an ECU response ID (0x7E8-0x7EF) and the PID is
// in the table, otherwise `fallback`. The CAN_IMPORT sensor preset says
// TEMPERATURE for every PID, so judging a CAN input by its sensor turned RPM
// and speed into "C" and refused their real units.
MeasurementType canImportMeasurementType(uint16_t sourceCanId, uint8_t pid,
                                         MeasurementType fallback);

// inputMeasurementType() for a configured input: an imported OBD-II PID
// measures what the PID table says, anything else what its sensor/app says.
// The one answer the units checks and Input::measurementType should agree on.
MeasurementType inputMeasurementTypeOf(const Input& in);

// The first registry unit of `type` (e.g. BAR for PRESSURE, KPH for SPEED;
// the PERCENT family shares PERCENT), or 0 if the registry has none.
uint8_t defaultUnitsForMeasurement(MeasurementType type);

struct StandardPIDInfo;

/**
 * Decode of a standard PID row in the input's BASE unit (#509). Rows are in
 * the PID's J1979 unit (kPa, min); inputs store base units (bar, s), so
 * scale and offset go through that unit's registry conversion:
 * base = (raw * scale + offset - unitOffset) / unitFactor.
 * `unitsIndex` is the row's own unit (the natural display unit). Returns
 * false if the row's unit is not a registry unit of its measurement type.
 * `row` must be a RAM copy (memcpy_P).
 */
bool standardPidToBase(const StandardPIDInfo& row, float& scale, float& offset,
                       uint8_t& unitsIndex);

#endif // MEASUREMENT_COMPAT_H
