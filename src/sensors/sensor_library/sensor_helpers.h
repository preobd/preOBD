/*
 * sensor_helpers.h - Sensor Library Helper Functions
 *
 * Lookup and utility functions for the sensor library registry.
 * This file must be included AFTER the SENSOR_LIBRARY array is defined.
 */

#ifndef SENSOR_LIBRARY_HELPERS_H
#define SENSOR_LIBRARY_HELPERS_H

#include <Arduino.h>
#include "sensor_types.h"
#include "sensor_categories.h"
#include "../../lib/hash.h"
#include "../../lib/bus_needs.h"   // BUS_NEED_* for busNeedsForSensorIndex()

// Helper macros for reading individual fields from PROGMEM
#define READ_SENSOR_NAME(info) ((const char*)pgm_read_ptr(&(info)->name))
#define READ_SENSOR_LABEL(info) ((const char*)pgm_read_ptr(&(info)->label))
#define READ_SENSOR_DESCRIPTION(info) ((const char*)pgm_read_ptr(&(info)->description))
#define READ_SENSOR_MIN_VALUE(info) pgm_read_float(&(info)->minValue)
#define READ_SENSOR_MAX_VALUE(info) pgm_read_float(&(info)->maxValue)

// ===== SENSOR LOOKUP FUNCTIONS =====

/**
 * Get Sensor info from flash memory (O(1) direct array indexing)
 * Validates that the sensor entry is implemented (has non-null label)
 */
inline const SensorInfo* getSensorInfo(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    const SensorInfo* info = &SENSOR_LIBRARY[index];
    // Validate entry (check if label is non-null for implemented sensors)
    if (pgm_read_ptr(&info->label) == nullptr) return nullptr;
    return info;
}

/**
 * Get Sensor info by index (same as above, different parameter type)
 * Does not validate - returns entry even if not implemented
 */
inline const SensorInfo* getSensorByIndex(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    return &SENSOR_LIBRARY[index];
}

/**
 * Get Sensor index by name hash (O(n) search)
 */
inline uint8_t getSensorIndexByHash(uint16_t hash) {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        uint16_t sensorHash = pgm_read_word(&SENSOR_LIBRARY[i].nameHash);
        if (sensorHash == hash) {
            return i;
        }
    }
    return 0;  // SENSOR_NONE
}

/**
 * Get Sensor index by name (O(n) search)
 */
inline uint8_t getSensorIndexByName(const char* name) {
    if (!name) return 0;
    uint16_t hash = djb2_hash(name);
    return getSensorIndexByHash(hash);
}

/**
 * Load entire sensor info from PROGMEM into RAM (cleaner code)
 */
inline void loadSensorInfo(const SensorInfo* flashInfo, SensorInfo* ramCopy) {
    memcpy_P(ramCopy, flashInfo, sizeof(SensorInfo));
}

/**
 * Get sensor measurement type from SENSOR_LIBRARY (O(1) direct array indexing)
 */
inline MeasurementType getSensorMeasurementType(uint8_t index) {
    if (index >= NUM_SENSORS) return MEASURE_TEMPERATURE;
    return (MeasurementType)pgm_read_byte(&SENSOR_LIBRARY[index].measurementType);
}

/**
 * Get sensor name by index (reverse lookup for JSON export)
 *
 * @param index  Sensor index (0-NUM_SENSORS)
 * @return       Sensor name string in PROGMEM, or nullptr if invalid
 */
inline const char* getSensorNameByIndex(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    return READ_SENSOR_NAME(&SENSOR_LIBRARY[index]);
}

// ===== CATEGORY-DEPENDENT HELPER FUNCTIONS =====

/**
 * Derive sensor category from existing sensor properties.
 * Categories match the sensor definition files in sensors/ directory.
 *
 * @param sensorIndex  Index into SENSOR_LIBRARY
 * @return             SensorCategory enum value
 */
