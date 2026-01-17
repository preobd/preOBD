/*
 * units_registry.h - Display Units Registry
 *
 * Defines a registry-based architecture for display units.
 *
 * ARCHITECTURE:
 * - All units stored in PROGMEM (flash memory) to save RAM
 * - Hash-based name lookup for O(1) average case performance
 * - Index-based access for fast direct lookup
 * - Multiple aliases per unit (e.g., "C", "CELSIUS")
 */

#ifndef UNITS_REGISTRY_H
#define UNITS_REGISTRY_H

#include <Arduino.h>
#include "sensor_types.h"
#include "hash.h"

// ===== UNITS INFO STRUCTURE =====

/**
 * UnitsInfo - Complete metadata for a display unit
 *
 * Stored in PROGMEM to minimize RAM usage. Contains all information
 * needed for unit conversion, display, and parsing.
 *
 * Conversion formula: output = input * conversionFactor + conversionOffset
 *
 * Example: Celsius to Fahrenheit
 *   F = C * (9/5) + 32
 *   conversionFactor = 1.8
 *   conversionOffset = 32.0
 */
struct UnitsInfo {
    const char* name;               // Full name: "CELSIUS", "FAHRENHEIT", etc (PROGMEM)
    const char* alias;              // Short alias: "C", "F", "psi", etc (PROGMEM)
    const char* symbol;             // Display symbol: "C", "F", "psi", etc (PROGMEM)
    MeasurementType measurementType; // What physical quantity (temp, pressure, etc)
    float conversionFactor;         // Multiply by this (from base unit)
    float conversionOffset;         // Add this after multiplication
    uint16_t nameHash;              // Precomputed hash of name (uppercase)
    uint16_t aliasHash;             // Precomputed hash of alias (uppercase)
};

// ===== UNITS REGISTRY (PROGMEM - Flash Memory) =====

// String literals in PROGMEM
static const char PSTR_CELSIUS[] PROGMEM = "CELSIUS";
static const char PSTR_C[] PROGMEM = "C";

static const char PSTR_FAHRENHEIT[] PROGMEM = "FAHRENHEIT";
static const char PSTR_F[] PROGMEM = "F";

static const char PSTR_BAR[] PROGMEM = "BAR";
static const char PSTR_BAR_SYMBOL[] PROGMEM = "bar";

static const char PSTR_PSI[] PROGMEM = "PSI";
static const char PSTR_PSI_SYMBOL[] PROGMEM = "psi";

static const char PSTR_KPA[] PROGMEM = "KPA";
static const char PSTR_KPA_SYMBOL[] PROGMEM = "kPa";

static const char PSTR_INHG[] PROGMEM = "INHG";
static const char PSTR_INHG_SYMBOL[] PROGMEM = "inHg";

static const char PSTR_VOLTS[] PROGMEM = "VOLTS";
static const char PSTR_V[] PROGMEM = "V";

static const char PSTR_RPM[] PROGMEM = "RPM";
static const char PSTR_RPM_SYMBOL[] PROGMEM = "rpm";

static const char PSTR_PERCENT[] PROGMEM = "PERCENT";
static const char PSTR_PERCENT_SYMBOL[] PROGMEM = "%";

static const char PSTR_METERS[] PROGMEM = "METERS";
static const char PSTR_M[] PROGMEM = "M";
static const char PSTR_M_SYMBOL[] PROGMEM = "m";

static const char PSTR_FEET[] PROGMEM = "FEET";
static const char PSTR_FT[] PROGMEM = "FT";
static const char PSTR_FT_SYMBOL[] PROGMEM = "ft";

static const char PSTR_KPH[] PROGMEM = "KPH";
static const char PSTR_KPH_SYMBOL[] PROGMEM = "km/h";

static const char PSTR_MPH[] PROGMEM = "MPH";
static const char PSTR_MPH_SYMBOL[] PROGMEM = "mph";

/**
 * UNITS_REGISTRY - Complete unit definitions
 *
 * BASE UNITS (conversionFactor = 1.0, conversionOffset = 0.0):
 * - Temperature: Celsius
 * - Pressure: Bar
 * - Voltage: Volts
 * - Speed: RPM
 * - Percent: Percent (0.0 - 100.0)
 * - Distance: Meters
 *
 * CONVERSION FACTORS (from conversion functions in sensor_read.cpp):
 * - Fahrenheit: C * 9/5 + 32    => factor=1.8, offset=32.0
 * - PSI:        bar * 14.5038   => factor=14.5038, offset=0
 * - KPA:        bar * 100.0     => factor=100.0, offset=0
 * - INHG:       bar * 29.53     => factor=29.53, offset=0
 * - FEET:       m * 3.28084     => factor=3.28084, offset=0
 */
