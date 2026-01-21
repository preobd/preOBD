/*
 * sensor_categories.h - Sensor Category Definitions and Helper Functions
 *
 * Categories group sensors by technology/calibration type for two-layer selection.
 * Categories are derived from calibrationType + measurementType at runtime.
 */

#ifndef SENSOR_LIBRARY_CATEGORIES_H
#define SENSOR_LIBRARY_CATEGORIES_H

#include <Arduino.h>
#include "sensor_types.h"
#include "../hash.h"

// ===== SENSOR CATEGORY ENUMERATION =====
// Categories group sensors by technology/calibration type for two-layer selection.
// Derived from calibrationType + measurementType at runtime (no storage overhead).
enum SensorCategory : uint8_t {
    CAT_THERMOCOUPLE = 0,      // K-type thermocouple amplifiers (CAL_NONE + TEMP + PIN_DIGITAL)
    CAT_NTC_THERMISTOR,        // NTC thermistor temperature sensors (CAL_THERMISTOR_*)
    CAT_LINEAR_TEMP,           // Linear temperature sensors (CAL_LINEAR + TEMP)
    CAT_LINEAR_PRESSURE,       // Linear pressure sensors (CAL_LINEAR + PRESSURE)
    CAT_RESISTIVE_PRESSURE,    // Resistive/piezoresistive pressure senders (CAL_PRESSURE_POLYNOMIAL)
    CAT_VOLTAGE,               // Voltage measurement sensors
    CAT_RPM,                   // Engine RPM sensors
    CAT_SPEED,                 // Vehicle speed sensors
    CAT_I2C,                   // I2C bus sensors (BME280, etc.)
    CAT_DIGITAL,               // Digital input sensors (float switch, etc.)
    CAT_COUNT                  // Number of categories
};

// Sensor category info structure for display and lookup
struct SensorCategoryInfo {
    const char* name;          // Primary key: "THERMOCOUPLE", "NTC_THERMISTOR"
    const char* label;         // Display label: "K-Type Thermocouples"
    uint16_t nameHash;         // Precomputed hash for fast lookup
};

// ===== SENSOR CATEGORY STRINGS (PROGMEM) =====
static const char PSTR_CAT_THERMOCOUPLE[] PROGMEM = "THERMOCOUPLE";
static const char PSTR_CAT_THERMOCOUPLE_LABEL[] PROGMEM = "K-Type Thermocouples";
static const char PSTR_CAT_NTC_THERMISTOR[] PROGMEM = "NTC_THERMISTOR";
static const char PSTR_CAT_NTC_THERMISTOR_LABEL[] PROGMEM = "NTC Thermistors";
static const char PSTR_CAT_LINEAR_TEMP[] PROGMEM = "LINEAR_TEMP";
static const char PSTR_CAT_LINEAR_TEMP_LABEL[] PROGMEM = "Linear Temperature Sensors";
static const char PSTR_CAT_LINEAR_PRESSURE[] PROGMEM = "LINEAR_PRESSURE";
static const char PSTR_CAT_LINEAR_PRESSURE_LABEL[] PROGMEM = "Linear Pressure Sensors";
static const char PSTR_CAT_RESISTIVE_PRESSURE[] PROGMEM = "RESISTIVE_PRESSURE";
static const char PSTR_CAT_RESISTIVE_PRESSURE_LABEL[] PROGMEM = "Resistive Pressure Senders";
static const char PSTR_CAT_VOLTAGE[] PROGMEM = "VOLTAGE";
static const char PSTR_CAT_VOLTAGE_LABEL[] PROGMEM = "Voltage Sensors";
static const char PSTR_CAT_RPM[] PROGMEM = "RPM";
static const char PSTR_CAT_RPM_LABEL[] PROGMEM = "RPM Sensors";
static const char PSTR_CAT_SPEED[] PROGMEM = "SPEED";
static const char PSTR_CAT_SPEED_LABEL[] PROGMEM = "Speed Sensors";
static const char PSTR_CAT_I2C[] PROGMEM = "I2C";
static const char PSTR_CAT_I2C_LABEL[] PROGMEM = "I2C Bus Sensors";
static const char PSTR_CAT_DIGITAL[] PROGMEM = "DIGITAL";
static const char PSTR_CAT_DIGITAL_LABEL[] PROGMEM = "Digital Input Sensors";

