/*
 * pin_registry.cpp - Pin Conflict Detection System Implementation
 *
 * Maintains a global registry of pin assignments to prevent conflicts.
 * See pin_registry.h for detailed API documentation.
 */

#include "pin_registry.h"
#include "message_api.h"
#include "log_tags.h"
#include <Arduino.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Global pin registry array
static PinUsage pinRegistry[MAX_PIN_REGISTRY];
static uint8_t registrySize = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void initPinRegistry() {
    clearPinRegistry();
}

void clearPinRegistry() {
    registrySize = 0;
    for (uint8_t i = 0; i < MAX_PIN_REGISTRY; i++) {
        pinRegistry[i].pin = 0xFF;
        pinRegistry[i].type = PIN_UNUSED;
        pinRegistry[i].description = nullptr;
    }
}

// ============================================================================
// PIN REGISTRATION
// ============================================================================

bool registerPin(uint8_t pin, PinUsageType type, const char* description) {
    // Check if pin already registered
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            // Pin already in use
            return false;
        }
    }

    // Check if registry is full
    if (registrySize >= MAX_PIN_REGISTRY) {
        msg.debug.error(TAG_SYSTEM, "Pin registry full");
        return false;
    }

    // Register the pin
    pinRegistry[registrySize].pin = pin;
    pinRegistry[registrySize].type = type;
    pinRegistry[registrySize].description = description;
    registrySize++;

    return true;
}

void unregisterPin(uint8_t pin) {
    // Find the pin in the registry
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            // Remove by shifting remaining entries down
            for (uint8_t j = i; j < registrySize - 1; j++) {
                pinRegistry[j] = pinRegistry[j + 1];
            }
            registrySize--;

            // Clear the last entry
            pinRegistry[registrySize].pin = 0xFF;
            pinRegistry[registrySize].type = PIN_UNUSED;
            pinRegistry[registrySize].description = nullptr;

            return;
        }
    }
}

// ============================================================================
// PIN QUERIES
// ============================================================================

bool isPinAvailable(uint8_t pin) {
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            return false;  // Pin is in use
        }
    }
    return true;  // Pin is available
}

PinUsageType getPinUsage(uint8_t pin) {
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            return pinRegistry[i].type;
        }
    }
    return PIN_UNUSED;
}

const char* getPinDescription(uint8_t pin) {
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            return pinRegistry[i].description;
        }
    }
    return nullptr;
}

// ============================================================================
// PIN VALIDATION
// ============================================================================

bool validateNoPinConflict(uint8_t pin, PinUsageType newType, const char* newDesc) {
    // Check if pin is available
    if (isPinAvailable(pin)) {
        return true;  // No conflict
    }

    // Pin is already in use - print detailed error message
    PinUsageType existingType = getPinUsage(pin);
    const char* existingDesc = getPinDescription(pin);

    msg.debug.error(TAG_SYSTEM, "Pin %d already in use", pin);
    msg.debug.error(TAG_SYSTEM, "  Current: %s%s%s",
                   getPinUsageTypeName(existingType),
                   existingDesc ? " (" : "",
                   existingDesc ? existingDesc : "");
    msg.debug.error(TAG_SYSTEM, "  Attempted: %s%s%s",
                   getPinUsageTypeName(newType),
                   newDesc ? " (" : "",
                   newDesc ? newDesc : "");

    return false;  // Conflict detected
}

// ============================================================================
// DEBUGGING
// ============================================================================

void dumpPinRegistry() {
    msg.control.println(F("=== Pin Registry ==="));
    msg.control.print(F("Registered pins: "));
    msg.control.print(registrySize);
    msg.control.print(F("/"));
    msg.control.println(MAX_PIN_REGISTRY);
    msg.control.println();

    if (registrySize == 0) {
        msg.control.println(F("  (no pins registered)"));
        return;
    }

    for (uint8_t i = 0; i < registrySize; i++) {
        msg.control.print(F("  Pin "));
        if (pinRegistry[i].pin < 10) msg.control.print(F(" "));
        msg.control.print(pinRegistry[i].pin);
        msg.control.print(F(": "));

        msg.control.print(getPinUsageTypeName(pinRegistry[i].type));

        if (pinRegistry[i].description) {
            msg.control.print(F(" - "));
            msg.control.print(pinRegistry[i].description);
        }

        msg.control.println();
    }

    msg.control.println();
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

const char* getPinUsageTypeName(PinUsageType type) {
    switch (type) {
        case PIN_UNUSED:    return "Unused";
        case PIN_RESERVED:  return "Reserved";
        case PIN_INPUT:     return "Input";
        case PIN_OUTPUT:    return "Output";
        case PIN_BUTTON:    return "Button";
        case PIN_BUZZER:    return "Buzzer";
        case PIN_CS:        return "Chip Select";
        default:            return "Unknown";
    }
}

uint8_t getPinRegistrySize() {
    return registrySize;
}

const PinUsage* getPinUsageByIndex(uint8_t index) {
    if (index >= registrySize) {
        return nullptr;
    }
    return &pinRegistry[index];
}
