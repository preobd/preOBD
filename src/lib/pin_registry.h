/*
 * pin_registry.h - Pin Conflict Detection System
 *
 * Maintains a global registry of pin assignments to prevent conflicts
 * when configuring buses, inputs, and outputs. Provides validation
 * before accepting new pin configurations.
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
 * Usage:
 * 1. Register bus pins as PIN_RESERVED during initialization
 * 2. Register system pins (button, buzzer) with their specific types
 * 3. Call validateNoPinConflict() before accepting new pin assignments
 *
 * Example:
 *   registerPin(18, PIN_RESERVED, "Wire SDA");
 *   registerPin(5, PIN_BUTTON, "Mode Button");
 *   if (validateNoPinConflict(A0, PIN_INPUT, "Oil Pressure")) {
 *       registerPin(A0, PIN_INPUT, "Oil Pressure");
 *   }
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
};

// ============================================================================
// CONFIGURATION
// ============================================================================

// Maximum number of pins that can be tracked
// Set to 64 to accommodate Teensy 4.1 (54 digital + 14 analog = 58 total)
#define MAX_PIN_REGISTRY 64

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Initialize the pin registry
 * Clears all existing pin assignments
 */
void initPinRegistry();

/**
 * Register a pin with the registry
 *
 * @param pin Pin number to register
 * @param type Usage type (from PinUsageType enum)
 * @param description Human-readable description (stored as pointer, not copied)
 * @return true if registered successfully, false if pin already in use or registry full
 *
 * Note: description string must remain valid for the lifetime of the registration
 */
bool registerPin(uint8_t pin, PinUsageType type, const char* description);

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
 * Validate that a pin has no conflicts before assignment
 * If conflict exists, prints error message to Serial
 *
 * @param pin Pin number to validate
 * @param newType Proposed usage type
 * @param newDesc Proposed description
 * @return true if no conflict (pin available), false if conflict exists
 *
 * This function is the main entry point for pin conflict detection.
 * It checks if the pin is available and prints descriptive error if not.
 */
bool validateNoPinConflict(uint8_t pin, PinUsageType newType, const char* newDesc);

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

#endif // PIN_REGISTRY_H