// ===== SENSOR CATEGORY REGISTRY (PROGMEM) =====
// Hash values computed with: python3 -c "h=5381; s='NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
static const PROGMEM SensorCategoryInfo SENSOR_CATEGORIES[] = {
    { PSTR_CAT_THERMOCOUPLE,       PSTR_CAT_THERMOCOUPLE_LABEL,       0xA69C },  // THERMOCOUPLE
    { PSTR_CAT_NTC_THERMISTOR,     PSTR_CAT_NTC_THERMISTOR_LABEL,     0xC0BA },  // NTC_THERMISTOR
    { PSTR_CAT_LINEAR_TEMP,        PSTR_CAT_LINEAR_TEMP_LABEL,        0x9095 },  // LINEAR_TEMP
    { PSTR_CAT_LINEAR_PRESSURE,    PSTR_CAT_LINEAR_PRESSURE_LABEL,    0x91B8 },  // LINEAR_PRESSURE
    { PSTR_CAT_RESISTIVE_PRESSURE, PSTR_CAT_RESISTIVE_PRESSURE_LABEL, 0xF99B },  // RESISTIVE_PRESSURE
    { PSTR_CAT_VOLTAGE,            PSTR_CAT_VOLTAGE_LABEL,            0x03F7 },  // VOLTAGE
    { PSTR_CAT_RPM,                PSTR_CAT_RPM_LABEL,                0x1A54 },  // RPM
    { PSTR_CAT_SPEED,              PSTR_CAT_SPEED_LABEL,              0xFEF6 },  // SPEED
    { PSTR_CAT_I2C,                PSTR_CAT_I2C_LABEL,                0xF023 },  // I2C
    { PSTR_CAT_DIGITAL,            PSTR_CAT_DIGITAL_LABEL,            0x9803 },  // DIGITAL
};

// Helper macros for category info
#define READ_CATEGORY_NAME(info) ((const char*)pgm_read_ptr(&(info)->name))
#define READ_CATEGORY_LABEL(info) ((const char*)pgm_read_ptr(&(info)->label))

// ===== CATEGORY HELPER FUNCTION DECLARATIONS =====
// These are defined in sensor_helpers.h after SENSOR_LIBRARY is available

/**
 * Get category info by category enum value
 */
inline const SensorCategoryInfo* getCategoryInfo(SensorCategory cat) {
    if (cat >= CAT_COUNT) return nullptr;
    return &SENSOR_CATEGORIES[cat];
}

/**
 * Get category by name or alias (case-insensitive)
 * Supports aliases: NTC, THERMISTOR -> NTC_THERMISTOR
 *                   TC -> THERMOCOUPLE
 *                   RESISTIVE, PIEZO -> RESISTIVE_PRESSURE
 *
 * @param name  Category name or alias
 * @return      SensorCategory enum, or CAT_COUNT if not found
 */
inline SensorCategory getCategoryByName(const char* name) {
    if (!name) return CAT_COUNT;

    uint16_t hash = djb2_hash(name);

    // Check primary names first
    for (uint8_t i = 0; i < CAT_COUNT; i++) {
        uint16_t catHash = pgm_read_word(&SENSOR_CATEGORIES[i].nameHash);
        if (catHash == hash) {
            return (SensorCategory)i;
        }
    }

    // Check aliases
    // NTC -> NTC_THERMISTOR (hash 0x09CA)
    if (hash == 0x09CA) return CAT_NTC_THERMISTOR;
    // THERMISTOR -> NTC_THERMISTOR (hash 0x4556)
    if (hash == 0x4556) return CAT_NTC_THERMISTOR;
    // TC -> THERMOCOUPLE (hash 0x755C)
    if (hash == 0x755C) return CAT_THERMOCOUPLE;
    // RESISTIVE -> RESISTIVE_PRESSURE (hash 0xA9A3)
    if (hash == 0xA9A3) return CAT_RESISTIVE_PRESSURE;
    // PIEZO -> RESISTIVE_PRESSURE (hash 0xE18C)
    if (hash == 0xE18C) return CAT_RESISTIVE_PRESSURE;

    return CAT_COUNT;  // Not found
}

/**
 * Check if a name matches a measurement type filter (virtual category)
 * Returns the MeasurementType if matched, or -1 if not a measurement filter
 *
 * Supports: TEMPERATURE, PRESSURE, VOLTAGE, RPM, SPEED, HUMIDITY, ELEVATION, DIGITAL
 */
inline int8_t getMeasurementTypeFilter(const char* name) {
    if (!name) return -1;

    uint16_t hash = djb2_hash(name);

    // TEMPERATURE (hash 0x0353)
    if (hash == 0x0353) return MEASURE_TEMPERATURE;
    // PRESSURE (hash 0x233E)
    if (hash == 0x233E) return MEASURE_PRESSURE;
    // Note: VOLTAGE, RPM, SPEED, HUMIDITY, ELEVATION, DIGITAL match category names
    // so they'll be handled by getCategoryByName() first

    return -1;  // Not a measurement filter
}

#endif // SENSOR_LIBRARY_CATEGORIES_H
