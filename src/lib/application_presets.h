/*
 * application_presets.h - Application Type Presets
 *
 * Defines high-level application presets (CHT, OIL_PRESSURE, etc.) that provide
 * default configurations for common automotive sensors. These presets live in
 * flash memory and are used as templates when configuring inputs.
 *
 * IMPORTANT: Min/max values are stored in STANDARD UNITS:
 * - Temperature: Celsius
 * - Pressure: bar
 * - Voltage: volts
 */

#ifndef APPLICATION_PRESETS_H
#define APPLICATION_PRESETS_H

#include <Arduino.h>
#include "../inputs/input.h"

// ===== APPLICATION PRESET STRUCTURE =====
struct ApplicationPreset {
    Application application;
    const char* name;               // "CHT", "OIL" (abbreviation)
    const char* displayName;        // "Cylinder Head Temp" (full name)
    Sensor defaultSensor;           // Default hardware sensor
    Units defaultUnits;             // Default display units
    float defaultMinValue;          // Alarm minimum (STANDARD UNITS!)
    float defaultMaxValue;          // Alarm maximum (STANDARD UNITS!)
    uint8_t obd2pid;               // OBD-II PID
    uint8_t obd2length;            // OBD-II response length
    bool defaultAlarmEnabled;
    bool defaultDisplayEnabled;
    MeasurementType expectedMeasurementType;  // Expected physical quantity
};

