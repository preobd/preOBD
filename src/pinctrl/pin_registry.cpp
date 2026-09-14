/*
 * pin_registry.cpp - Pin Conflict Detection System Implementation
 *
 * Maintains a global registry of pin assignments to prevent conflicts.
 * See pin_registry.h for detailed API documentation.
 */

#include "pin_registry.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../storage/system_config.h"
#include "analog_pins.h"
#include "../inputs/input_manager.h"
#include <Arduino.h>
#include <string.h>

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
        pinRegistry[i].rank = 0;
    }
}

// ============================================================================
// PIN REGISTRATION
// ============================================================================

bool registerPin_unchecked(uint8_t pin, PinUsageType type, const char* description, uint8_t rank) {
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
    pinRegistry[registrySize].rank = rank;
    registrySize++;

    return true;
}

// Claim a whole group or nothing. See pin_registry.h for the contract.
//
// The rollback only ever touches pins THIS call registered (tracked in
// `claimed`), never a pin that was already reserved to the same owner and
// never one held by somebody else — freeing either would turn a refused
// reservation into a second bug.
bool reservePins_unchecked(const uint8_t* pins, uint8_t count, PinUsageType type,
                           const char* description, uint8_t rank) {
    if (!pins || count > 8) return false;

    uint8_t claimed = 0;   // bit i set => pins[i] was registered by this call

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t pin = pins[i];
        if (pin == 0xFF) continue;   // pin not present on this platform

        // Already ours: re-reserving something that is already up is not a conflict.
        // Same weakness as pin_owner.cpp's pinsConflict() self-skip: two callers
        // sharing both `type` and `description` (e.g. two PIN_OUTPUT Actions,
        // which share ACTION_PIN_DESC) would wrongly match here too. This is
        // safe ONLY because claimPins() always runs pinsConflict() first and
        // that check already refused the case with the type-gated PIN_RESERVED
        // rule — reservePins_unchecked() never sees it. Don't call this
        // directly from anywhere but pin_owner.cpp (see pin_registry.h).
        const char* held = getPinDescription(pin);
        if (held && description && getPinUsage(pin) == type &&
            strcmp(held, description) == 0) {
            continue;
        }

        if (registerPin_unchecked(pin, type, description, rank)) {
            claimed |= (uint8_t)(1 << i);
            continue;
        }

        // Conflict (or registry full): undo this call's own claims and refuse.
        for (uint8_t j = 0; j < i; j++) {
            if (claimed & (uint8_t)(1 << j)) unregisterPin(pins[j]);
        }
        return false;
    }

    return true;
}

void releasePins(const uint8_t* pins, uint8_t count, const char* description) {
    if (!pins || !description) return;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t pin = pins[i];
        if (pin == 0xFF) continue;

        const char* held = getPinDescription(pin);
        if (held && getPinUsage(pin) == PIN_RESERVED &&
            strcmp(held, description) == 0) {
            unregisterPin(pin);
        }
    }
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
            pinRegistry[registrySize].rank = 0;

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

uint8_t getPinRank(uint8_t pin) {
    for (uint8_t i = 0; i < registrySize; i++) {
        if (pinRegistry[i].pin == pin) {
            return pinRegistry[i].rank;
        }
    }
    return 0;
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
    } else {
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            msg.control.print(F("A"));
            msg.control.print(analogNum);
        } else {
            msg.control.print(pin);
        }
    }
}

// Helper to print pin with padding for alignment.
//
// analogLabel: show an analog-capable pin by its A-name (e.g. "A8"). Only true
// where the pin is meaningful as an analog *input* — bus/system/output sections
// pass false so a dual-purpose pin (e.g. Teensy CAN1 on pins 22/23, which are
// also A8/A9) prints by its physical number in that role, not "A8/A9".
static void printPinPadded(uint8_t pin, bool analogLabel = false) {
    msg.control.print(F("  Pin "));

    int8_t analogNum = analogLabel ? analogIndexForPin(pin) : -1;

    // Determine width needed for pin number
    if (pin == 254) {
        // Teensy built-in SDIO pin - special case
        printPinName(pin);
    } else if (pin >= 0xF0 || (pin >= 0xC0 && pin < 0xE0)) {
        // Virtual pins like "CAN:0" or "I2C:15" - already at least 5 chars
        printPinName(pin);
    } else if (analogNum >= 0) {
        // Analog pins like "A0"
        msg.control.print(F("A"));
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
        #if INCLUDE_CONTROL_ENGINE
        // Check control-engine RELAY Action targets FIRST: action pins are also
        // registered (as PIN_OUTPUT, for conflict detection), but this dedicated
        // view carries the action id, which the generic registry entry doesn't.
        for (int i = 0; i < MAX_ACTIONS; i++) {
            const Action& a = actions[i];
            if (a.id != CONTROL_ID_NONE && a.type == ACT_RELAY && a.target == specificPin) {
                printPinPadded(specificPin);
                msg.control.print(F("Output    - Action #"));
                msg.control.println(a.id);
                return;
            }
        }
        #endif

        // Check inputs array (inputs aren't in the registry; they self-track).
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].pin == specificPin && inputs[i].applicationIndex != 0xFF) {
                printPinPadded(specificPin, /*analogLabel=*/true);
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

        // Generic registry entry (buses, system pins, RGB output).
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

        // Pin not found — an unclaimed analog-capable pin shows its A-name so
        // an `INFO A5`-style query echoes the analog identity the user asked for.
        printPinPadded(specificPin, /*analogLabel=*/true);
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

    #if INCLUDE_CONTROL_ENGINE
    // Count control-engine RELAY Actions
    uint8_t actionCount = 0;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (actions[i].id != CONTROL_ID_NONE &&
            actions[i].type == ACT_RELAY) {
            actionCount++;
        }
    }
    msg.control.print(F(" | Actions: "));
    msg.control.print(actionCount);
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
            printPinPadded(inputs[i].pin, /*analogLabel=*/true);
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

    #if INCLUDE_CONTROL_ENGINE
    // Action Pins (control-engine RELAY actuators)
    bool hasActions = false;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        const Action& a = actions[i];
        if (a.id != CONTROL_ID_NONE && a.type == ACT_RELAY) {
            if (!hasActions) {
                msg.control.println(F("Action Pins:"));
                hasActions = true;
            }
            printPinPadded(a.target);
            msg.control.print(F("Output    - Action #"));
            msg.control.println(a.id);
        }
    }
    if (hasActions) msg.control.println();
    #endif
}
