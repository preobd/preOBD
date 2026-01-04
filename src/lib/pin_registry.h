/*
 * pin_registry.h - Pin Conflict Detection System
 *
 * Maintains a global registry of pin assignments to prevent conflicts
 * when configuring buses, inputs, and outputs. Provides validation
 * before accepting new pin configurations.
 *
 * Usage:
 * 1. Register all system pins during initialization
 * 2. Call validateNoPinConflict() before accepting new pin assignments
 * 3. Register pins after successful validation
 *
 * Example:
 *   registerPin(5, PIN_MODE_BUTTON, "Mode Button");
 *   if (validateNoPinConflict(18, PIN_I2C_SDA, "I2C0 SDA")) {
 *       registerPin(18, PIN_I2C_SDA, "I2C0 SDA");
 *   }
 */

#ifndef PIN_REGISTRY_H
#define PIN_REGISTRY_H

#include <Arduino.h>

// ============================================================================
// PIN USAGE TYPES
// ============================================================================

/**
 * Enumeration of pin usage types
 * Used to categorize pins and provide meaningful error messages
 */
enum PinUsageType {
    PIN_UNUSED = 0,          // Pin not in use
    PIN_MODE_BUTTON,         // Mode/config button
    PIN_BUZZER,              // Alarm buzzer
    PIN_LED,                 // Status LED
    PIN_CAN_CS,              // CAN controller chip select (MCP2515)
    PIN_CAN_INT,             // CAN controller interrupt (MCP2515)
    PIN_SD_CS,               // SD card chip select
    PIN_TEST_MODE,           // Test mode trigger pin
    PIN_ANALOG_INPUT,        // Analog sensor input
    PIN_I2C_SDA,             // I2C data line
    PIN_I2C_SCL,             // I2C clock line
    PIN_SPI_MOSI,            // SPI master out, slave in
    PIN_SPI_MISO,            // SPI master in, slave out
    PIN_SPI_SCK,             // SPI clock
    PIN_SPI_CS,              // SPI chip select (sensor-specific)
    PIN_CAN_TX,              // CAN transmit
    PIN_CAN_RX,              // CAN receive
    PIN_SERIAL_TX,           // UART transmit
    PIN_SERIAL_RX,           // UART receive
    PIN_RELAY_OUTPUT         // Relay control output
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

#endif // PIN_REGISTRY_H
