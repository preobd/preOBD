/*
 * input_manager.cpp - Input Configuration & Management Implementation
 */

#include <math.h>  // isnan
#include <string.h>
 #include "../config.h"
#include "input_manager.h"
#include "../storage/input_store.h"    // inputStoreSetProbeExclusion (#402 step 2)
#include "../cli/command_helpers.h"
#include "measurement_compat.h"
#include "../storage/system_config.h"  // OUTPUT_MASK_ALL_DATA / OutputId enum
#include "../pinctrl/analog_pins.h"
#include "../comms/message_router.h"  // For msg.control
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../pinctrl/pin_registry.h"
#include "../pinctrl/pin_owner.h"
#include "../storage/bus_needs.h"
#include "../drivers/bus_manager.h"
#include "../rules/rules_manager.h"  // rulesResetTables + rule/action table helpers

// ===== GLOBAL STATE =====
Input inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;

// True once inputs[] is the ground truth (sentinel-filled by initInputManager()
// and then loaded, successfully or not). BSS-zero (false) before that — which
// is the honest starting state: on cold start inputs[] hasn't been touched yet,
// so reading it would answer every pin "held" (pin 0) or "not held" (everything
// else) at random, neither of which is the truth (#385). See inputHoldsPin()
// below for the dispatch this drives.
static bool inputsTableIsLive = false;

// Exposes inputsTableIsLive to ASSERT_INPUTS_TABLE_LIVE() (input_manager.h)
// for callers outside this TU — the debug/native-test-only guard other
// pre-config consumers (broadcasters, etc.) call before reading
// inputs[]/rules[]/actions[] directly (#368/#369).
bool inputsTableIsLiveForAssert() { return inputsTableIsLive; }

// Production accessor for the same flag, for input_store.cpp's inputHoldsPin()
// (#402 step 2): that dispatch is a real runtime decision (§12.2 source 2),
// not a debug assert, so it gets its own properly-named accessor rather than
// overloading inputsTableIsLiveForAssert() — which stays compiled out on a
// release build and must not gain a production caller.
bool inputManagerTableIsLive() { return inputsTableIsLive; }

// ===== INITIALIZATION =====
bool initInputManager() {
    // Clear all inputs
    memset(inputs, 0, sizeof(inputs));
    numActiveInputs = 0;
#if INCLUDE_RULES_ENGINE
    // Empty the BSS-zeroed control tables up front: id==0 is a valid record id, so
    // an unstamped table looks full of phantom rule/action #0. loadInputConfig()
    // below is validate-before-mutate (it only touches the tables once a valid
    // store is confirmed), so cold-boot safety with a blank/invalid EEPROM relies
    // on this reset, not on the loader (#296 B2).
    rulesResetTables();
#endif

    // Initialize with invalid values
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;  // Invalid pin
        inputs[i].applicationIndex = 0;  // 0 = NONE
        inputs[i].sensorIndex = 0;       // 0 = NONE
    }

    // Try to load from EEPROM
    bool eepromLoaded = loadInputConfig();
    if (!eepromLoaded) {
        msg.debug.info(TAG_CONFIG, "No valid config in EEPROM - starting with blank configuration");
    }

    // inputs[] is now the ground truth — sentinel-filled above and either
    // loaded from a valid store or left blank by a rejected one. Either way
    // it is the authoritative live answer from here on; inputHoldsPin()
    // switches off the boot-window EEPROM pre-scan (#385).
    inputsTableIsLive = true;

    return eepromLoaded;
}

// ===== HELPER FUNCTIONS =====

/**
 * Find an input configuration by its physical pin number.
 * @param pin  Pin number (use A0, A1, etc. for analog pins)
 * @return Pointer to Input struct, or nullptr if pin not configured
 */
Input* getInputByPin(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return &inputs[i];
        }
    }
    return nullptr;
}

/**
 * Find an input configuration by its array index.
 * @param index  Array index (0 to MAX_INPUTS-1)
 * @return Pointer to Input struct, or nullptr if index out of range
 * @note  Unlike getInputByPin(), this returns even unconfigured slots
 */
Input* getInputByIndex(uint8_t index) {
    if (index < MAX_INPUTS) {
        return &inputs[index];
    }
    return nullptr;
}

