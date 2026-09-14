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
 *
 * The UNITS_REGISTRY[] data (and its PROGMEM string literals) live in
 * units_registry.cpp, not here. This header used to define the array
 * directly, which meant every one of the ~14 .cpp files that include it
 * got its own PROGMEM copy — the PROGMEM section attribute stops the
 * linker folding them back together (#400). One definition, extern here,
 * is the same shape as bus_pin_roles.{h,cpp} (#304).
 */

#ifndef UNITS_REGISTRY_H
#define UNITS_REGISTRY_H

#include <Arduino.h>
#include "calibration_types.h"
#include "../core/hash.h"

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

// ===== UNITS REGISTRY (PROGMEM - Flash Memory, defined in units_registry.cpp) =====
//
// NUM_UNITS is a manifest constant, not sizeof(UNITS_REGISTRY)/sizeof(...) —
// an extern array of unknown bound can't be sized from here. units_registry.cpp
// carries a static_assert tying it to the array's real extent so the two
// cannot silently drift.
#define NUM_UNITS 13

extern const PROGMEM UnitsInfo UNITS_REGISTRY[];

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
 * Defined in units_registry.cpp (needs the PROGMEM empty-string fallback,
 * which lives alongside the rest of the registry data).
 *
 * @param unitsIndex  Array index (0-12)
 * @return            Pointer to symbol string in PROGMEM, or empty string if invalid
 *
 * Example:
 *   const char* symbol = getUnitStringByIndex(0);  // Returns "C" (PROGMEM)
 *   msg.control.print((__FlashStringHelper*)symbol);
 */
const char* getUnitStringByIndex(uint8_t unitsIndex);

#endif // UNITS_REGISTRY_H
