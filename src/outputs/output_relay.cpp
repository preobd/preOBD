/*
 * output_relay.cpp - Relay output module implementation
 *
 * Controls 12V relays based on sensor thresholds with hysteresis.
 * Supports manual override and EEPROM-backed configuration.
 *
 * Design pattern follows output_alarm.cpp for consistency.
 */

#include "../config.h"

#ifdef ENABLE_RELAY_OUTPUT

#include "output_relay.h"
#include "../lib/system_config.h"
#include "../inputs/input_manager.h"
#include "../lib/message_api.h"
#include "../lib/units_registry.h"
#include "../lib/log_tags.h"

// ===== RUNTIME STATE =====
static RelayRuntimeState relayStates[MAX_RELAYS];

// ===== HELPER FUNCTIONS =====

/**
 * Get input index from pin number
 * @param inputPin Analog/digital pin number
 * @return Input array index, or 0xFF if not found
 */
static uint8_t getInputIndexByPin(uint8_t inputPin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled && inputs[i].pin == inputPin) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

/**
 * Evaluate relay rule and determine desired state
 * Implements hysteresis logic with separate on/off thresholds
 *
 * @param relayIndex Index of relay to evaluate
 * @return Desired relay state (true = ON, false = OFF)
 */
static bool evaluateRelayRule(uint8_t relayIndex) {
    RelayConfig* cfg = &systemConfig.relays[relayIndex];
    RelayRuntimeState* state = &relayStates[relayIndex];

    // Safety: Check if input is assigned and valid
    if (cfg->inputIndex >= MAX_INPUTS) {
        return false;  // No input assigned
    }

    Input* input = &inputs[cfg->inputIndex];

    // Safety: If sensor disabled or value is NaN, turn OFF
    if (!input->flags.isEnabled || isnan(input->value)) {
        return false;
    }

    // Safety: Don't activate during warmup or init
    if (input->alarmContext.state == ALARM_WARMUP ||
        input->alarmContext.state == ALARM_INIT) {
        return false;
    }

    float value = input->value;
    bool currentState = state->currentState;

    // Hysteresis logic based on mode
    if (cfg->mode == RELAY_AUTO_HIGH) {
        // Turn ON when value >= thresholdOn
        // Turn OFF when value <= thresholdOff
        // Maintain state in between (hysteresis band)
        if (!currentState && value >= cfg->thresholdOn) {
            return true;  // Activate
        }
        if (currentState && value <= cfg->thresholdOff) {
            return false;  // Deactivate
        }
        return currentState;  // Maintain current state
    }
    else if (cfg->mode == RELAY_AUTO_LOW) {
        // Turn ON when value <= thresholdOn
        // Turn OFF when value >= thresholdOff
        // Maintain state in between (hysteresis band)
        if (!currentState && value <= cfg->thresholdOn) {
            return true;  // Activate
        }
        if (currentState && value >= cfg->thresholdOff) {
            return false;  // Deactivate
        }
        return currentState;  // Maintain current state
    }

    return false;
}

// ===== OUTPUT MODULE INTERFACE =====

/**
 * Initialize relay output module
 * Called once during setup by output manager
 */
void initRelayOutput() {
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        RelayConfig* cfg = &systemConfig.relays[i];
        RelayRuntimeState* state = &relayStates[i];

        // Initialize runtime state
        state->currentState = false;
        state->lastStateChange = millis();
        state->stateChangeCount = 0;

        // Skip disabled or unconfigured relays
        if (cfg->mode == RELAY_DISABLED || cfg->outputPin == 0xFF) {
            continue;
        }

        // Configure pin as output
        pinMode(cfg->outputPin, OUTPUT);

        // Initialize to OFF state (safe default)
        digitalWrite(cfg->outputPin, LOW);

        msg.debug.info(TAG_RELAY, "Relay %d initialized on pin %d", i, cfg->outputPin);
    }

    msg.debug.info(TAG_RELAY, "Relay output initialized");
}

