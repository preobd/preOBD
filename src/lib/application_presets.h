/*
 * application_presets.h - Application Type Presets (Registry Architecture)
 *
 * Defines high-level application presets (CHT, OIL_PRESSURE, etc.) that provide
 * default configurations for common automotive sensors. These presets live in
 * flash memory and are used as templates when configuring inputs.
 *
 * ARCHITECTURE:
 * - All data stored in PROGMEM (flash memory) to save RAM
 * - Hash-based name lookup for O(1) average case performance
 * - Index-based access for direct lookup
 * - Primary key: .name field for stable identification
 *
 * IMPORTANT: Min/max values are stored in STANDARD UNITS:
 * - Temperature: Celsius
 * - Pressure: bar
 * - Voltage: volts
 *
 * HOW TO ADD A NEW APPLICATION:
 * 1. Add PROGMEM strings for name and label
 * 2. Add ApplicationPreset entry to APPLICATION_PRESETS[] below with unique name
 * 3. Compute hash: python3 -c "h=5381; s='YOUR_NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
 */

#ifndef APPLICATION_PRESETS_H
#define APPLICATION_PRESETS_H

#include <Arduino.h>
#include "../inputs/input.h"
#include "hash.h"
#include "generated/registry_enums.h"

// ===== APPLICATION PRESET STRUCTURE =====
struct ApplicationPreset {
    const char* name;              // PRIMARY KEY: "CHT", "OIL_TEMP", "BOOST_PRESSURE"
    const char* abbreviation;      // Short label: "CHT", "OIL", "MAP"
    const char* label;             // Display string: "Cylinder Head Temperature", "Oil Temperature"
    const char* description;       // Help text (nullable)
    uint8_t defaultSensor;         // Default hardware sensor (index into SENSOR_LIBRARY)
    uint8_t defaultUnits;          // Default display units (index into UNITS_REGISTRY)
    float defaultMinValue;         // Alarm minimum (STANDARD UNITS!)
    float defaultMaxValue;         // Alarm maximum (STANDARD UNITS!)
    uint8_t obd2pid;              // OBD-II PID
    uint8_t obd2length;           // OBD-II response length
    bool defaultAlarmEnabled;
    bool defaultDisplayEnabled;
    MeasurementType expectedMeasurementType;  // Expected physical quantity
    uint16_t nameHash;            // Precomputed djb2_hash(name) for fast lookup
    uint16_t warmupTime_ms;       // Alarm warmup time (milliseconds, 0 = instant)
    uint16_t persistTime_ms;      // Fault persistence time (milliseconds)
};

// ===== STRING LITERALS IN PROGMEM =====
static const char PSTR_APP_NONE[] PROGMEM = "NONE";

static const char PSTR_CHT[] PROGMEM = "CHT";
static const char PSTR_CHT_ABBR[] PROGMEM = "CHT";
static const char PSTR_CHT_LABEL[] PROGMEM = "Cylinder Head Temperature";

static const char PSTR_EGT[] PROGMEM = "EGT";
static const char PSTR_EGT_ABBR[] PROGMEM = "EGT";
static const char PSTR_EGT_LABEL[] PROGMEM = "Exhaust Gas Temperature";

static const char PSTR_COOLANT_TEMP[] PROGMEM = "COOLANT_TEMP";
static const char PSTR_COOLANT_TEMP_ABBR[] PROGMEM = "WTR";
static const char PSTR_COOLANT_TEMP_LABEL[] PROGMEM = "Coolant Temperature";

static const char PSTR_OIL_TEMP[] PROGMEM = "OIL_TEMP";
static const char PSTR_OIL_TEMP_ABBR[] PROGMEM = "OIL";
static const char PSTR_OIL_TEMP_LABEL[] PROGMEM = "Oil Temperature";

static const char PSTR_TCASE_TEMP[] PROGMEM = "TCASE_TEMP";
static const char PSTR_TCASE_TEMP_ABBR[] PROGMEM = " TC";
static const char PSTR_TCASE_TEMP_LABEL[] PROGMEM = "Transfer Case Temperature";

static const char PSTR_AMBIENT_TEMP[] PROGMEM = "AMBIENT_TEMP";
static const char PSTR_AMBIENT_TEMP_ABBR[] PROGMEM = "AMB";
static const char PSTR_AMBIENT_TEMP_LABEL[] PROGMEM = "Ambient Air Temperature";