// ===== APPLICATION PRESETS (PROGMEM - Flash Memory) =====
//
// IMPORTANT: Array order MUST match Application enum values!
// - APPLICATION_PRESETS[0] = APP_NONE
// - APPLICATION_PRESETS[1] = CHT
// - APPLICATION_PRESETS[n] = application with enum value n
//
// To add a new application:
// 1. Add enum value at END of Application enum (next sequential number)
// 2. Add ApplicationPreset entry at END of this array (index = enum value)
// 3. Update NUM_APPLICATION_PRESETS to match new array size
// 4. Do NOT insert in the middle - causes enum value shifts
//
// Placeholder entries (name = nullptr) reserve slots for undefined applications.
//
static const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    // Index 0: APP_NONE (placeholder)
    { APP_NONE, nullptr, nullptr, SENSOR_NONE, CELSIUS, 0, 0, 0, 0, false, false, MEASURE_TEMPERATURE },

    // Index 1: CHT - Cylinder Head Temperature
    // Order: application, name, displayName, defaultSensor, defaultUnits, defaultMinValue, defaultMaxValue, obd2pid, obd2length, defaultAlarmEnabled, defaultDisplayEnabled, expectedMeasurementType
    { CHT, "CHT", "Cylinder Head Temp", MAX6675, CELSIUS, -1, 260, 0xC8, 1, true, true, MEASURE_TEMPERATURE },

    // Index 2: EGT - Exhaust Gas Temperature
    { EGT, "EGT", "Exhaust Gas Temp", MAX31855, CELSIUS, -1, 600, 0x78, 2, true, true, MEASURE_TEMPERATURE },

    // Index 3: COOLANT_TEMP - Engine Coolant Temperature
    { COOLANT_TEMP, "WTR", "Coolant Temp", VDO_120C_LOOKUP, CELSIUS, -1, 100, 0x05, 1, true, true, MEASURE_TEMPERATURE },

    // Index 4: OIL_TEMP - Engine Oil Temperature
    { OIL_TEMP, "OIL", "Oil Temp", VDO_150C_STEINHART, CELSIUS, -1, 150, 0x5C, 1, true, true, MEASURE_TEMPERATURE },

    // Index 5: TCASE_TEMP - Transfer Case Temperature
    { TCASE_TEMP, "TRANS", "Transfer Case Temp", VDO_120C_LOOKUP, CELSIUS, -1, 100, 0xC9, 1, true, true, MEASURE_TEMPERATURE },

    // Index 6: AMBIENT_TEMP - Ambient Air Temperature (BME280)
    { AMBIENT_TEMP, "AMB", "Ambient Air Temperature", BME280_TEMP, CELSIUS, 0, 0, 0x46, 1, false, true, MEASURE_TEMPERATURE },

    // Index 7: OIL_PRESSURE - Engine Oil Pressure
    { OIL_PRESSURE, "OPS", "Oil Pressure", VDO_5BAR, BAR, 1, 5, 0xCA, 1, true, true, MEASURE_PRESSURE },

    // Index 8: BOOST_PRESSURE - Boost/Intake Pressure
    { BOOST_PRESSURE, "BST", "Boost Pressure", VDO_2BAR, BAR, -1, 2, 0x6F, 2, false, true, MEASURE_PRESSURE },

    // Index 9: FUEL_PRESSURE (placeholder - not yet implemented)
    { FUEL_PRESSURE, nullptr, nullptr, SENSOR_NONE, BAR, 0, 0, 0, 0, false, false, MEASURE_PRESSURE },

    // Index 10: BAROMETRIC_PRESSURE - Barometric Pressure (BME280)
    { BAROMETRIC_PRESSURE, "ABP", "Barometric Pressure", BME280_PRESSURE, BAR, 0, 0, 0x33, 1, false, true, MEASURE_PRESSURE },

    // Index 11: PRIMARY_BATTERY - Primary Battery Voltage
    { PRIMARY_BATTERY, "BAT", "Primary Battery", VOLTAGE_DIVIDER, VOLTS, 10, 15, 0xCB, 1, false, true, MEASURE_VOLTAGE },

    // Index 12: AUXILIARY_BATTERY - Auxiliary Battery Voltage
    { AUXILIARY_BATTERY, "AUX", "Auxiliary Battery", VOLTAGE_DIVIDER, VOLTS, 0, 0, 0xCC, 1, false, true, MEASURE_VOLTAGE },

    // Index 13: COOLANT_LEVEL - Coolant Level (Float Switch)
    { COOLANT_LEVEL, "LVL", "Level", FLOAT_SWITCH, PERCENT, 0, 1, 0xA2, 1, true, true, MEASURE_DIGITAL },

    // Index 14: HUMIDITY - Relative Humidity (BME280)
    { HUMIDITY, " RH", "Relative Humidity", BME280_HUMIDITY, PERCENT, 0, 0, 0, 0, false, true, MEASURE_HUMIDITY },

    // Index 15: ELEVATION - Elevation (BME280)
    { ELEVATION, "ELV", "Elevation", BME280_ELEVATION, METERS, 0, 0, 0xA1, 2, false, true, MEASURE_ELEVATION },

    // Index 16: ENGINE_RPM (placeholder - not yet implemented)
    { ENGINE_RPM, nullptr, nullptr, SENSOR_NONE, RPM, 0, 0, 0, 0, false, false, MEASURE_RPM }
};

#define NUM_APPLICATION_PRESETS 17  // Must match number of Application enum values (0-16)

// ===== HELPER FUNCTIONS =====
// Get Application preset from flash memory (O(1) direct array indexing)
inline const ApplicationPreset* getApplicationPreset(Application app) {
    if (app >= NUM_APPLICATION_PRESETS) return nullptr;
    const ApplicationPreset* preset = &APPLICATION_PRESETS[app];
    // Validate entry (check if name is non-null for implemented applications)
    if (pgm_read_ptr(&preset->name) == nullptr) return nullptr;
    return preset;
}

// Load entire preset from PROGMEM into RAM (helper for cleaner code)
inline void loadApplicationPreset(const ApplicationPreset* flashPreset, ApplicationPreset* ramCopy) {
    memcpy_P(ramCopy, flashPreset, sizeof(ApplicationPreset));
}

// Get expected measurement type for application from APPLICATION_PRESETS (O(1) direct array indexing)
inline MeasurementType getApplicationExpectedMeasurementType(Application app) {
    if (app >= NUM_APPLICATION_PRESETS) return MEASURE_TEMPERATURE;
    return (MeasurementType)pgm_read_byte(&APPLICATION_PRESETS[app].expectedMeasurementType);
}

#endif // APPLICATION_PRESETS_H