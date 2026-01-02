/*
 * output_relay.h - Relay output module for controlling 12V relays
 *
 * Enables automatic control of relays based on sensor thresholds with hysteresis.
 * Supports manual override and EEPROM-backed configuration.
 *
 * Example use cases:
 *   - Turn on cooling fan when coolant temp >= 100°C, off at 95°C
 *   - Activate warning light when oil pressure drops below threshold
 *   - Control electric water pump based on temperature
 */

#ifndef OUTPUT_RELAY_H
#define OUTPUT_RELAY_H

#include <Arduino.h>
#include "../inputs/input.h"

// Maximum number of relays supported (can be overridden by build flag)
#ifndef MAX_RELAYS
#define MAX_RELAYS 2
#endif

// ===== RELAY CONFIGURATION =====

// Relay operating modes
enum RelayMode : uint8_t {
    RELAY_DISABLED = 0,     // Relay disabled (no output)
    RELAY_AUTO_HIGH = 1,    // Turn ON when value >= thresholdOn, OFF when value <= thresholdOff
    RELAY_AUTO_LOW = 2,     // Turn ON when value <= thresholdOn, OFF when value >= thresholdOff
    RELAY_MANUAL_ON = 3,    // Manual override - forced ON
    RELAY_MANUAL_OFF = 4    // Manual override - forced OFF
};

// Per-relay configuration (16 bytes) - stored in EEPROM
struct RelayConfig {
    uint8_t outputPin;           // GPIO pin number (0xFF = unconfigured)
    uint8_t inputIndex;          // Index into inputs[] array (0xFF = unassigned)
    uint8_t mode;                // RelayMode enum
    uint8_t reserved;            // Padding for alignment
    float thresholdOn;           // Activation threshold in standard units (°C, bar, etc.)
    float thresholdOff;          // Deactivation threshold in standard units
    uint32_t reserved2;          // Future expansion
};

// Runtime state (not persisted to EEPROM)
struct RelayRuntimeState {
    bool currentState;           // Current relay output state (HIGH/LOW)
    uint32_t lastStateChange;    // millis() when state last changed
    uint32_t stateChangeCount;   // Debug counter for state changes
};

// ===== OUTPUT MODULE INTERFACE =====
// These functions are called by output_manager.cpp

void initRelayOutput();          // Initialize relay GPIO pins
void sendRelayOutput(Input* input);  // Per-input send (unused for relays)
void updateRelayOutput();        // Main update function (called every loop)

// ===== CONFIGURATION API =====
// Used by serial command handlers

bool setRelayPin(uint8_t relayIndex, uint8_t pin);
bool setRelayInput(uint8_t relayIndex, uint8_t inputPin);
bool setRelayThresholds(uint8_t relayIndex, float thresholdOn, float thresholdOff);
bool setRelayMode(uint8_t relayIndex, RelayMode mode);
bool getRelayState(uint8_t relayIndex);

// ===== QUERY FUNCTIONS =====
// For status display and debugging

void printRelayStatus(uint8_t relayIndex);
void printAllRelayStatus();

#endif // OUTPUT_RELAY_H