static const PROGMEM UnitsInfo UNITS_REGISTRY[] = {
    // Index 0: CELSIUS (base unit for temperature)
    {
        .name = PSTR_CELSIUS,
        .alias = PSTR_C,
        .symbol = PSTR_C,
        .measurementType = MEASURE_TEMPERATURE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x82DD,  // djb2_hash("CELSIUS")
        .aliasHash = 0xB5E8  // djb2_hash("C")
    },

    // Index 1: FAHRENHEIT
    {
        .name = PSTR_FAHRENHEIT,
        .alias = PSTR_F,
        .symbol = PSTR_F,
        .measurementType = MEASURE_TEMPERATURE,
        .conversionFactor = 1.8,
        .conversionOffset = 32.0,
        .nameHash = 0xA9E3,  // djb2_hash("FAHRENHEIT")
        .aliasHash = 0xB5EB  // djb2_hash("F")
    },

    // Index 2: BAR (base unit for pressure)
    {
        .name = PSTR_BAR,
        .alias = PSTR_BAR,       // Same as name
        .symbol = PSTR_BAR_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xD45A,  // djb2_hash("BAR")
        .aliasHash = 0xD45A  // same as name
    },

    // Index 3: PSI
    {
        .name = PSTR_PSI,
        .alias = PSTR_PSI,
        .symbol = PSTR_PSI_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 14.5038,
        .conversionOffset = 0.0,
        .nameHash = 0x1231,  // djb2_hash("PSI")
        .aliasHash = 0x1231  // same as name
    },

    // Index 4: KPA
    {
        .name = PSTR_KPA,
        .alias = PSTR_KPA,
        .symbol = PSTR_KPA_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 100.0,
        .conversionOffset = 0.0,
        .nameHash = 0xFC81,  // djb2_hash("KPA")
        .aliasHash = 0xFC81  // same as name
    },

    // Index 5: INHG
    {
        .name = PSTR_INHG,
        .alias = PSTR_INHG,
        .symbol = PSTR_INHG_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 29.53,
        .conversionOffset = 0.0,
        .nameHash = 0x6C8B,  // djb2_hash("INHG")
        .aliasHash = 0x6C8B  // same as name
    },

    // Index 6: VOLTS (base unit)
    {
        .name = PSTR_VOLTS,
        .alias = PSTR_V,
        .symbol = PSTR_V,
        .measurementType = MEASURE_VOLTAGE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xDBDD,  // djb2_hash("VOLTS")
        .aliasHash = 0xB5FB  // djb2_hash("V")
    },

    // Index 7: RPM (base unit)
    {
        .name = PSTR_RPM,
        .alias = PSTR_RPM,
        .symbol = PSTR_RPM_SYMBOL,
        .measurementType = MEASURE_RPM,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x1A54,  // djb2_hash("RPM")
        .aliasHash = 0x1A54  // same as name
    },

    // Index 8: PERCENT (base unit)
    {
        .name = PSTR_PERCENT,
        .alias = PSTR_PERCENT_SYMBOL,
        .symbol = PSTR_PERCENT_SYMBOL,
        .measurementType = MEASURE_HUMIDITY,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x53B6,  // djb2_hash("PERCENT")
        .aliasHash = 0xB5CA  // djb2_hash("%")
    },

    // Index 9: METERS (base unit for elevation)
    {
        .name = PSTR_METERS,
        .alias = PSTR_M,
        .symbol = PSTR_M_SYMBOL,
        .measurementType = MEASURE_ELEVATION,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x1835,  // djb2_hash("METERS")
        .aliasHash = 0xB5F2  // djb2_hash("M")
    },

    // Index 10: FEET
    {
        .name = PSTR_FEET,
        .alias = PSTR_FT,
        .symbol = PSTR_FT_SYMBOL,
        .measurementType = MEASURE_ELEVATION,
        .conversionFactor = 3.28084,
        .conversionOffset = 0.0,
        .nameHash = 0xA0C9,  // djb2_hash("FEET")
        .aliasHash = 0x739F  // djb2_hash("FT")
    },

    // Index 11: KPH (base unit for speed)
    {
        .name = PSTR_KPH,
        .alias = PSTR_KPH,
        .symbol = PSTR_KPH_SYMBOL,
        .measurementType = MEASURE_SPEED,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xFC88,  // djb2_hash("KPH")
        .aliasHash = 0xFC88  // same as name
    },

    // Index 12: MPH
    {
        .name = PSTR_MPH,
        .alias = PSTR_MPH,
        .symbol = PSTR_MPH_SYMBOL,
        .measurementType = MEASURE_SPEED,
        .conversionFactor = 0.621371,  // km/h to mph conversion
        .conversionOffset = 0.0,
        .nameHash = 0x050A,  // djb2_hash("MPH")
        .aliasHash = 0x050A  // same as name
    }
};

// Automatically calculate the number of units
constexpr uint8_t NUM_UNITS = sizeof(UNITS_REGISTRY) / sizeof(UNITS_REGISTRY[0]);

// ===== HELPER FUNCTIONS =====

/**
 * Get UnitsInfo by array index (O(1) direct access)
 *
 * This function provides direct array access using the unit index.
 * This is the fastest lookup method.
 *
 * @param index  Array index (0-10)
 * @return       Pointer to UnitsInfo in PROGMEM, or nullptr if invalid
 */
inline const UnitsInfo* getUnitsByIndex(uint8_t index) {
    if (index >= NUM_UNITS) return nullptr;
    return &UNITS_REGISTRY[index];
}

