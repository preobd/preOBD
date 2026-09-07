/*
 * input_manager.h - Input Configuration & Management
 *
 * Manages configuration and runtime operation of input pins, including:
 * - Initialization from EEPROM or static config
 * - Setting Application (measurement type) and Sensor (hardware)
 * - EEPROM persistence (in EEPROM/serial config mode)
 * - Runtime queries and modifications
 * - Custom calibration overrides
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "../config.h"
#include "../lib/platform.h"
#include <Arduino.h>
#include "input.h"

// Global inputs array
extern Input inputs[MAX_INPUTS];
extern uint8_t numActiveInputs;

// ===== INITIALIZATION =====
bool initInputManager();              // Initialize and load from EEPROM (returns true if EEPROM config loaded)

// ===== CONFIGURATION =====
bool allocateInputSlot(uint8_t pin);  // Create an empty input slot (required before setInputSensor on a new pin)
void freeInputSlot(uint8_t pin);      // Release a slot allocated by allocateInputSlot on failure

// setInputApplication() — assign an application preset to `pin`, creating the
// input if it doesn't exist yet.
//
// CONTRACT — all-or-nothing. On `false` NOTHING has changed:
//   * no slot was claimed and numActiveInputs is untouched, so a refused NEW
//     input leaves no half-built slot behind (an enabled slot with a null
//     readFunction is the #361/#353 board fault);
//   * an EXISTING input at `pin` keeps *every* field it had — application,
//     names, unitsIndex, min/max, obd2pid/length, outputMask, divider_ratio,
//     flags.display / flags.alarm / flags.useCustomCalibration, and in
//     particular flags.isEnabled, which a refusal must never flip on (#364).
// It is guaranteed structurally, by validating the candidate before writing
// anything — not by a revert. Callers MUST honour the return value: on failure
// treat the input as still holding its previous configuration and do not
// continue building it (that omission was #363).
//
// On `true` the input exists, is populated from the preset, and has a valid
// readFunction installed by setInputSensor().
bool setInputApplication(uint8_t pin, uint8_t appIndex);
bool setInputSensor(uint8_t pin, uint8_t sensorIndex);
bool setInputAbbr(uint8_t pin, const char* abbr);
bool setInputDisplayName(uint8_t pin, const char* displayName);
bool setInputUnits(uint8_t pin, uint8_t unitsIndex);
bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue);
bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length);
bool enableInput(uint8_t pin, bool enable);
bool enableInputAlarm(uint8_t pin, bool enable);
bool enableInputDisplay(uint8_t pin, bool enable);
bool setInputAlarmWarmup(uint8_t pin, uint16_t warmupTime_ms);
bool setInputAlarmPersist(uint8_t pin, uint16_t persistTime_ms);
bool setInputOutputMask(uint8_t pin, uint8_t outputId, bool enable);
bool clearInput(uint8_t pin);

// ===== CALIBRATION OVERRIDES =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c);
bool setInputCalibrationLookup(uint8_t pin, float bias);
bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax);
bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c);
bool clearInputCalibration(uint8_t pin);

// ===== HELPER FUNCTIONS =====
Input* getInputByPin(uint8_t pin);   // Find input by pin number
Input* getInputByIndex(uint8_t index); // Get input by array index
uint8_t getInputIndex(uint8_t pin);  // Get array index for pin

// ===== BUS REQUIREMENTS (see lib/bus_needs.h) =====
// The inputs layer's two contributions to bus ownership, both consumed by the
// bus layer without it ever including an inputs header:
//
//   storedInputBusNeeds() — a read-only pre-scan of the stored records, run at
//     boot BEFORE loadInputConfig() and initLCD() so the buses those records
//     need are up (and their pins reserved) before anything is validated (#358).
//   inputHoldsPin() — registered via setPinOwnerInputProbe() so every pin
//     claim can see configured inputs, which deliberately never enter the pin
//     registry (mega2560 has no RAM for them).
uint8_t storedInputBusNeeds();
bool inputHoldsPin(uint8_t pin);

// ===== PERSISTENCE =====
bool saveInputConfig();               // Save all inputs to EEPROM
bool loadInputConfig();               // Load all inputs from EEPROM
void resetInputConfig();              // Clear all inputs and EEPROM

// ===== RUNTIME =====
void readAllInputs();                 // Read all enabled inputs

// ===== INFO =====
// Format a pin's display label ("A0", "I2C:0", "CAN:3", or the raw number)
// into buf. Single source for LIST INPUTS and the JSON dump's "pinLabel".
void formatPinLabel(uint8_t pin, char* buf, size_t bufLen);
void printInputInfo(uint8_t pin);    // Print detailed input information
// printInputAlarmInfo() lives in cmd_rule.cpp (declared in command_handlers.h):
// the per-input alarm view renders the input's canonical alarm Rule, not the
// retired legacy Input alarm fields (#338).
void printInputOutputInfo(uint8_t pin);
void printInputCalibration(uint8_t pin);
void listAllInputs();                // List all active inputs
void listApplicationPresets();       // List available Applications
void listSensors(const char* filter = nullptr);  // List sensors (categories, by category, or by measurement type)

#endif // INPUT_MANAGER_H
