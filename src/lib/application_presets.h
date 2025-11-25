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
};

// ===== APPLICATION PRESETS (PROGMEM - Flash Memory) =====

static const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    // CHT - Cylinder Head Temperature
    // Order: application, name, displayName, defaultSensor, defaultUnits, defaultMinValue, defaultMaxValue, obd2pid, obd2length, defaultAlarmEnabled, defaultDisplayEnabled
    { CHT, "CHT", "Cylinder Head Temp", MAX6675, CELSIUS, -1, 260, 0xC8, 1, true, true },

    // EGT - Exhaust Gas Temperature
    { EGT, "EGT", "Exhaust Gas Temp", MAX31855, CELSIUS, -1, 600, 0x78, 2, true, true },

    // COOLANT_TEMP - Engine Coolant Temperature
    { COOLANT_TEMP, "WTR", "Coolant Temp", VDO_120C_LOOKUP, CELSIUS, -1, 100, 0x05, 1, true, true },

    // OIL_TEMP - Engine Oil Temperature
    { OIL_TEMP, "OIL", "Oil Temp", VDO_150C_STEINHART, CELSIUS, -1, 150, 0x5C, 1, true, true },

    // TCASE_TEMP - Transfer Case Temperature
    { TCASE_TEMP, "TRANS", "Transfer Case Temp", VDO_120C_LOOKUP, CELSIUS, -1, 100, 0xC9, 1, true, true },

    // OIL_PRESSURE - Engine Oil Pressure
    { OIL_PRESSURE, "OPS", "Oil Pressure", VDO_5BAR, BAR, 1, 5, 0xCA, 1, true, true },

    // BOOST_PRESSURE - Boost/Intake Pressure
    { BOOST_PRESSURE, "BST", "Boost Pressure", VDO_2BAR, BAR, -1, 2, 0x6F, 2, false, true },

    // PRIMARY_BATTERY - Primary Battery Voltage
    { PRIMARY_BATTERY, "BAT", "Primary Battery", VOLTAGE_DIVIDER, VOLTS, 10, 15, 0xCB, 1, false, true },

    // AUXILIARY_BATTERY - Auxiliary Battery Voltage
    { AUXILIARY_BATTERY, "AUX", "Auxiliary Battery", VOLTAGE_DIVIDER, VOLTS, 0, 0, 0xCC, 1, false, true },

    // AMBIENT_TEMP - Ambient Air Temperature (BME280)
    { AMBIENT_TEMP, "AMB", "Ambient Air Temperature", BME280_TEMP, CELSIUS, 0, 0, 0x46, 1, false, true },

    // BAROMETRIC_PRESSURE - Barometric Pressure (BME280)
    { BAROMETRIC_PRESSURE, "ABP", "Barometric Pressure", BME280_PRESSURE, BAR, 0, 0, 0x33, 1, false, true },

    // HUMIDITY - Relative Humidity (BME280)
    { HUMIDITY, " RH", "Relative Humidity", BME280_HUMIDITY, PERCENT, 0, 0, 0, 0, false, true },

    // ELEVATION - Elevation (BME280)
    { ELEVATION, "ELV", "Elevation", BME280_ELEVATION, METERS, 0, 0, 0xA1, 2, false, true },

    // COOLANT_LEVEL - Coolant Level (Float Switch)
    { COOLANT_LEVEL, "LVL", "Level", FLOAT_SWITCH, PERCENT, 0, 1, 0xA2, 1, true, true }
};

#define NUM_APPLICATION_PRESETS (sizeof(APPLICATION_PRESETS) / sizeof(ApplicationPreset))

// ===== HELPER FUNCTION =====
// Get Application preset from flash memory
inline const ApplicationPreset* getApplicationPreset(Application app) {
    for (byte i = 0; i < NUM_APPLICATION_PRESETS; i++) {
        if (pgm_read_byte(&APPLICATION_PRESETS[i].application) == app) {
            return &APPLICATION_PRESETS[i];
        }
    }
    return nullptr;
}

// Load entire preset from PROGMEM into RAM (helper for cleaner code)
inline void loadApplicationPreset(const ApplicationPreset* flashPreset, ApplicationPreset* ramCopy) {
    memcpy_P(ramCopy, flashPreset, sizeof(ApplicationPreset));
}

#endif // APPLICATION_PRESETS_H