/**
 * Get UnitsInfo by hash value (O(n) linear search)
 *
 * Searches the registry for a unit with matching name hash or alias hash.
 * Used for parsing user input strings.
 *
 * @param hash  16-bit hash value to search for
 * @return      Pointer to UnitsInfo in PROGMEM, or nullptr if not found
 *
 * Example:
 *   uint16_t hash = djb2_hash("C");
 *   const UnitsInfo* info = getUnitsByHash(hash);  // Returns CELSIUS entry
 */
inline const UnitsInfo* getUnitsByHash(uint16_t hash) {
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        const UnitsInfo* info = &UNITS_REGISTRY[i];
        uint16_t nameHash = pgm_read_word(&info->nameHash);
        uint16_t aliasHash = pgm_read_word(&info->aliasHash);

        if (hash == nameHash || hash == aliasHash) {
            return info;
        }
    }
    return nullptr;
}

/**
 * Get unit index by hash value (O(n) linear search)
 *
 * Searches the registry for a unit with matching name hash or alias hash.
 * Returns the array index (0-10).
 *
 * @param hash  16-bit hash value to search for
 * @return      Array index (0-10), or 0 (CELSIUS) if not found
 */
inline uint8_t getUnitsIndexByHash(uint16_t hash) {
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        const UnitsInfo* info = &UNITS_REGISTRY[i];
        uint16_t nameHash = pgm_read_word(&info->nameHash);
        uint16_t aliasHash = pgm_read_word(&info->aliasHash);

        if (hash == nameHash || hash == aliasHash) {
            return i;
        }
    }
    return 0;  // CELSIUS (default)
}

/**
 * Get unit index by name string (O(n) hash-based search)
 *
 * Hashes the input string and searches for matching unit.
 * Case-insensitive. Returns index instead of pointer.
 *
 * @param name  Unit name or alias ("CELSIUS", "C", "PSI", etc)
 * @return      Array index (0-10), or 0 (CELSIUS) if not found
 */
inline uint8_t getUnitsIndexByName(const char* name) {
    if (!name) return 0;
    uint16_t hash = djb2_hash(name);
    return getUnitsIndexByHash(hash);
}

/**
 * Get UnitsInfo by name string (O(n) hash-based search)
 *
 * Hashes the input string and searches for matching unit.
 * Case-insensitive.
 *
 * @param name  Unit name or alias ("CELSIUS", "C", "PSI", etc)
 * @return      Pointer to UnitsInfo in PROGMEM, or nullptr if not found
 *
 * Example:
 *   const UnitsInfo* info = getUnitsByName("celsius");  // Case-insensitive
 */
inline const UnitsInfo* getUnitsByName(const char* name) {
    if (!name) return nullptr;
    uint16_t hash = djb2_hash(name);
    return getUnitsByHash(hash);
}

/**
 * Load entire UnitsInfo from PROGMEM into RAM (helper for cleaner code)
 *
 * When you need to access multiple fields from UnitsInfo, it's more efficient
 * to copy the entire struct to RAM once rather than reading each field from
 * PROGMEM individually.
 *
 * @param flashInfo  Pointer to UnitsInfo in PROGMEM
 * @param ramCopy    Pointer to UnitsInfo struct in RAM to fill
 */
inline void loadUnitsInfo(const UnitsInfo* flashInfo, UnitsInfo* ramCopy) {
    memcpy_P(ramCopy, flashInfo, sizeof(UnitsInfo));
}

/**
 * Read a single field from UnitsInfo in PROGMEM
 *
 * Helper macros for reading individual fields without copying the entire struct.
 */
#define READ_UNITS_NAME(info) ((const char*)pgm_read_ptr(&(info)->name))
#define READ_UNITS_ALIAS(info) ((const char*)pgm_read_ptr(&(info)->alias))
#define READ_UNITS_SYMBOL(info) ((const char*)pgm_read_ptr(&(info)->symbol))
#define READ_UNITS_FACTOR(info) pgm_read_float(&(info)->conversionFactor)
#define READ_UNITS_OFFSET(info) pgm_read_float(&(info)->conversionOffset)
#define READ_UNITS_MEASUREMENT_TYPE(info) ((MeasurementType)pgm_read_byte(&(info)->measurementType))

/**
 * Get unit symbol string by index (O(1) direct access)
 *
 * Returns the display symbol string for a unit (e.g., "C", "psi", "km/h").
 * This is a convenience function that wraps getUnitsByIndex() and extracts
 * only the symbol field.
 *
 * @param unitsIndex  Array index (0-12)
 * @return            Pointer to symbol string in PROGMEM, or empty string if invalid
 *
 * Example:
 *   const char* symbol = getUnitStringByIndex(0);  // Returns "C" (PROGMEM)
 *   msg.control.print((__FlashStringHelper*)symbol);
 */
// Empty string in PROGMEM for invalid unit index
static const char PSTR_EMPTY_UNIT[] PROGMEM = "";

inline const char* getUnitStringByIndex(uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return PSTR_EMPTY_UNIT;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    return (const char*)pgm_read_ptr(&info->symbol);
}

#endif // UNITS_REGISTRY_H
