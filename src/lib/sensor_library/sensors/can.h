/*
 * can.h - CAN Bus Imported Sensors
 *
 * Sensors imported from CAN bus (OBD-II, J1939, custom protocols).
 * Data is read from cached CAN frames rather than physical pins.
 */

#ifndef SENSOR_LIBRARY_SENSORS_CAN_H
#define SENSOR_LIBRARY_SENSORS_CAN_H

#include <Arduino.h>

// Forward declaration
struct Input;
void readCANSensor(Input* ptr);

// ===== PROGMEM STRINGS =====
static const char PSTR_CAN_IMPORT[] PROGMEM = "CAN_IMPORT";
static const char PSTR_CAN_IMPORT_LABEL[] PROGMEM = "CAN Bus Import (OBD-II/J1939)";
static const char PSTR_CAN_IMPORT_DESC[] PROGMEM = "Import sensor from CAN bus - supports OBD-II, J1939, and custom protocols";

// ===== DEFAULT CALIBRATION =====
// Default CAN sensor calibration (passthrough, OBD-II standard)
static const PROGMEM CANSensorCalibration default_can_cal = {
    .source_can_id = 0x7E8,      // OBD-II ECU response ID
    .source_pid = 0x00,          // Placeholder PID (user must configure)
    .data_offset = 0,            // Start of data payload
    .data_length = 1,            // Single byte default
    .is_big_endian = true,       // OBD-II uses big-endian
    .scale_factor = 1.0,         // No scaling by default
    .offset = 0.0                // No offset by default
};

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define CAN_SENSORS \
    X_SENSOR(PSTR_CAN_IMPORT, PSTR_CAN_IMPORT_LABEL, PSTR_CAN_IMPORT_DESC, readCANSensor, nullptr, \
             MEASURE_TEMPERATURE, CAL_CAN_IMPORT, &default_can_cal, \
             100, -273.0, 1000.0, 0x2251, PIN_ANALOG)

// Note:
// - measurementType defaults to MEASURE_TEMPERATURE but is overridden when imported from standard PID table
// - minInterval set to 100ms (typical CAN broadcast rate)
// - hash 0x2251 = djb2_hash("CAN_IMPORT")
// - PIN_ANALOG is placeholder (CAN sensors use virtual pins 0xC0-0xDF)

#endif // SENSOR_LIBRARY_SENSORS_CAN_H