/**
 * Get the array index for a given pin number.
 * @param pin  Pin number to search for
 * @return Array index (0 to MAX_INPUTS-1), or 0xFF if not found
 */
uint8_t getInputIndex(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

/**
 * Find the first unused slot in the inputs array.
 * @return Array index of free slot, or 0xFF if array is full
 */
static uint8_t findFreeSlot() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) {
            return i;
        }
    }
    return 0xFF;  // No free slots
}

/**
 * Validate a CANDIDATE input configuration.
 *
 * Takes the fields it judges by value rather than an Input* on purpose: it is
 * called BEFORE anything is written to the slot, so a refusal costs nothing to
 * undo. See the setInputApplication() contract in input_manager.h (#361/#364).
 *
 * @param pin           pin the candidate wants
 * @param self          the input already occupying `pin`, or nullptr for a new
 *                      one — skipped in the conflict scan so an existing input
 *                      never collides with itself
 * @param alarmEnabled  candidate flags.alarm
 * @param minValue      candidate alarm minimum (standard units)
 * @param maxValue      candidate alarm maximum (standard units)
 *
 * Checks for:
 * - Pin ownership conflicts — registry (bus/system pin/action) AND other
 *   configured inputs, both via the one predicate (#384/#385)
 * - Alarm threshold sanity (min < max)
 * Returns true if valid, false if errors found
 */
static bool validateInputConfig(uint8_t pin, const Input* self,
                                bool alarmEnabled, float minValue, float maxValue) {
    // One door for pin ownership (#384). This used to be TWO checks here: a
    // registry-only lookup that deliberately skipped the input probe (no
    // self-exclusion existed, so a candidate re-validating its OWN unchanged
    // pin would see itself and misreport a conflict), plus a hand-rolled
    // other-input scan that duplicated exactly what the probe already knows.
    // inputStoreSetProbeExclusion() closes that gap by identity (see its
    // declaration in storage/input_store.h) — set immediately before the
    // call, cleared immediately after so the exclusion never leaks into an
    // unrelated one. The excluded pointer itself lives in input_store.cpp,
    // behind inputHoldsPin() — this is a setter call, not a direct write,
    // now that the two live in different translation units (#402 step 2).
    inputStoreSetProbeExclusion(self);
    const bool conflict = pinsConflict(&pin, 1, PIN_RANK_INPUT, "candidate input");
    inputStoreSetProbeExclusion(nullptr);
    if (conflict) return false;

    // Check alarm threshold sanity (only if alarms enabled)
    if (alarmEnabled) {
        if (minValue >= maxValue) {
            msg.control.print(F("ERROR: Invalid alarm range ("));
            msg.control.print(minValue);
            msg.control.print(F(" >= "));
            msg.control.print(maxValue);
            msg.control.println(F(")"));
            return false;
        }
    }

    return true;
}

// ===== CONFIGURATION FUNCTIONS =====

bool allocateInputSlot(uint8_t pin) {
    if (getInputByPin(pin) != nullptr) return true;  // already exists
    uint8_t slot = findFreeSlot();
    if (slot == 0xFF) {
        msg.control.print(F("ERROR: No free input slots (max "));
        msg.control.print(MAX_INPUTS);
        msg.control.println(F(" inputs)"));
        msg.control.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
        return false;
    }
    memset(&inputs[slot], 0, sizeof(Input));
    inputs[slot].pin = pin;
    numActiveInputs++;
    return true;
}

void freeInputSlot(uint8_t pin) {
    // Wipe the whole slot, not just the pin: findFreeSlot() recycles on
    // pin == 0xFF alone, so anything left behind here (notably
    // flags.isEnabled and a stale readFunction) would be inherited by the
    // next occupant of the slot. See #361.
    //
    // clearInput() is exactly that wipe, and it *recounts* numActiveInputs
    // rather than decrementing. Recounting is what the count means -- it is
    // an enabled-count, defined that way by loadInputConfig() and the JSON
    // import -- and it keeps this helper correct no matter where a caller
    // fails relative to the increment.
    clearInput(pin);
}

