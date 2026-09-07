/*
 * pin_registry.h - Pin Conflict Detection System
 *
 * Maintains a global registry of pin assignments to prevent conflicts
 * when configuring buses, inputs, and outputs.
 *
 * Design Philosophy:
 * Uses a two-tier approach:
 * 1. Reserved pins: Bus pins and system pins that are fixed/non-configurable
 * 2. User-configurable pins: Inputs, outputs, buttons, buzzers, chip selects
 *
 * We intentionally do NOT track the specific function of bus pins (e.g.,
 * "I2C_SDA" vs "I2C_SCL"). On Teensy 4.x, these are hardware-fixed anyway.
 * On ESP32, we just need to know a pin is "reserved by Wire1" - not which
 * specific line it is.
 *
 * registerPin_unchecked()/reservePins_unchecked() are named "_unchecked" on
 * purpose: they mutate the registry with NO awareness of configured inputs
 * (source 2 of pin ownership truth — see pin_owner.h) and no awareness of the
 * precedence ladder (PinRank). Calling them directly from anywhere but
 * pin_owner.cpp reopens exactly the class of bug (#351, #354, #358, #359,
 * #379, #381, #384) this split exists to close. Every other module claims
 * pins through pin_owner.h's pinsConflict()/claimPins() instead; this header
 * is otherwise a normal read/release API (isPinAvailable, getPinDescription,
 * releasePins, unregisterPin, ...).
 */

#ifndef PIN_REGISTRY_H
#define PIN_REGISTRY_H

#include <Arduino.h>

// ============================================================================
// PIN USAGE TYPES
// ============================================================================

/**
 * Simplified enumeration of pin usage types
 *
 * Two-tier model:
 * - PIN_RESERVED: Bus pins, boot pins, system pins (non-configurable)
 * - Others: User-configurable pins
 *
 * Note: We do NOT track specific bus functions (I2C_SDA, SPI_MOSI, etc.)
 * The description field provides details when needed (e.g., "Wire1 SDA")
 */
enum PinUsageType {
    PIN_UNUSED = 0,     // Pin not in use
    PIN_RESERVED,       // Bus pins, boot pins, system pins (non-configurable)
    PIN_INPUT,          // Analog/digital sensor input
    PIN_OUTPUT,         // Relay, LED, digital output
    PIN_BUTTON,         // User button (mode switch, etc.)
    PIN_BUZZER,         // Buzzer/speaker
    PIN_CS              // Chip select (SPI devices, SD card, MCP2515)
};

// ============================================================================
// PIN USAGE STRUCTURE
// ============================================================================

/**
 * Pin Usage Entry
 * Stores information about a single pin assignment
 */
struct PinUsage {
    uint8_t pin;                // Pin number
    PinUsageType type;          // Usage type
    const char* description;    // Human-readable description (e.g., "I2C0 SDA", "Oil Pressure Sensor")
    uint8_t rank;                // Precedence (PinRank in pin_owner.h); lower wins. 0 = not set.
};

// ============================================================================
// CONFIGURATION
// ============================================================================

// Maximum number of pins that can be tracked; set per board in boards/<board>/board.h.

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Initialize the pin registry
 * Clears all existing pin assignments
 */
void initPinRegistry();

/**
 * Register a pin with the registry — RAW primitive, no cross-checking.
 *
 * Only pin_owner.cpp may call this directly (see the header comment above).
 * Every other module goes through pin_owner.h's claimPins().
 *
 * @param pin Pin number to register
 * @param type Usage type (from PinUsageType enum)
 * @param description Human-readable description (stored as pointer, not copied)
 * @param rank Precedence (PinRank in pin_owner.h) this claim is made at
 * @return true if registered successfully, false if pin already in use or registry full
 *
 * Note: description string must remain valid for the lifetime of the registration
 */
bool registerPin_unchecked(uint8_t pin, PinUsageType type, const char* description, uint8_t rank);