inline SensorCategory getSensorCategory(uint8_t sensorIndex) {
    if (sensorIndex >= NUM_SENSORS) return CAT_THERMOCOUPLE;

    const SensorInfo* sensor = &SENSOR_LIBRARY[sensorIndex];
    MeasurementType measType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
    CalibrationType calType = (CalibrationType)pgm_read_byte(&sensor->calibrationType);
    PinTypeRequirement pinType = (PinTypeRequirement)pgm_read_byte(&sensor->pinTypeRequirement);

    // Environmental sensors (environmental.h)
    if (pinType == PIN_I2C) return CAT_ENVIRONMENTAL;

    // Digital input sensors (digital.h)
    if (measType == MEASURE_DIGITAL) return CAT_DIGITAL;

    // Frequency-based sensors - RPM and Speed (frequency.h)
    if (measType == MEASURE_RPM || measType == MEASURE_SPEED) return CAT_FREQUENCY;

    // Voltage sensors (voltage.h)
    if (measType == MEASURE_VOLTAGE) return CAT_VOLTAGE;

    // BME280 humidity/elevation are ENVIRONMENTAL
    if (measType == MEASURE_HUMIDITY || measType == MEASURE_ELEVATION) return CAT_ENVIRONMENTAL;

    // Pressure sensors (pressure.h) - all calibration types
    if (measType == MEASURE_PRESSURE) return CAT_PRESSURE;

    // Temperature sensors
    if (measType == MEASURE_TEMPERATURE) {
        // Thermocouples: CAL_NONE with digital pins (thermocouples.h)
        if (calType == CAL_NONE && pinType == PIN_DIGITAL) return CAT_THERMOCOUPLE;
        // All other temp sensors are thermistors (thermistors.h)
        return CAT_THERMISTOR;
    }

    // Fluid level sensors (level.h)
    if (measType == MEASURE_PERCENT) return CAT_LEVEL;

    return CAT_THERMOCOUPLE;  // Default fallback
}

/**
 * Which bus must be up for this sensor to be readable?
 *
 * This is the "declared, not discovered" half of #358: bus bring-up at boot is
 * driven by the stored records' sensors, so an I2C sensor no longer depends on a
 * display having been configured to bring its bus up first.
 *
 * Two facts already in the entry answer it, and neither had to be invented:
 *
 *   - pinTypeRequirement == PIN_I2C marks the I2C sensors.
 *   - The SPI-attached family is CAT_THERMOCOUPLE (MAX6675 / MAX31855) — the
 *     two sensors whose read path calls getActiveSPI(). MEASURE_DIGITAL parts
 *     (float switch) are categorized before that branch, so they are not caught.
 *
 * There is deliberately no PIN_SPI: pinTypeRequirement answers "what kind of PIN
 * does this sensor occupy", and a MAX6675 genuinely occupies a digital CS pin.
 * It is also a wire-format value — json_registry.cpp exports it and the mobile
 * app switches its pin picker on ANALOG|DIGITAL|I2C — so widening it would be a
 * cross-repo break for a fact orthogonal to it anyway.
 *
 * @return a BUS_NEED_* mask (see lib/bus_needs.h)
 */
inline uint8_t busNeedsForSensorIndex(uint8_t sensorIndex) {
    const SensorInfo* sensor = getSensorByIndex(sensorIndex);
    if (!sensor) return BUS_NEED_NONE;

    if ((PinTypeRequirement)pgm_read_byte(&sensor->pinTypeRequirement) == PIN_I2C)
        return BUS_NEED_I2C;

    if (getSensorCategory(sensorIndex) == CAT_THERMOCOUPLE) return BUS_NEED_SPI;

    return BUS_NEED_NONE;
}

/**
 * Same, keyed by a stored record's sensorHash.
 *
 * An unknown hash resolves to 0 (SENSOR_NONE), which needs no bus — the right
 * answer for a record this firmware cannot resolve.
 */
inline uint8_t busNeedsForSensorHash(uint16_t sensorHash) {
    return busNeedsForSensorIndex(getSensorIndexByHash(sensorHash));
}

/**
 * Count sensors in a category
 */
inline uint8_t countSensorsInCategory(SensorCategory cat) {
    uint8_t count = 0;
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) != nullptr) {
            if (getSensorCategory(i) == cat) count++;
        }
    }
    return count;
}

/**
 * Count sensors by measurement type
 */
inline uint8_t countSensorsByMeasurementType(MeasurementType measType) {
    uint8_t count = 0;
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) != nullptr) {
            MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
            if (sensorMeasType == measType) count++;
        }
    }
    return count;
}

/**
 * Find sensor index by category and preset name
 * Used for two-layer SET SENSOR <category> <preset> syntax
 *
 * @param cat     Category to search within
 * @param preset  Sensor name (can be partial match within category)
 * @return        Sensor index, or 0 if not found
 */
inline uint8_t getSensorIndexByCategoryAndName(SensorCategory cat, const char* preset) {
    if (!preset || cat >= CAT_COUNT) return 0;

    uint16_t presetHash = djb2_hash(preset);

    // Search for exact match within category
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) == nullptr) continue;

        if (getSensorCategory(i) == cat) {
            uint16_t sensorHash = pgm_read_word(&sensor->nameHash);
            if (sensorHash == presetHash) {
                return i;
            }
        }
    }

    return 0;  // Not found
}

#endif // SENSOR_LIBRARY_HELPERS_H