bool setInputApplication(uint8_t pin, uint8_t appIndex) {
    // Find or create input
    Input* input = getInputByPin(pin);
    bool isNewInput = (input == nullptr);

    // ---- ADMISSION PHASE ------------------------------------------------
    // Everything that can say "no" runs here, BEFORE a single byte is written
    // to the input table. That is what makes the "a refusal changes nothing"
    // contract in input_manager.h structural rather than a revert each error
    // path has to remember to perform. Populate-then-validate previously left
    // a new slot enabled with a null readFunction (#361/#353, faulted the
    // board) and an existing input half-rewritten with the new application's
    // units/PID and force-enabled (#364).

    // Resolve the preset BEFORE claiming a slot. Claiming first meant this
    // early return either leaked an uncounted slot or, if it released one,
    // decremented a count that isn't incremented until further below.
    const ApplicationPreset* flashPreset = getApplicationByIndex(appIndex);
    if (flashPreset == nullptr) {
        msg.control.println(F("ERROR: Invalid Type"));
        return false;
    }

    // Load entire preset into RAM (cleaner than scattered pgm_read_* calls)
    ApplicationPreset preset;
    loadApplicationPreset(flashPreset, &preset);

    // The alarm flag the candidate would carry — validateInputConfig only looks
    // at the min/max band when it's armed, so it has to match what the commit
    // phase below writes.
#if INCLUDE_RULES_ENGINE
    const bool candidateAlarm = false;
#else
    const bool candidateAlarm = preset.defaultAlarmEnabled;
#endif
    if (!validateInputConfig(pin, input, candidateAlarm,
                             preset.defaultMinValue, preset.defaultMaxValue)) {
        return false;
    }

    // Resolve the preset's sensor now as well. setInputSensor() runs AFTER the
    // commit phase, and an invalid index is its only failure mode once the slot
    // exists — so checking it here is what lets the commit phase be infallible
    // (an existing input has no slot to free and nothing to revert to).
    uint8_t defaultSensor = preset.defaultSensor;
    if (getSensorByIndex(defaultSensor) == nullptr) {
        msg.control.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    // Measurement-type compatibility (#383), one predicate shared with
    // setInputSensor() below. A well-formed preset's own default sensor
    // always matches its expectedMeasurementType by construction; this is a
    // tripwire against a future preset table entry that doesn't.
    if (!sensorMatchesApplication(defaultSensor, appIndex)) {
        msg.control.println(F("ERROR: Application preset's default sensor does not match its measurement type"));
        return false;
    }

    if (isNewInput) {
        uint8_t slot = findFreeSlot();
        if (slot == 0xFF) {
            msg.control.print(F("ERROR: No free input slots (max "));
            msg.control.print(MAX_INPUTS);
            msg.control.println(F(" inputs)"));
            msg.control.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
            return false;
        }
        input = &inputs[slot];
        memset(input, 0, sizeof(Input));
        input->pin = pin;
    }

    // ---- COMMIT PHASE ---------------------------------------------------
    // Past this point nothing may fail; do not add a `return false` below.

    // Apply preset to input
    input->applicationIndex = appIndex;

    // Read abbreviation and label from PROGMEM pointers
    // (loadApplicationPreset copies the struct, but string pointers still point to PROGMEM)
    strncpy_P(input->abbrName, preset.abbreviation, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';

    if (preset.label) {
        strncpy_P(input->displayName, preset.label, sizeof(input->displayName) - 1);
    } else {
        strncpy_P(input->displayName, preset.name, sizeof(input->displayName) - 1);
    }
    input->displayName[sizeof(input->displayName) - 1] = '\0';

    // Don't set sensorIndex yet - let setInputSensor() do it
    // (This allows sensorChanged check to work correctly; defaultSensor was
    // resolved and validated in the admission phase above.)
    input->unitsIndex = preset.defaultUnits;

    // CRITICAL: Store min/max in STANDARD UNITS (no conversion!)
    // Preset already has values in Celsius, bar, volts, etc.
    input->minValue = preset.defaultMinValue;
    input->maxValue = preset.defaultMaxValue;

    input->obd2pid = preset.obd2pid;
    input->obd2length = preset.obd2length;
    // App-seeded alarms are now control-engine Rules (§11.5): the caller
    // (set_application) seeds a default Rule from the template. Don't also arm
    // the legacy per-input alarm, or the threshold would be enforced twice.
    // (candidateAlarm resolves that build-flag choice once, above, so the
    // validator and this assignment can never disagree.)
    input->flags.alarm = candidateAlarm;
    input->flags.display = preset.defaultDisplayEnabled;
    input->flags.isEnabled = true;
    input->flags.useCustomCalibration = false;  // Use preset calibration
    input->outputMask = OUTPUT_MASK_ALL_DATA;   // All data outputs enabled by default
    input->divider_ratio = 1.0f;                // Default to no divider

    // (Legacy alarm context removed — set_application seeds a control-engine
    // Rule from the template instead, §11.5.)

    // Increment count if this is a new input
    if (isNewInput) {
        numActiveInputs++;
    }

    // Set up sensor (function pointers + calibration)
    // This also sets input->sensorIndex. It cannot fail here: the slot exists
    // and defaultSensor was resolved during admission. The guard stays as a
    // tripwire in case a future failure mode is added to setInputSensor() --
    // and it must free the slot, because an enabled slot with a null
    // readFunction is exactly the #361 fault.
    if (!setInputSensor(pin, defaultSensor)) {
        if (isNewInput) freeInputSlot(pin);
        return false;
    }
    return true;
}

bool setInputSensor(uint8_t pin, uint8_t sensorIndex) {
    Input* input = requireInput(pin);
    if (!input) return false;

    // ---- ADMISSION PHASE ------------------------------------------------
    // Get Sensor info from flash
    const SensorInfo* flashInfo = getSensorByIndex(sensorIndex);
    if (flashInfo == nullptr) {
        msg.control.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    // Measurement-type compatibility (#383): this is the real enforcement
    // point. An input with an application already assigned (the normal
    // case -- SENSOR can only target an existing input, see requireInput())
    // cannot be pointed at a sensor that measures something else. This is
    // what closes the CLI two-step gap (SET <pin> APPLICATION <x> then
    // SET <pin> SENSOR <y-of-a-different-type>) and, via
    // importInputFromJSON()'s setInputApplication()+setInputSensor() pair,
    // the JSON import gap too -- both call this same function.
    if (!sensorMatchesApplication(sensorIndex, input->applicationIndex)) {
        msg.control.println(F("ERROR: Sensor/application measurement type mismatch"));
        return false;
    }

    // Load entire sensor info into RAM
    SensorInfo info;
    loadSensorInfo(flashInfo, &info);

    // ---- COMMIT PHASE -----------------------------------------------------
    // Nothing below this point may fail.

    // Check if sensor is actually changing (to avoid redundant init)
    bool sensorChanged = (input->sensorIndex != sensorIndex);

    // Apply sensor info to input
    input->sensorIndex = sensorIndex;
    input->readFunction = info.readFunction;
    input->measurementType = info.measurementType;
    input->calibrationType = info.calibrationType;

    // Point to calibration in PROGMEM (don't copy to RAM unless custom)
    input->presetCalibration = info.defaultCalibration;
    input->flags.useCustomCalibration = false;

    // Call sensor-specific initialization function only if sensor changed
    // (Prevents duplicate init when setting same sensor twice)
    if (sensorChanged && info.initFunction) {
        info.initFunction(input);
    }

    // The record is legitimate either way — don't fail the assignment — but if
    // this sensor needs a bus that isn't up (no explicit selection AND no
    // build-time WIRING declaration, #377), it cannot read yet. Tell the user
    // what to run. Same guidance as the boot diagnosis (main.cpp), through the
    // same rate-limited helper, so the two paths cannot drift apart.
    uint8_t needs = busNeedsForSensorIndex(sensorIndex);
    if ((needs & BUS_NEED_I2C) && !getActiveI2C()) reportNoI2CBus();
    if ((needs & BUS_NEED_SPI) && !getActiveSPI()) reportNoSPIBus();

    return true;
}

bool setInputAbbr(uint8_t pin, const char* abbr) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->abbrName, abbr, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';
    return true;
}

bool setInputDisplayName(uint8_t pin, const char* displayName) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->displayName, displayName, sizeof(input->displayName) - 1);
    input->displayName[sizeof(input->displayName) - 1] = '\0';
    return true;
}