static const char PSTR_OIL_PRESSURE[] PROGMEM = "OIL_PRESSURE";
static const char PSTR_OIL_PRESSURE_ABBR[] PROGMEM = " OP";
static const char PSTR_OIL_PRESSURE_LABEL[] PROGMEM = "Oil Pressure";

static const char PSTR_BOOST_PRESSURE[] PROGMEM = "BOOST_PRESSURE";
static const char PSTR_BOOST_PRESSURE_ABBR[] PROGMEM = "BST";
static const char PSTR_BOOST_PRESSURE_LABEL[] PROGMEM = "Turbo Boost Pressure";

static const char PSTR_FUEL_PRESSURE[] PROGMEM = "FUEL_PRESSURE";
static const char PSTR_FUEL_PRESSURE_ABBR[] PROGMEM = " FP";
static const char PSTR_FUEL_PRESSURE_LABEL[] PROGMEM = "Fuel Pressure";

static const char PSTR_BAROMETRIC_PRESSURE[] PROGMEM = "BAROMETRIC_PRESSURE";
static const char PSTR_BAROMETRIC_PRESSURE_ABBR[] PROGMEM = "ABP";
static const char PSTR_BAROMETRIC_PRESSURE_LABEL[] PROGMEM = "Barometric Pressure";

static const char PSTR_PRIMARY_BATTERY[] PROGMEM = "PRIMARY_BATTERY";
static const char PSTR_PRIMARY_BATTERY_ABBR[] PROGMEM = "BAT";
static const char PSTR_PRIMARY_BATTERY_LABEL[] PROGMEM = "Primary Battery";

static const char PSTR_AUXILIARY_BATTERY[] PROGMEM = "AUXILIARY_BATTERY";
static const char PSTR_AUXILIARY_BATTERY_ABBR[] PROGMEM = "AUX";
static const char PSTR_AUXILIARY_BATTERY_LABEL[] PROGMEM = "Auxiliary Battery";

static const char PSTR_COOLANT_LEVEL[] PROGMEM = "COOLANT_LEVEL";
static const char PSTR_COOLANT_LEVEL_ABBR[] PROGMEM = "LVL";
static const char PSTR_COOLANT_LEVEL_LABEL[] PROGMEM = "Coolant Level";

static const char PSTR_HUMIDITY[] PROGMEM = "HUMIDITY";
static const char PSTR_HUMIDITY_ABBR[] PROGMEM = " RH";
static const char PSTR_HUMIDITY_LABEL[] PROGMEM = "Relative Humidity";

static const char PSTR_ELEVATION[] PROGMEM = "ELEVATION";
static const char PSTR_ELEVATION_ABBR[] PROGMEM = "ELEV";
static const char PSTR_ELEVATION_LABEL[] PROGMEM = "Elevation";

static const char PSTR_ENGINE_RPM[] PROGMEM = "ENGINE_RPM";
static const char PSTR_ENGINE_RPM_ABBR[] PROGMEM = "RPM";
static const char PSTR_ENGINE_RPM_LABEL[] PROGMEM = "Engine RPM";

static const char PSTR_VEHICLE_SPEED[] PROGMEM = "VEHICLE_SPEED";
static const char PSTR_VEHICLE_SPEED_ABBR[] PROGMEM = "SPD";
static const char PSTR_VEHICLE_SPEED_LABEL[] PROGMEM = "Vehicle Speed";

