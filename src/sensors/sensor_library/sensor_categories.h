/*
 * sensor_categories.h - Sensor Category Definitions and Helper Functions
 *
 * Categories group sensors by technology/calibration type for two-layer selection.
 * Categories are derived from calibrationType + measurementType at runtime.
 *
 * The PSTR_CAT_* strings and the SENSOR_CATEGORIES[] table itself live in
 * sensor_categories.cpp, not here. This header used to define them
 * directly, which meant every translation unit that (transitively) included
 * this header via sensor_library.h, sensor_helpers.h or json_registry.cpp
 * got its own PROGMEM copy — the PROGMEM section attribute stops the
 * linker folding them back together (#400 missed this file). One
 * definition, extern here, is the same shape as sensor_library.{h,cpp} and
 * units_registry.{h,cpp}.
 */

#ifndef SENSOR_LIBRARY_CATEGORIES_H
#define SENSOR_LIBRARY_CATEGORIES_H

#include <Arduino.h>
#include "sensor_types.h"
#include "../../lib/hash.h"

// ===== SENSOR CATEGORY ENUMERATION =====
// Categories match the sensor definition files in sensors/ directory.
// Each category corresponds to one file (thermocouples.h, thermistors.h, etc.).
enum SensorCategory : uint8_t {
    CAT_THERMOCOUPLE = 0,      // K-type thermocouple amplifiers (thermocouples.h)
    CAT_THERMISTOR,            // NTC thermistors and linear temp sensors (thermistors.h)
    CAT_PRESSURE,              // All pressure sensors - linear and resistive (pressure.h)
    CAT_VOLTAGE,               // Voltage measurement sensors (voltage.h)
    CAT_FREQUENCY,             // RPM and speed sensors (frequency.h)
    CAT_ENVIRONMENTAL,         // Environmental sensors (environmental.h)
    CAT_DIGITAL,               // Digital input sensors - float switch, etc. (digital.h)
    CAT_LEVEL,                 // Fluid level sensors - fuel level senders (level.h)
    CAT_COUNT                  // Number of categories
};

// Sensor category info structure for display and lookup
struct SensorCategoryInfo {
    const char* name;          // Primary key: "THERMOCOUPLE", "NTC_THERMISTOR"
    const char* label;         // Display label: "K-Type Thermocouples"
    uint16_t nameHash;         // Precomputed hash for fast lookup
};

// ===== SENSOR CATEGORY STRINGS AND REGISTRY (PROGMEM, defined in sensor_categories.cpp) =====
// Category names match the sensor definition files in sensors/ directory.
// CAT_COUNT (enum, above) is the manifest constant for the table's extent;
// sensor_categories.cpp carries a static_assert tying it to the array's
// real extent so the two cannot silently drift.
extern const PROGMEM SensorCategoryInfo SENSOR_CATEGORIES[];

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
 * Supports aliases: NTC -> THERMISTOR
 *                   TC -> THERMOCOUPLE
 *                   RPM, SPEED -> FREQUENCY
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
    // NTC -> THERMISTOR (hash 0x09CA)
    if (hash == 0x09CA) return CAT_THERMISTOR;
    // TC -> THERMOCOUPLE (hash 0x755C)
    if (hash == 0x755C) return CAT_THERMOCOUPLE;
    // RPM -> FREQUENCY (hash 0x1A54)
    if (hash == 0x1A54) return CAT_FREQUENCY;
    // SPEED -> FREQUENCY (hash 0xFEF6)
    if (hash == 0xFEF6) return CAT_FREQUENCY;

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