/**
 * Reserve a group of pins for one owner, all or nothing — RAW primitive.
 *
 * Only pin_owner.cpp may call this directly (see the header comment above).
 * Every other module goes through pin_owner.h's claimPins().
 *
 * A bus rests on the invariant "bus up implies its pins are reserved" (#359),
 * so a partial claim is worse than no claim: it leaves the caller believing it
 * owns copper that another function is already driving. This claims every pin
 * in the group or none of them — a failure rolls back the pins THIS call
 * registered, and leaves pins that were already someone else's untouched.
 *
 * Pins equal to 0xFF are skipped ("this bus has no such pin on this platform").
 * A pin already held with this same type and description is treated as
 * already ours, so re-reserving something that is already up succeeds.
 *
 * @param pins        Array of pin numbers to claim
 * @param count       Number of entries in pins[] (max 8)
 * @param type        Usage type to register the pins as
 * @param description Owner name; stored by pointer, must outlive the reservation
 * @param rank        Precedence (PinRank in pin_owner.h) this claim is made at
 * @return true if the whole group is now reserved to `description`
 */
bool reservePins_unchecked(const uint8_t* pins, uint8_t count, PinUsageType type,
                           const char* description, uint8_t rank);

/**
 * Release pins previously reserved by claimPins() (pin_owner.h) for the same owner
 *
 * Only entries whose registered description matches `description` are released,
 * so this can never free another function's pin.
 */
void releasePins(const uint8_t* pins, uint8_t count, const char* description);

/**
 * Check if a pin is available (not registered)
 *
 * @param pin Pin number to check
 * @return true if pin is available, false if already in use
 */
bool isPinAvailable(uint8_t pin);

/**
 * Get the usage type of a registered pin
 *
 * @param pin Pin number to query
 * @return PinUsageType of the pin, or PIN_UNUSED if not registered
 */
PinUsageType getPinUsage(uint8_t pin);

/**
 * Get the description of a registered pin
 *
 * @param pin Pin number to query
 * @return Description string, or NULL if pin not registered
 */
const char* getPinDescription(uint8_t pin);

/**
 * Get the precedence rank a registered pin was claimed at
 *
 * @param pin Pin number to query
 * @return PinRank value (see pin_owner.h), or 0 if pin not registered
 */
uint8_t getPinRank(uint8_t pin);

/**
 * Unregister a pin (mark as available)
 *
 * @param pin Pin number to unregister
 */
void unregisterPin(uint8_t pin);

/**
 * Clear the entire pin registry
 * Marks all pins as unused
 */
void clearPinRegistry();

/**
 * Dump the entire pin registry to Serial
 * Useful for debugging pin assignments
 */
void dumpPinRegistry();

/**
 * Get human-readable name for a PinUsageType
 *
 * @param type Usage type enum value
 * @return String representation (e.g., "I2C SDA", "Mode Button")
 */
const char* getPinUsageTypeName(PinUsageType type);

/**
 * Get the number of registered pins
 *
 * @return Number of pins currently registered
 */
uint8_t getPinRegistrySize();

/**
 * Get pin usage entry by index
 *
 * @param index Index in the registry (0 to getPinRegistrySize()-1)
 * @return Pointer to PinUsage entry, or nullptr if index out of bounds
 */
const PinUsage* getPinUsageByIndex(uint8_t index);

/**
 * Print comprehensive pin allocation status
 *
 * Shows all pin allocations organized by category: system pins, bus pins,
 * input pins, and relay output pins. Handles virtual pins (CAN:0-31, I2C:0-15)
 * and analog pins (A0-A15) with proper formatting.
 *
 * @param specificPin Pin number to query (0xFF = show all pins)
 *
 * Usage:
 *   printPinStatus();           // Show all pin allocations
 *   printPinStatus(false, A0);  // Query specific pin
 */
void printPinStatus(uint8_t specificPin = 0xFF);

#endif // PIN_REGISTRY_H