bool setInputUnits(uint8_t pin, uint8_t unitsIndex) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->unitsIndex = unitsIndex;
    return true;
}

bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    // Validate range (allow both to be 0 for disabled alarms)
    if (minValue >= maxValue && !(minValue == 0 && maxValue == 0)) {
        msg.control.print(F("ERROR: Min alarm ("));
        msg.control.print(minValue);
        msg.control.print(F(") must be less than max alarm ("));
        msg.control.print(maxValue);
        msg.control.println(F(")"));
        return false;
    }

    // Validate against sensor capabilities
    const SensorInfo* sensorInfo = getSensorByIndex(input->sensorIndex);
    if (sensorInfo && input->sensorIndex != 0) {  // Skip validation for SENSOR_NONE
        // Check if alarm range exceeds sensor's physical capabilities
        if (minValue < sensorInfo->minValue || maxValue > sensorInfo->maxValue) {
            msg.control.print(F("WARNING: Alarm range ("));
            msg.control.print(minValue);
            msg.control.print(F(" - "));
            msg.control.print(maxValue);
            msg.control.print(F(") exceeds sensor capability ("));
            msg.control.print(sensorInfo->minValue);
            msg.control.print(F(" - "));
            msg.control.print(sensorInfo->maxValue);

            // Print sensor name for clarity
            char sensorName[32];
            strcpy_P(sensorName, (const char*)sensorInfo->name);
            msg.control.print(F(") for "));
            msg.control.println(sensorName);

            // Don't fail - allow the user to set it, but warn them
            // This is useful for sensors that might be replaced or for edge cases
        }
    }

    // Store in STANDARD UNITS (caller's responsibility to provide correct units)
    input->minValue = minValue;
    input->maxValue = maxValue;
    return true;
}

bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->obd2pid = pid;
    input->obd2length = length;
    return true;
}

bool enableInput(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.isEnabled = enable;
    return true;
}

bool enableInputAlarm(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    // Idempotent: only (re)arm on an actual flag transition. Re-initializing on
    // every call would reset an already-armed alarm back to ALARM_INIT/warmup
    // and clear faultStartTime/isInAlarm — so a periodic config re-sync that
    // re-issues SET <pin> ALARM ENABLE could keep an alarm perpetually in
    // warmup and prevent it from ever firing (and would silently suppress a
    // live alarm). The EEPROM load path already arms the context on boot, so
    // there is no "flag true but DISABLED" state to recover here.
    if (input->flags.alarm == enable) return true;

    input->flags.alarm = enable;  // dead field (alarms are Rules); retained for json compat
    return true;
}

bool enableInputDisplay(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.display = enable;
    return true;
}

bool setInputAlarmWarmup(uint8_t pin, uint16_t warmupTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.warmupTime_ms = warmupTime_ms;
    return true;
}

bool setInputAlarmPersist(uint8_t pin, uint16_t persistTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.persistTime_ms = persistTime_ms;
    return true;
}

bool setInputOutputMask(uint8_t pin, uint8_t outputId, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    if (outputId > OUTPUT_SD) return false;  // Only data outputs (0-3)

    if (enable) {
        input->outputMask |= (1 << outputId);
    } else {
        input->outputMask &= ~(1 << outputId);
    }
    return true;
}

bool clearInput(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    memset(input, 0, sizeof(Input));
    input->pin = 0xFF;

    // Recount active inputs
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;
        }
    }

    return true;
}

// ===== CALIBRATION OVERRIDE FUNCTIONS =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.steinhart.bias_resistor = bias;
    input->customCalibration.steinhart.steinhart_a = a;
    input->customCalibration.steinhart.steinhart_b = b;
    input->customCalibration.steinhart.steinhart_c = c;

    return true;
}

bool setInputCalibrationLookup(uint8_t pin, float bias) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.lookup.bias_resistor = bias;

    return true;
}

bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressureLinear.voltage_min = vMin;
    input->customCalibration.pressureLinear.voltage_max = vMax;
    input->customCalibration.pressureLinear.output_min = pMin;
    input->customCalibration.pressureLinear.output_max = pMax;

    return true;
}

bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressurePolynomial.bias_resistor = bias;
    input->customCalibration.pressurePolynomial.poly_a = a;
    input->customCalibration.pressurePolynomial.poly_b = b;
    input->customCalibration.pressurePolynomial.poly_c = c;

    return true;
}

bool clearInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = false;
    memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

    return true;
}

// ===== RUNTIME =====

void readAllInputs() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled && inputs[i].readFunction != nullptr) {
            inputs[i].readFunction(&inputs[i]);
        }
    }
}