/**
 * Send output data per input
 * Required by OutputModule interface but not used for relays
 */
void sendRelayOutput(Input* input) {
    // Relay decisions are made by scanning configuration in updateRelayOutput()
    // This function is called by output manager but we don't need per-input logic
}

/**
 * Update relay outputs
 * Called every loop iteration by output manager
 * Evaluates rules and controls relay GPIO pins
 */
void updateRelayOutput() {
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        RelayConfig* cfg = &systemConfig.relays[i];
        RelayRuntimeState* state = &relayStates[i];

        // Skip disabled relays
        if (cfg->mode == RELAY_DISABLED || cfg->outputPin == 0xFF) {
            continue;
        }

        bool desiredState = false;

        // Determine desired state based on mode
        switch (cfg->mode) {
            case RELAY_MANUAL_ON:
                desiredState = true;
                break;

            case RELAY_MANUAL_OFF:
                desiredState = false;
                break;

            case RELAY_AUTO_HIGH:
            case RELAY_AUTO_LOW:
                desiredState = evaluateRelayRule(i);
                break;

            default:
                desiredState = false;
                break;
        }

        // Update output if state changed
        if (desiredState != state->currentState) {
            digitalWrite(cfg->outputPin, desiredState ? HIGH : LOW);
            state->currentState = desiredState;
            state->lastStateChange = millis();
            state->stateChangeCount++;

            msg.debug.info(TAG_RELAY, "Relay %d -> %s", i, desiredState ? "ON" : "OFF");
        }
    }
}

// ===== CONFIGURATION API =====

/**
 * Set relay output pin
 * @param relayIndex Relay index (0-1)
 * @param pin GPIO pin number
 * @return true if successful
 */
bool setRelayPin(uint8_t relayIndex, uint8_t pin) {
    if (relayIndex >= MAX_RELAYS) {
        msg.control.println(F("ERROR: Invalid relay index"));
        return false;
    }

    RelayConfig* cfg = &systemConfig.relays[relayIndex];

    // Configure new pin
    cfg->outputPin = pin;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);  // Start in OFF state

    return true;
}

/**
 * Link relay to sensor input
 * @param relayIndex Relay index (0-1)
 * @param inputPin Sensor pin number (e.g., A0, A1, etc.)
 * @return true if successful
 */
bool setRelayInput(uint8_t relayIndex, uint8_t inputPin) {
    if (relayIndex >= MAX_RELAYS) {
        msg.control.println(F("ERROR: Invalid relay index"));
        return false;
    }

    uint8_t inputIndex = getInputIndexByPin(inputPin);
    if (inputIndex == 0xFF) {
        msg.control.print(F("ERROR: No enabled input found on pin "));
        msg.control.println(inputPin);
        return false;
    }

    systemConfig.relays[relayIndex].inputIndex = inputIndex;
    return true;
}

/**
 * Set relay thresholds
 * @param relayIndex Relay index (0-1)
 * @param thresholdOn Activation threshold
 * @param thresholdOff Deactivation threshold
 * @return true if successful
 */
bool setRelayThresholds(uint8_t relayIndex, float thresholdOn, float thresholdOff) {
    if (relayIndex >= MAX_RELAYS) {
        msg.control.println(F("ERROR: Invalid relay index"));
        return false;
    }

    RelayConfig* cfg = &systemConfig.relays[relayIndex];

    // Validation warning for AUTO_HIGH mode
    if (cfg->mode == RELAY_AUTO_HIGH && thresholdOff >= thresholdOn) {
        msg.control.println(F("WARNING: For AUTO_HIGH, thresholdOff should be < thresholdOn"));
    }

    // Validation warning for AUTO_LOW mode
    if (cfg->mode == RELAY_AUTO_LOW && thresholdOn >= thresholdOff) {
        msg.control.println(F("WARNING: For AUTO_LOW, thresholdOn should be < thresholdOff"));
    }

    cfg->thresholdOn = thresholdOn;
    cfg->thresholdOff = thresholdOff;
    return true;
}

