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
#include "../core/platform.h"
#include <Arduino.h>
#include "input.h"

// Global inputs array
extern Input inputs[MAX_INPUTS];
extern uint8_t numActiveInputs;

// True once inputs[] (and the control tables, see initInputManager()) are the
// live, loaded ground truth rather than raw BSS. Read this — never assume the
// table is safe to read just because you're past setup() — before any code
// added outside input_manager.cpp scans inputs[]/rules[]/actions[] directly
// (#368/#369; see ASSERT_INPUTS_TABLE_LIVE below and the comment at the
// broadcaster init() block in main.cpp).
bool inputsTableIsLiveForAssert();

// Production accessor for the same flag — used by storage/input_store.cpp's
// inputHoldsPin() (§12.2 source 2), which is a real runtime dispatch, not a
// debug assert, so it does not go through the guard above (#402 step 2).
bool inputManagerTableIsLive();

// Debug-build-only guard for a consumer that reads inputs[]/rules[]/actions[]
// directly (bypassing the accessor functions below). Fires only in the DEBUG
// build env (-D DEBUG, see boards/teensy41/board.ini) and the native test
// build (-D NATIVE_TEST); compiles to nothing elsewhere, so it costs zero
// flash/RAM on shipping firmware — mega2560 sits at 97.7% RAM and cannot
// afford a RAM-resident guard on every build (see CLAUDE.md AVR debug-string
// note). Catches the #352 defect class at its source: a broadcaster (or any
// other pre-config consumer) reading the table before initInputManager() has
// sentinel-filled and loaded it, instead of silently reading BSS-zero and
// surfacing minutes later on the wire.
#if defined(DEBUG) || defined(NATIVE_TEST)
#include <assert.h>
#define ASSERT_INPUTS_TABLE_LIVE() assert(inputsTableIsLiveForAssert())
#else
#define ASSERT_INPUTS_TABLE_LIVE() ((void)0)
#endif

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

// Look up an Input by pin; if absent, print the standard "ERROR: Input not
// configured" message on the control plane and return nullptr. Used by every
// pin-scoped CLI leaf and by setInputSensor() here, which is why it lives with
// the Input model rather than in cli/ (#411).
Input* requireInput(uint8_t pin);
Input* getInputByIndex(uint8_t index); // Get input by array index
uint8_t getInputIndex(uint8_t pin);  // Get array index for pin

// ===== BUS REQUIREMENTS / PERSISTENCE =====
// storedInputBusNeeds(), inputHoldsPin(), saveInputConfig(), loadInputConfig()
// and resetInputConfig() moved to storage/input_store.h in #402 step 2 — the
// shared EEPROM config-store arena is a storage-format concern, not part of
// the Input model. Include "../storage/input_store.h" for those.

// ===== RUNTIME =====
void readAllInputs();                 // Read all enabled inputs

// ===== INFO =====
// Presentation over inputs[] (printInputInfo, printInputOutputInfo,
// printInputCalibration, listAllInputs) lives in cli/input_display.h, and the
// pin-label decoder (formatPinLabel) lives in pinctrl/pin_label.h, and the
// sensor/application registry listers (listApplicationPresets, listSensors)
// live in cli/registry_display.h — moved out of the Input model in #402
// step 1 since none of it is domain-model behaviour.

#endif // INPUT_MANAGER_H