// ===== APPLICATION PRESETS (PROGMEM - Flash Memory) =====
//
// To add a new application:
// 1. Add PROGMEM strings for name and label above
// 2. Add ApplicationPreset entry at END of this array
// 3. Compute nameHash using Python one-liner in header
//
// Placeholder entries (label = nullptr) reserve slots for unimplemented applications.
//
static const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    // Index 0: APP_NONE (placeholder)
    {

        .name = PSTR_APP_NONE,
        .abbreviation = nullptr,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_NONE,
        .defaultUnits = 0,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x2F75,  // djb2_hash("NONE")
        .warmupTime_ms = 0,
        .persistTime_ms = 0
    },

    // ===== TEMPERATURE APPLICATIONS =====
    // Index 1: CHT - Cylinder Head Temperature
    {

        .name = PSTR_CHT,
        .abbreviation = PSTR_CHT_ABBR,
        .label = PSTR_CHT_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_MAX31855,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 260.0,
        .obd2pid = 0xC8,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xD984,  // djb2_hash("CHT")
        .warmupTime_ms = 30000,  // 30 seconds warmup
        .persistTime_ms = 2000  // 2 seconds persistence
    },

    // Index 2: EGT - Exhaust Gas Temperature
    {

        .name = PSTR_EGT,
        .abbreviation = PSTR_EGT_ABBR,
        .label = PSTR_EGT_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_MAX31855,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 600.0,
        .obd2pid = 0x78,
        .obd2length = 2,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xE1E5,  // djb2_hash("EGT")
        .warmupTime_ms = 20000,  // 20 seconds warmup
        .persistTime_ms = 2000  // 2 seconds persistence
    },

    // Index 3: COOLANT_TEMP - Engine Coolant Temperature
    {

        .name = PSTR_COOLANT_TEMP,
        .abbreviation = PSTR_COOLANT_TEMP_ABBR,
        .label = PSTR_COOLANT_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_120C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 100.0,
        .obd2pid = 0x05,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xB5AA,  // djb2_hash("COOLANT_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // Index 4: OIL_TEMP - Engine Oil Temperature
    {

        .name = PSTR_OIL_TEMP,
        .abbreviation = PSTR_OIL_TEMP_ABBR,
        .label = PSTR_OIL_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_150C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 150.0,
        .obd2pid = 0x5C,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xB5BE,  // djb2_hash("OIL_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // Index 5: TCASE_TEMP - Transfer Case Temperature
    {

        .name = PSTR_TCASE_TEMP,
        .abbreviation = PSTR_TCASE_TEMP_ABBR,
        .label = PSTR_TCASE_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_120C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 100.0,
        .obd2pid = 0xC9,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x1BEA,  // djb2_hash("TCASE_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // Index 6: AMBIENT_TEMP - Ambient Air Temperature (BME280)
    {

        .name = PSTR_AMBIENT_TEMP,
        .abbreviation = PSTR_AMBIENT_TEMP_ABBR,
        .label = PSTR_AMBIENT_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_TEMP,
        .defaultUnits = 0,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x46,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x323A,  // djb2_hash("AMBIENT_TEMP")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // ===== PRESSURE APPLICATIONS =====
    // Index 7: OIL_PRESSURE - Engine Oil Pressure
    {

        .name = PSTR_OIL_PRESSURE,
        .abbreviation = PSTR_OIL_PRESSURE_ABBR,
        .label = PSTR_OIL_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_5BAR_CURVE,
        .defaultUnits = 2,
        .defaultMinValue = 1.0,
        .defaultMaxValue = 5.0,
        .obd2pid = 0xCA,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0x2361,  // djb2_hash("OIL_PRESSURE")
        .warmupTime_ms = 5000,  // 5 seconds warmup
        .persistTime_ms = 1000  // 1 second persistence
    },

    // Index 8: BOOST_PRESSURE - Boost/Intake Pressure
    {

        .name = PSTR_BOOST_PRESSURE,
        .abbreviation = PSTR_BOOST_PRESSURE_ABBR,
        .label = PSTR_BOOST_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_2BAR_CURVE,
        .defaultUnits = 2,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 2.0,
        .obd2pid = 0x6F,
        .obd2length = 2,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xC084,  // djb2_hash("BOOST_PRESSURE")
        .warmupTime_ms = 1000,  // 1 second warmup
        .persistTime_ms = 500  // 0.5 seconds persistence
    },

    // Index 9: FUEL_PRESSURE (placeholder - not yet implemented)
    {

        .name = PSTR_FUEL_PRESSURE,
        .abbreviation = PSTR_FUEL_PRESSURE_ABBR,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_NONE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xA889,  // djb2_hash("FUEL_PRESSURE")
        .warmupTime_ms = 2000,  // 2 seconds warmup
        .persistTime_ms = 1000  // 1 second persistence
    },

    // Index 10: BAROMETRIC_PRESSURE - Barometric Pressure (BME280)
    {

        .name = PSTR_BAROMETRIC_PRESSURE,
        .abbreviation = PSTR_BAROMETRIC_PRESSURE_ABBR,
        .label = PSTR_BAROMETRIC_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_PRESSURE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x33,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xFF65,  // djb2_hash("BAROMETRIC_PRESSURE")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // ===== VOLTAGE APPLICATIONS =====
    // Index 11: PRIMARY_BATTERY - Primary Battery Voltage
    {

        .name = PSTR_PRIMARY_BATTERY,
        .abbreviation = PSTR_PRIMARY_BATTERY_ABBR,
        .label = PSTR_PRIMARY_BATTERY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VOLTAGE_DIVIDER,
        .defaultUnits = 6,
        .defaultMinValue = 10.0,
        .defaultMaxValue = 15.0,
        .obd2pid = 0xCB,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_VOLTAGE,
        .nameHash = 0xD063,  // djb2_hash("PRIMARY_BATTERY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 1000  // 1 second persistence
    },

    // Index 12: AUXILIARY_BATTERY - Auxiliary Battery Voltage
    {

        .name = PSTR_AUXILIARY_BATTERY,
        .abbreviation = PSTR_AUXILIARY_BATTERY_ABBR,
        .label = PSTR_AUXILIARY_BATTERY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VOLTAGE_DIVIDER,
        .defaultUnits = 6,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0xCC,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_VOLTAGE,
        .nameHash = 0x01F7,  // djb2_hash("AUXILIARY_BATTERY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 1000  // 1 second persistence
    },

    // ===== DIGITAL APPLICATIONS =====
    // Index 13: COOLANT_LEVEL - Coolant Level (Float Switch)
    {

        .name = PSTR_COOLANT_LEVEL,
        .abbreviation = PSTR_COOLANT_LEVEL_ABBR,
        .label = PSTR_COOLANT_LEVEL_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_FLOAT_SWITCH,
        .defaultUnits = 8,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 1.0,
        .obd2pid = 0xA2,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_DIGITAL,
        .nameHash = 0xCC0C,  // djb2_hash("COOLANT_LEVEL")
        .warmupTime_ms = 5000,  // 5 seconds warmup
        .persistTime_ms = 2000  // 2 seconds persistence
    },

    // ===== ENVIRONMENTAL APPLICATIONS =====
    // Index 14: HUMIDITY - Relative Humidity (BME280)
    {

        .name = PSTR_HUMIDITY,
        .abbreviation = PSTR_HUMIDITY_ABBR,
        .label = PSTR_HUMIDITY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_HUMIDITY,
        .defaultUnits = 8,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_HUMIDITY,
        .nameHash = 0x1612,  // djb2_hash("HUMIDITY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // Index 15: ELEVATION - Elevation (BME280)
    {

        .name = PSTR_ELEVATION,
        .abbreviation = PSTR_ELEVATION_ABBR,
        .label = PSTR_ELEVATION_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_ELEVATION,
        .defaultUnits = 9,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0xA1,
        .obd2length = 2,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_ELEVATION,
        .nameHash = 0xC26C,  // djb2_hash("ELEVATION")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000  // 5 seconds persistence
    },

    // ===== RPM APPLICATIONS =====
    // Index 16: ENGINE_RPM (placeholder - not yet implemented)
    {

        .name = PSTR_ENGINE_RPM,
        .abbreviation = PSTR_ENGINE_RPM_ABBR,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_W_PHASE_RPM,
        .defaultUnits = 7,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_RPM,
        .nameHash = 0x4429,  // djb2_hash("ENGINE_RPM")
        .warmupTime_ms = 2000,  // 2 seconds warmup
        .persistTime_ms = 0  // No persistence needed
    },

    // ===== SPEED APPLICATIONS =====
    // Index 17: VEHICLE_SPEED
    {
        .name = PSTR_VEHICLE_SPEED,
        .abbreviation = PSTR_VEHICLE_SPEED_ABBR,
        .label = PSTR_VEHICLE_SPEED_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_HALL_SPEED,
        .defaultUnits = 11,  // KPH
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,  // No alarm by default (informational only)
        .obd2pid = 0x0D,  // OBD-II PID 0x0D: Vehicle Speed
        .obd2length = 1,  // Single byte response
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_SPEED,
        .nameHash = 0x46F5,  // djb2_hash("VEHICLE_SPEED")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 0  // No persistence needed
    }
};

// Automatically calculate the number of application presets
constexpr uint8_t NUM_APPLICATION_PRESETS = sizeof(APPLICATION_PRESETS) / sizeof(APPLICATION_PRESETS[0]);

// ===== HELPER FUNCTIONS =====

/**
 * Get ApplicationPreset by array index (O(1) direct access)
 *
 * This function provides direct array access using the application index.
 * This is the fastest lookup method.
 *
 * @param index  Array index (0-16)
 * @return       Pointer to ApplicationPreset in PROGMEM, or nullptr if invalid
 */
inline const ApplicationPreset* getApplicationByIndex(uint8_t index) {
    if (index >= NUM_APPLICATION_PRESETS) return nullptr;
    return &APPLICATION_PRESETS[index];
}

/**
 * Get ApplicationPreset index by hash value (O(n) linear search)
 *
 * Searches the registry for an application with matching name hash.
 * Used for parsing user input strings.
 *
 * @param hash  16-bit hash value to search for
 * @return      Array index (0-16), or 0 (APP_NONE) if not found
 */
inline uint8_t getApplicationIndexByHash(uint16_t hash) {
    for (uint8_t i = 0; i < NUM_APPLICATION_PRESETS; i++) {
        uint16_t appHash = pgm_read_word(&APPLICATION_PRESETS[i].nameHash);
        if (appHash == hash) {
            return i;
        }
    }
    return 0;  // APP_NONE
}

/**
 * Get ApplicationPreset index by name string (O(n) hash-based search)
 *
 * Hashes the input string and searches for matching application.
 * Case-insensitive.
 *
 * @param name  Application name ("CHT", "OIL_TEMP", etc)
 * @return      Array index (0-16), or 0 (APP_NONE) if not found
 */
inline uint8_t getApplicationIndexByName(const char* name) {
    if (!name) return 0;
    uint16_t hash = djb2_hash(name);
    return getApplicationIndexByHash(hash);
}

/**
 * Get ApplicationPreset from flash memory (O(1) direct array indexing)
 *
 * Validates that the entry is implemented (non-null label).
 *
 * @param index  Application index (0-16)
 * @return     Pointer to ApplicationPreset in PROGMEM, or nullptr if invalid/unimplemented
 */
inline const ApplicationPreset* getApplicationPreset(uint8_t index) {
    if (index >= NUM_APPLICATION_PRESETS) return nullptr;
    const ApplicationPreset* preset = &APPLICATION_PRESETS[index];
    // Validate entry (check if label is non-null for implemented applications)
    if (pgm_read_ptr(&preset->label) == nullptr) return nullptr;
    return preset;
}

/**
 * Load entire ApplicationPreset from PROGMEM into RAM (helper for cleaner code)
 *
 * When you need to access multiple fields from ApplicationPreset, it's more efficient
 * to copy the entire struct to RAM once rather than reading each field from
 * PROGMEM individually.
 *
 * @param flashPreset  Pointer to ApplicationPreset in PROGMEM
 * @param ramCopy      Pointer to ApplicationPreset struct in RAM to fill
 */
inline void loadApplicationPreset(const ApplicationPreset* flashPreset, ApplicationPreset* ramCopy) {
    memcpy_P(ramCopy, flashPreset, sizeof(ApplicationPreset));
}

/**
 * Get expected measurement type for application from APPLICATION_PRESETS (O(1) direct array indexing)
 *
 * @param index  Application index (0-16)
 * @return     Expected MeasurementType for this application
 */
inline MeasurementType getApplicationExpectedMeasurementType(uint8_t index) {
    if (index >= NUM_APPLICATION_PRESETS) return MEASURE_TEMPERATURE;
    return (MeasurementType)pgm_read_byte(&APPLICATION_PRESETS[index].expectedMeasurementType);
}

/**
 * Read a single field from ApplicationPreset in PROGMEM
 *
 * Helper macros for reading individual fields without copying the entire struct.
 */
#define READ_APP_NAME(preset) ((const char*)pgm_read_ptr(&(preset)->name))
#define READ_APP_LABEL(preset) ((const char*)pgm_read_ptr(&(preset)->label))
#define READ_APP_DESCRIPTION(preset) ((const char*)pgm_read_ptr(&(preset)->description))
#define READ_APP_DEFAULT_SENSOR(preset) ((Sensor)pgm_read_byte(&(preset)->defaultSensor))
#define READ_APP_DEFAULT_UNITS(preset) ((uint8_t)pgm_read_byte(&(preset)->defaultUnits))
#define READ_APP_MIN_VALUE(preset) pgm_read_float(&(preset)->defaultMinValue)
#define READ_APP_MAX_VALUE(preset) pgm_read_float(&(preset)->defaultMaxValue)

/**
 * Get application name by index (reverse lookup for JSON export)
 *
 * @param index  Application index (0-16)
 * @return       Application name string in PROGMEM, or nullptr if invalid
 */
inline const char* getApplicationNameByIndex(uint8_t index) {
    if (index >= NUM_APPLICATION_PRESETS) return nullptr;
    return READ_APP_NAME(&APPLICATION_PRESETS[index]);
}

// ===== END HELPER FUNCTIONS =====

#endif // APPLICATION_PRESETS_H