/**
 * Set relay mode
 * @param relayIndex Relay index (0-1)
 * @param mode RelayMode enum value
 * @return true if successful
 */
bool setRelayMode(uint8_t relayIndex, RelayMode mode) {
    if (relayIndex >= MAX_RELAYS) {
        msg.control.println(F("ERROR: Invalid relay index"));
        return false;
    }

    systemConfig.relays[relayIndex].mode = mode;
    return true;
}

/**
 * Get current relay state
 * @param relayIndex Relay index (0-1)
 * @return Current state (true = ON, false = OFF)
 */
bool getRelayState(uint8_t relayIndex) {
    if (relayIndex >= MAX_RELAYS) {
        return false;
    }

    return relayStates[relayIndex].currentState;
}

// ===== QUERY FUNCTIONS =====

/**
 * Print status of a specific relay
 * @param relayIndex Relay index (0-1)
 */
void printRelayStatus(uint8_t relayIndex) {
    if (relayIndex >= MAX_RELAYS) {
        msg.control.println(F("ERROR: Invalid relay index"));
        return;
    }

    RelayConfig* cfg = &systemConfig.relays[relayIndex];
    RelayRuntimeState* state = &relayStates[relayIndex];

    msg.control.println(F("================================="));
    msg.control.print(F("Relay "));
    msg.control.println(relayIndex);
    msg.control.println(F("================================="));

    // Configuration
    msg.control.print(F("Output Pin: "));
    if (cfg->outputPin == 0xFF) {
        msg.control.println(F("Not configured"));
    } else {
        msg.control.println(cfg->outputPin);
    }

    msg.control.print(F("Input: "));
    if (cfg->inputIndex == 0xFF) {
        msg.control.println(F("Not assigned"));
    } else {
        Input* input = &inputs[cfg->inputIndex];
        msg.control.print(F("Pin "));
        msg.control.print(input->pin);
        msg.control.print(F(" ("));
        msg.control.print(input->abbrName);
        msg.control.println(F(")"));

        msg.control.print(F("  Current Value: "));
        msg.control.print(input->value);
        msg.control.print(F(" "));
        const UnitsInfo* units = getUnitsByIndex(input->unitsIndex);
        if (units) {
            msg.control.println((__FlashStringHelper*)units->symbol);
        } else {
            msg.control.println();
        }
    }

    msg.control.print(F("Mode: "));
    switch (cfg->mode) {
        case RELAY_DISABLED:    msg.control.println(F("DISABLED")); break;
        case RELAY_AUTO_HIGH:   msg.control.println(F("AUTO_HIGH")); break;
        case RELAY_AUTO_LOW:    msg.control.println(F("AUTO_LOW")); break;
        case RELAY_MANUAL_ON:   msg.control.println(F("MANUAL_ON")); break;
        case RELAY_MANUAL_OFF:  msg.control.println(F("MANUAL_OFF")); break;
        default:                msg.control.println(F("UNKNOWN")); break;
    }

    msg.control.print(F("Threshold ON: "));
    msg.control.println(cfg->thresholdOn);
    msg.control.print(F("Threshold OFF: "));
    msg.control.println(cfg->thresholdOff);

    // Runtime state
    msg.control.print(F("Current State: "));
    msg.control.println(state->currentState ? F("ON") : F("OFF"));

    msg.control.print(F("State Changes: "));
    msg.control.println(state->stateChangeCount);

    msg.control.print(F("Last Change: "));
    uint32_t secondsAgo = (millis() - state->lastStateChange) / 1000;
    msg.control.print(secondsAgo);
    msg.control.println(F(" seconds ago"));
    msg.control.println();
}

/**
 * Print status of all relays
 */
void printAllRelayStatus() {
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        printRelayStatus(i);
    }
}

#endif // ENABLE_RELAY_OUTPUT
