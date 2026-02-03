/*
 * pin_registry.cpp - Pin Conflict Detection System Implementation
 *
 * Maintains a global registry of pin assignments to prevent conflicts.
 * See pin_registry.h for detailed API documentation.
 */

#include "pin_registry.h"
#include "message_api.h"
#include "log_tags.h"
#include "system_config.h"
#include "../inputs/input_manager.h"
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
    msg.debug.error(TAG_SYSTEM, "  Current: %s%s%s%s",
                   getPinUsageTypeName(existingType),
                   existingDesc ? " (" : "",
                   existingDesc ? existingDesc : "",
                   existingDesc ? ")" : "");
    msg.debug.error(TAG_SYSTEM, "  Attempted: %s%s%s%s",
                   getPinUsageTypeName(newType),
                   newDesc ? " (" : "",
                   newDesc ? newDesc : "",
                   newDesc ? ")" : "");

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

// ============================================================================
// PIN STATUS DISPLAY
// ============================================================================

// Helper to print pin name (A0, CAN:0, I2C:0, or numeric)
static void printPinName(uint8_t pin) {
    // Check for special Teensy pins first before virtual pin ranges
    if (pin == 254) {
        // Teensy built-in SDIO pin
        msg.control.print(F("SDIO"));
    } else if (pin >= 0xF0) {
        msg.control.print(F("I2C:"));
        msg.control.print(pin - 0xF0);
    } else if (pin >= 0xC0 && pin < 0xE0) {
        msg.control.print(F("CAN:"));
        msg.control.print(pin - 0xC0);
    } else if (pin >= A0) {
        msg.control.print(F("A"));
        msg.control.print(pin - A0);
    } else {
        msg.control.print(pin);
    }
}

// Helper to print pin with padding for alignment
static void printPinPadded(uint8_t pin) {
    msg.control.print(F("  Pin "));

    // Determine width needed for pin number
    if (pin == 254) {
        // Teensy built-in SDIO pin - special case
        printPinName(pin);
    } else if (pin >= 0xF0 || (pin >= 0xC0 && pin < 0xE0)) {
        // Virtual pins like "CAN:0" or "I2C:15" - already at least 5 chars
        printPinName(pin);
    } else if (pin >= A0) {
        // Analog pins like "A0" - pad to 2 chars
        msg.control.print(F("A"));
        uint8_t analogNum = pin - A0;
        msg.control.print(analogNum);
    } else {
        // Digital pins - pad to 2 chars
        if (pin < 10) msg.control.print(' ');
        msg.control.print(pin);
    }

    msg.control.print(F(": "));
}

void printPinStatus(uint8_t specificPin) {
    // Query specific pin mode
    if (specificPin != 0xFF) {
        // Check if pin is in registry
        PinUsageType usage = getPinUsage(specificPin);
        const char* desc = getPinDescription(specificPin);

        if (usage != PIN_UNUSED) {
            printPinPadded(specificPin);
            msg.control.print(getPinUsageTypeName(usage));
            if (desc) {
                msg.control.print(F("  - "));
                msg.control.print(desc);
            }
            msg.control.println();
            return;
        }

        // Check inputs array
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].pin == specificPin && inputs[i].applicationIndex != 0xFF) {
                printPinPadded(specificPin);
                msg.control.print(F("Input     - "));
                msg.control.print(inputs[i].displayName);
                if (inputs[i].abbrName[0] != '\0') {
                    msg.control.print(F(" ("));
                    msg.control.print(inputs[i].abbrName);
                    msg.control.print(F(")"));
                }
                msg.control.println();
                return;
            }
        }

        #ifdef ENABLE_RELAY_OUTPUT
        // Check relays
        for (uint8_t i = 0; i < MAX_RELAYS; i++) {
            if (systemConfig.relays[i].outputPin == specificPin &&
                systemConfig.relays[i].outputPin != 0xFF) {
                printPinPadded(specificPin);
                msg.control.print(F("Output    - Relay "));
                msg.control.println(i);
                return;
            }
        }
        #endif

        // Pin not found
        printPinPadded(specificPin);
        msg.control.println(F("Available"));
        return;
    }

    // Full listing mode
    msg.control.println(F("=== Pin Allocation Status ==="));
    msg.control.print(F("Registry: "));
    msg.control.print(registrySize);
    msg.control.print(F(" | Inputs: "));

    // Use global count of active inputs (pin != 0xFF && isEnabled)
    extern uint8_t numActiveInputs;
    msg.control.print(numActiveInputs);

    #ifdef ENABLE_RELAY_OUTPUT
    // Count active relays
    uint8_t relayCount = 0;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (systemConfig.relays[i].outputPin != 0xFF) {
            relayCount++;
        }
    }
    msg.control.print(F(" | Relays: "));
    msg.control.print(relayCount);
    #endif

    msg.control.println();
    msg.control.println();

    // System Pins (buttons, buzzers, chip selects)
    bool hasSystemPins = false;
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].type == PIN_BUTTON ||
            pinRegistry[i].type == PIN_BUZZER ||
            pinRegistry[i].type == PIN_CS) {
            if (!hasSystemPins) {
                msg.control.println(F("System Pins:"));
                hasSystemPins = true;
            }
            printPinPadded(pinRegistry[i].pin);
            msg.control.print(getPinUsageTypeName(pinRegistry[i].type));
            if (pinRegistry[i].description) {
                msg.control.print(F("  - "));
                msg.control.print(pinRegistry[i].description);
            }
            msg.control.println();
        }
    }
    if (hasSystemPins) msg.control.println();

    // Bus Pins (reserved for I2C, SPI, CAN hardware)
    bool hasBusPins = false;
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].type == PIN_RESERVED) {
            if (!hasBusPins) {
                msg.control.println(F("Bus Pins:"));
                hasBusPins = true;
            }
            printPinPadded(pinRegistry[i].pin);
            msg.control.print(F("Reserved  - "));
            if (pinRegistry[i].description) {
                msg.control.print(pinRegistry[i].description);
            }
            msg.control.println();
        }
    }
    if (hasBusPins) msg.control.println();

    // Input Pins (sensor inputs)
    bool hasInputs = false;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        // Only show inputs that have both an application AND a valid pin assigned
        // Exclude 0xFF (invalid/unassigned pin marker)
        if (inputs[i].applicationIndex != 0xFF && inputs[i].pin != 0xFF) {
            if (!hasInputs) {
                msg.control.println(F("Input Pins:"));
                hasInputs = true;
            }
            printPinPadded(inputs[i].pin);
            msg.control.print(F("Input     - "));
            msg.control.print(inputs[i].displayName);
            if (inputs[i].abbrName[0] != '\0') {
                msg.control.print(F(" ("));
                msg.control.print(inputs[i].abbrName);
                msg.control.print(F(")"));
            }
            msg.control.println();
        }
    }
    if (hasInputs) msg.control.println();

    #ifdef ENABLE_RELAY_OUTPUT
    // Relay Pins (output relays)
    bool hasRelays = false;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (systemConfig.relays[i].outputPin != 0xFF) {
            if (!hasRelays) {
                msg.control.println(F("Relay Pins:"));
                hasRelays = true;
            }
            printPinPadded(systemConfig.relays[i].outputPin);
            msg.control.print(F("Output    - Relay "));
            msg.control.println(i);
        }
    }
    if (hasRelays) msg.control.println();
    #endif
}
