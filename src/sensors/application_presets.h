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
 * OBD-II PID RELATIONSHIP (EXPORT vs IMPORT):
 * This file defines PIDs for EXPORTING preOBD sensor data over CAN bus.
 * Related file: can_sensor_library/standard_pids.h defines PIDs for IMPORTING
 * data from external ECUs.
 *
 * - EXPORT (this file): Broadcast preOBD sensors as OBD-II PIDs for consumption
 *   by tools like Torque, RealDash, etc. Values stored in .obd2pid/.obd2length
 * - IMPORT (standard_pids.h): Decode PIDs from external ECUs to create virtual
 *   sensors in preOBD
 *
 * Where semantic overlap exists (e.g., COOLANT_TEMP, ENGINE_RPM), PID values
 * MUST match standard_pids.h to maintain consistency. Standard PIDs use SAE J1979
 * Mode 01 definitions. Custom PIDs use manufacturer-specific range (0xC0-0xFF).
 *
 * HOW TO ADD A NEW APPLICATION:
 * 1. Add PROGMEM strings for name and label (in application_presets.cpp)
 * 2. Add ApplicationPreset entry to APPLICATION_PRESETS[] with unique name
 *    (in application_presets.cpp)
 * 3. Compute hash: python3 -c "h=5381; s='YOUR_NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
 * 4. For obd2pid, use standard PID from standard_pids.h if available, or custom (0xC0+)
 * 5. Bump NUM_APPLICATION_PRESETS below to match the new entry count
 *
 * The APPLICATION_PRESETS[] data (and its PROGMEM string literals) live in
 * application_presets.cpp, not here. This header used to define the array
 * directly, which meant every one of the ~11 .cpp files that include it got
 * its own PROGMEM copy — the PROGMEM section attribute stops the linker
 * folding them back together (#400). One definition, extern here, is the
 * same shape as bus_pin_roles.{h,cpp} (#304).
 */

#ifndef APPLICATION_PRESETS_H
#define APPLICATION_PRESETS_H

#include <Arduino.h>
#include "../inputs/input.h"
#include "../core/hash.h"
#include "generated/registry_enums.h"

// ===== APPLICATION CAR-AREA GROUPS =====
// Coarse vehicle-area grouping for client UIs (mobile app add-input wizard,
// webapp). Exported by name in LIST APPLICATIONS JSON ("group"); clients must
// bucket unknown names under OTHER so this enum can grow. OTHER = 0 keeps a
// preset that omits .group safely bucketed.
enum AppGroup : uint8_t {
    APP_GROUP_OTHER = 0,
    APP_GROUP_ENGINE,
    APP_GROUP_DRIVETRAIN,
    APP_GROUP_INDUCTION,
    APP_GROUP_EXHAUST,
    APP_GROUP_ELECTRICAL,
    APP_GROUP_ENVIRONMENT,
};

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
    AppGroup group;               // Car-area group for client UIs (LAST member —
                                  // designated initializers must stay in order)
};

// ===== APPLICATION PRESETS (PROGMEM - Flash Memory, defined in application_presets.cpp) =====
//
// NUM_APPLICATION_PRESETS is a manifest constant, not sizeof(APPLICATION_PRESETS)/
// sizeof(...) — an extern array of unknown bound can't be sized from here.
// application_presets.cpp carries a static_assert tying it to the array's real
// extent so the two cannot silently drift.
#define NUM_APPLICATION_PRESETS 29

extern const PROGMEM ApplicationPreset APPLICATION_PRESETS[];

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
