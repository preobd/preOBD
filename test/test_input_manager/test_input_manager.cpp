// Native unit test: setInputApplication()'s admission contract
// (src/inputs/input_manager.cpp).
//
// The property under test is the one documented on the declaration in
// input_manager.h: a REFUSED setInputApplication() changes nothing at all.
// Three issues live here:
//   #361/#353 — a refused NEW input used to leave an enabled slot behind with a
//               null readFunction, which updateSensors() then dispatched into.
//   #364      — a refused EXISTING input used to keep the new application's
//               units/PID/names and, worse, was force-ENABLED by the refusal.
//   #363      — the JSON import path never even looked at the return value.
//
// input_manager.cpp compiles on the host essentially unchanged: the registries
// it reads (APPLICATION_PRESETS / SENSOR_LIBRARY, defined in their own .cpp
// per #400) are PROGMEM tables that the stub turns into plain RAM. What it
// needs from the rest of the
// firmware comes from test/stubs/firmware_stubs.h — a null-transport message
// router (so every msg.control print is a no-op), a test-settable
// isPinAvailable(), and no-op bodies for the sensor read/init functions the
// library tables point at (referenced as data, so -dead_strip can't drop them).
//
// Calibration round-trip coverage for the same import path lives next door in
// test_json_calibration (#373).
#include <unity.h>
#include <Arduino.h>
#include <string.h>

// SENSOR_LIBRARY / UNITS_REGISTRY / APPLICATION_PRESETS are now defined in
// their own .cpp (#400), not inline in the header — pull the data in.
// SENSOR_CATEGORIES too (missed by #400, fixed separately) — pull it in.
#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/input_manager.cpp"
#include "../../src/storage/input_store.cpp"   // EEPROM persistence, split out by #402 step 2
#include "../../src/inputs/calibration_apply.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/storage/json_config.cpp"

// Firmware symbols input_manager.cpp / json_config.cpp expect from other TUs
// (shared with test_json_calibration).
#include "../stubs/firmware_stubs.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void setUp(void) {
    memset(g_pinReserved, 0, sizeof(g_pinReserved));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        memset(&inputs[i], 0, sizeof(Input));
        inputs[i].pin = 0xFF;
    }
    numActiveInputs = 0;
    // This suite drives inputs[] directly rather than through
    // initInputManager(), so tell inputHoldsPin() (#385) inputs[] is the live
    // table — otherwise it would fall back to the (here, blank) EEPROM scan
    // and every input-vs-input conflict check below would go silently inert.
    inputsTableIsLive = true;
}
void tearDown(void) {}

static uint8_t appCHT()  { return getApplicationIndexByName("CHT"); }
static uint8_t appOIL_P(){ return getApplicationIndexByName("OIL_PRESSURE"); }

static uint8_t occupiedSlots() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) if (inputs[i].pin != 0xFF) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Sanity: the registries really are reachable on the host.
// ---------------------------------------------------------------------------

static void test_registry_available(void) {
    TEST_ASSERT_NOT_EQUAL(0, appCHT());
    TEST_ASSERT_NOT_EQUAL(0, appOIL_P());
    TEST_ASSERT_NOT_EQUAL(appCHT(), appOIL_P());
}

static void test_accepts_a_valid_new_input(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));
    Input* in = getInputByPin(4);
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_TRUE(in->flags.isEnabled);
    TEST_ASSERT_NOT_NULL(in->readFunction);
    TEST_ASSERT_EQUAL_UINT8(1, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(1, numActiveInputs);
}

// ---------------------------------------------------------------------------
// #361/#353 — a refused NEW input must leave no slot behind at all.
// ---------------------------------------------------------------------------

static void test_refused_new_input_claims_no_slot(void) {
    g_pinReserved[7] = true;  // pin held by a bus
    TEST_ASSERT_FALSE(setInputApplication(7, appCHT()));
    TEST_ASSERT_NULL(getInputByPin(7));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
    // Nothing enabled, and nothing enabled without a readFunction (the #361
    // fault updateSensors() dispatched into).
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        TEST_ASSERT_FALSE(inputs[i].flags.isEnabled);
    }
}

// ---------------------------------------------------------------------------
// #364 — a refused EXISTING input must be byte-for-byte unchanged.
// ---------------------------------------------------------------------------

static void test_refused_existing_input_is_byte_identical(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));

    // The operator has since customised the input; capture the whole slot.
    Input* in = getInputByPin(4);
    in->unitsIndex = 3;
    in->obd2pid = 0x5C;
    in->obd2length = 2;
    in->flags.useCustomCalibration = true;
    in->outputMask = 0x02;
    Input before = *in;

    // Now the pin gets reserved out from under it and a re-application is tried.
    g_pinReserved[4] = true;
    TEST_ASSERT_FALSE(setInputApplication(4, appOIL_P()));

    Input* after = getInputByPin(4);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, after, sizeof(Input)));
    TEST_ASSERT_EQUAL_UINT8(1, numActiveInputs);
}

// The sharpest form of #364: the refusal used to *assign* isEnabled = true, so
// a rejected SET could start reads on a disabled input sitting on a bus pin.
static void test_refused_existing_input_stays_disabled(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));
    Input* in = getInputByPin(4);
    in->flags.isEnabled = false;

    g_pinReserved[4] = true;
    TEST_ASSERT_FALSE(setInputApplication(4, appOIL_P()));

    TEST_ASSERT_FALSE(getInputByPin(4)->flags.isEnabled);
}

// A refusal for the *conflict* reason (another enabled input already holds the
// pin) must be just as clean — different validator branch, same contract.
// The target of the re-application is the FIRST slot on the pin; a second,
// enabled slot squatting on the same pin is what the conflict scan trips over.
static void test_refused_on_pin_conflict_leaves_target_intact(void) {
    TEST_ASSERT_TRUE(setInputApplication(5, appCHT()));    // slot 0, pin 5
    TEST_ASSERT_TRUE(setInputApplication(6, appOIL_P()));  // slot 1, pin 6
    inputs[1].pin = 5;  // squatter: enabled, same pin, different slot

    Input before = inputs[0];
    TEST_ASSERT_FALSE(setInputApplication(5, appOIL_P()));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &inputs[0], sizeof(Input)));
}

// An unknown application index is refused before anything is touched too.
static void test_refused_bad_application_claims_no_slot(void) {
    TEST_ASSERT_FALSE(setInputApplication(9, 250));
    TEST_ASSERT_NULL(getInputByPin(9));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
}

// A successful re-application still does its job (guards against "fix" by
// simply refusing to update existing inputs).
static void test_successful_reapplication_updates_existing_input(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    Input* in = getInputByPin(4);
    TEST_ASSERT_EQUAL_UINT8(appOIL_P(), in->applicationIndex);
    TEST_ASSERT_EQUAL_UINT8(1, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(1, numActiveInputs);
}

// ---------------------------------------------------------------------------
// #383 — sensor/application measurement-type compatibility is enforced from
// setInputApplication()/setInputSensor()'s admission phase, so every writer
// that routes through them agrees: the CLI's combined syntax, its two-step
// form, and JSON import.
// ---------------------------------------------------------------------------

// The CLI's two-step form: SET <pin> APPLICATION <x> (ok) then
// SET <pin> SENSOR <y> where y measures something else. Before #383,
// set_application() ran no compatibility check at all and setInputSensor()
// only checked that the sensor index resolved -- this combination was
// silently accepted.
//
// MUTANT TARGET: removing the sensorMatchesApplication() call from
// setInputSensor()'s admission phase (input_manager.cpp) must fail this test.
static void test_two_step_refuses_measurement_type_mismatch(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));  // CHT -> MEASURE_TEMPERATURE
    Input* before = getInputByPin(4);
    Input snapshot = *before;

    uint8_t pressureSensor = getSensorIndexByName("MPX4250AP");  // MEASURE_PRESSURE
    TEST_ASSERT_NOT_EQUAL(0, pressureSensor);

    TEST_ASSERT_FALSE(setInputSensor(4, pressureSensor));

    // Refused: the input must be byte-identical to before the attempt (still
    // on the CHT preset's own sensor), not half-switched to the pressure part.
    Input* after = getInputByPin(4);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&snapshot, after, sizeof(Input)));
}

// A matching sensor of the SAME measurement type is still accepted -- the
// gate must not become "no sensor override ever succeeds".
static void test_two_step_accepts_matching_measurement_type(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));
    uint8_t otherTempSensor = getSensorIndexByName("MAX31855");  // also MEASURE_TEMPERATURE
    TEST_ASSERT_NOT_EQUAL(0, otherTempSensor);
    TEST_ASSERT_TRUE(setInputSensor(4, otherTempSensor));
    TEST_ASSERT_EQUAL_UINT8(otherTempSensor, getInputByPin(4)->sensorIndex);
}

// ---------------------------------------------------------------------------
// #363 — importInputFromJSON() must honour the refusal.
//
// The import path used to claim the slot itself (`inputs[idx].pin = pin`),
// which hid the record from setInputApplication()'s new-input branch, and then
// dropped the return value so setInputSensor() finished building an input the
// validator had refused — leaving a live readFunction driving a bus pin.
// ---------------------------------------------------------------------------

// Minimal runtime-schema record, matching what SYSTEM DUMP JSON emits.
static void makeInputRecord(JsonObject& o, uint8_t idx, uint8_t pin) {
    o["idx"] = idx;
    o["pin"] = pin;
    o["app"] = "CHT";
    o["sensor"] = "MAX6675";
    o["units"] = "CELSIUS";
    o["enabled"] = true;
}

static void test_json_import_accepts_a_valid_record(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    makeInputRecord(o, 0, 4);

    TEST_ASSERT_TRUE(importInputFromJSON(o, 0));
    Input* in = getInputByPin(4);
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_TRUE(in->flags.isEnabled);
    TEST_ASSERT_NOT_NULL(in->readFunction);
}

// #383: JSON IMPORT calls setInputApplication()/setInputSensor() directly
// (json_config.cpp), so it used to pair a pressure sensor with a temperature
// application without complaint. Both functions now share
// sensorMatchesApplication(), so the record is refused -- and, per the
// all-or-nothing import contract, leaves nothing behind.
//
// MUTANT TARGET: removing the sensorMatchesApplication() call from either
// setInputApplication() or setInputSensor()'s admission phase must fail this.
static void test_json_import_refuses_measurement_type_mismatch(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["idx"] = 0;
    o["pin"] = 4;
    o["app"] = "CHT";          // MEASURE_TEMPERATURE
    o["sensor"] = "MPX4250AP"; // MEASURE_PRESSURE
    o["units"] = "CELSIUS";
    o["enabled"] = true;

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));
    TEST_ASSERT_NULL(getInputByPin(4));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
}

static void test_json_import_refuses_a_bus_reserved_pin(void) {
    g_pinReserved[4] = true;

    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    makeInputRecord(o, 0, 4);

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));
    // No input on the reserved pin, and no half-built slot anywhere.
    TEST_ASSERT_NULL(getInputByPin(4));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        TEST_ASSERT_NULL(inputs[i].readFunction);
        TEST_ASSERT_FALSE(inputs[i].flags.isEnabled);
    }
}

// A refused record must not damage the inputs that imported cleanly before it.
// --- the predicate's own contract -------------------------------------------
// Everything above exercises sensorMatchesApplication() through a caller.
// These pin the predicate directly, because its DEFAULT ANSWER is the part a
// future caller inherits, and "compatible" is the wrong default for a
// validator. Note getApplicationExpectedMeasurementType() returns
// MEASURE_TEMPERATURE for an out-of-range index rather than erroring
// (application_presets.h), so a fail-open here would silently compare against
// temperature instead of refusing.

// MUTANT TARGET: change either out-of-range `return false` to `return true`.
static void test_out_of_range_indices_fail_closed(void) {
    TEST_ASSERT_FALSE(sensorMatchesApplication(NUM_SENSORS, 1));
    TEST_ASSERT_FALSE(sensorMatchesApplication(1, NUM_APPLICATION_PRESETS));
    TEST_ASSERT_FALSE(sensorMatchesApplication(0xFF, 0xFF));
}

// The other half of the contract: "nothing paired yet" is a real match, not an
// unknown, and must keep passing -- a blanket fail-closed would break every
// caller that legitimately holds an unset sensor or application.
static void test_unset_indices_are_compatible(void) {
    TEST_ASSERT_TRUE(sensorMatchesApplication(0, 1));
    TEST_ASSERT_TRUE(sensorMatchesApplication(1, 0));
    TEST_ASSERT_TRUE(sensorMatchesApplication(0, 0));
}

static void test_json_import_refusal_leaves_earlier_records_intact(void) {
    JsonDocument doc;
    JsonObject good = doc.to<JsonObject>();
    makeInputRecord(good, 0, 4);
    TEST_ASSERT_TRUE(importInputFromJSON(good, 0));
    Input before = *getInputByPin(4);

    g_pinReserved[6] = true;
    JsonDocument doc2;
    JsonObject bad = doc2.to<JsonObject>();
    makeInputRecord(bad, 1, 6);
    TEST_ASSERT_FALSE(importInputFromJSON(bad, 1));

    TEST_ASSERT_NULL(getInputByPin(6));
    TEST_ASSERT_EQUAL_UINT8(1, occupiedSlots());
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, getInputByPin(4), sizeof(Input)));
}

// ---------------------------------------------------------------------------
// #51 (app) — DIVIDER only reaches readLinearSensor(). inputHonorsDivider()
// keys on CAL_LINEAR, which is only correct while the library keeps
// CAL_LINEAR and readLinearSensor paired one-to-one.
// ---------------------------------------------------------------------------

static void test_cal_linear_pairs_with_read_linear_sensor(void) {
    uint8_t linear = 0;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        SensorInfo info;
        loadSensorInfo(getSensorByIndex(i), &info);
        const bool isLinearCal  = info.calibrationType == CAL_LINEAR;
        const bool isLinearRead = info.readFunction == readLinearSensor;
        TEST_ASSERT_EQUAL_MESSAGE(isLinearCal, isLinearRead, info.name);
        if (isLinearCal) linear++;
    }
    TEST_ASSERT_TRUE(linear > 0);
}

static void test_divider_honored_only_by_linear_sensors(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("MPX4250AP")));
    TEST_ASSERT_TRUE(inputHonorsDivider(getInputByPin(4)));

    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("VDO_5BAR_CURVE")));
    TEST_ASSERT_FALSE(inputHonorsDivider(getInputByPin(4)));

    TEST_ASSERT_FALSE(inputHonorsDivider(nullptr));
}

// A custom CAL profile rewrites input->calibrationType but never the read
// function, and CAL PRESET doesn't restore the type — so the predicate must
// follow the sensor, not that field (PR #459 review).
static void test_divider_follows_sensor_not_custom_cal(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("MPX4250AP")));
    Input* in = getInputByPin(4);
    // #460 made a mismatched profile impossible, so the type can't drift...
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalSteinhart(in, 10000.0f, 1.0e-3f, 2.0e-4f, 1.0e-7f));
    TEST_ASSERT_EQUAL_UINT8(CAL_LINEAR, in->calibrationType);
    TEST_ASSERT_TRUE(inputHonorsDivider(in));

    TEST_ASSERT_TRUE(setInputApplication(5, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(5, getSensorIndexByName("VDO_5BAR_CURVE")));
    Input* vdo = getInputByPin(5);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalLinear(vdo, 0.5f, 3.0f, 0.0f, 5.0f));
    // ...and the predicate still follows the sensor even if it did (legacy data).
    vdo->calibrationType = CAL_LINEAR;
    TEST_ASSERT_FALSE(inputHonorsDivider(vdo));
}

// #460: CAL PRESET (clearInputCalibration) restores the sensor's type and
// preset, so the driver's preset branch runs again.
static void test_cal_preset_restores_sensor_type(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("MPX4250AP")));
    Input* in = getInputByPin(4);
    const void* preset = in->presetCalibration;
    in->calibrationType = CAL_THERMISTOR_STEINHART;   // stale, as pre-#460 CLI left it
    in->presetCalibration = nullptr;
    in->flags.useCustomCalibration = true;
    TEST_ASSERT_TRUE(clearInputCalibration(4));
    TEST_ASSERT_EQUAL_UINT8(CAL_LINEAR, in->calibrationType);
    TEST_ASSERT_EQUAL_PTR(preset, in->presetCalibration);
    TEST_ASSERT_FALSE(in->flags.useCustomCalibration);
}

// #460: an override saved with another type than the sensor reads is dropped
// on load instead of being reinterpreted as this type's layout.
static void test_eeprom_load_drops_mismatched_custom_cal(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("MPX4250AP")));
    Input* in = getInputByPin(4);
    in->calibrationType = CAL_THERMISTOR_STEINHART;   // what the old CLI stored
    in->flags.useCustomCalibration = true;
    in->customCalibration.steinhart.bias_resistor = 2200.0f;
    InputEEPROM rec;
    buildInputRecord(*in, rec);
    Input loaded;
    memset(&loaded, 0, sizeof(loaded));
    applyInputRecord(rec, loaded);
    TEST_ASSERT_FALSE(loaded.flags.useCustomCalibration);
    TEST_ASSERT_EQUAL_UINT8(CAL_LINEAR, loaded.calibrationType);
    TEST_ASSERT_NOT_NULL(loaded.presetCalibration);

    // A matching custom survives.
    in->calibrationType = CAL_LINEAR;
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK, applyCalLinear(in, 0.5f, 3.0f, 0.0f, 5.0f));
    buildInputRecord(*in, rec);
    memset(&loaded, 0, sizeof(loaded));
    applyInputRecord(rec, loaded);
    TEST_ASSERT_TRUE(loaded.flags.useCustomCalibration);
}

// ---------------------------------------------------------------------------
// #458 — units must exist and match the input's measurement type.
// ---------------------------------------------------------------------------

static void test_units_strict_lookup(void) {
    TEST_ASSERT_EQUAL_UINT8(UNITS_NOT_FOUND, findUnitsIndexByName("BAAR"));
    TEST_ASSERT_EQUAL_UINT8(UNITS_NOT_FOUND, findUnitsIndexByName(nullptr));
    TEST_ASSERT_EQUAL_UINT8(getUnitsIndexByName("FAHRENHEIT"), findUnitsIndexByName("F"));  // alias
    // The lenient lookup keeps its CELSIUS default for EEPROM/system callers.
    TEST_ASSERT_EQUAL_UINT8(0, getUnitsIndexByName("BAAR"));
}

// Every seed must pass the new check, or SET APPLICATION / an import degrade
// would hand an input units setInputUnits() itself refuses.
static void test_application_default_units_match_their_type(void) {
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* p = getApplicationByIndex(i);
        TEST_ASSERT_TRUE_MESSAGE(
            unitsMatchMeasurement(READ_APP_DEFAULT_UNITS(p), getApplicationExpectedMeasurementType(i)),
            getApplicationNameByIndex(i));
    }
}

static void test_set_units_refuses_unknown_and_wrong_type(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("MPX4250AP")));
    Input* in = getInputByPin(4);
    const uint8_t before = in->unitsIndex;

    TEST_ASSERT_FALSE(setInputUnits(4, UNITS_NOT_FOUND));
    TEST_ASSERT_FALSE(setInputUnits(4, findUnitsIndexByName("F")));   // temperature on pressure
    TEST_ASSERT_EQUAL_UINT8(before, in->unitsIndex);                   // refusal changes nothing

    TEST_ASSERT_TRUE(setInputUnits(4, findUnitsIndexByName("PSI")));
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("PSI"), in->unitsIndex);
}

// The one PERCENT unit is tagged HUMIDITY but is also what PERCENT and
// DIGITAL inputs display in; nothing else crosses families.
static void test_percent_family_shares_the_percent_unit(void) {
    const uint8_t pct = findUnitsIndexByName("PERCENT");
    TEST_ASSERT_NOT_EQUAL(UNITS_NOT_FOUND, pct);
    TEST_ASSERT_TRUE(unitsMatchMeasurement(pct, MEASURE_HUMIDITY));
    TEST_ASSERT_TRUE(unitsMatchMeasurement(pct, MEASURE_PERCENT));
    TEST_ASSERT_TRUE(unitsMatchMeasurement(pct, MEASURE_DIGITAL));
    TEST_ASSERT_FALSE(unitsMatchMeasurement(pct, MEASURE_PRESSURE));
    TEST_ASSERT_FALSE(unitsMatchMeasurement(findUnitsIndexByName("VOLTS"), MEASURE_PERCENT));
}

static void test_json_import_degrades_bad_units_to_app_default(void) {
    const uint8_t chtDefault = READ_APP_DEFAULT_UNITS(getApplicationByIndex(appCHT()));
    const char* bad[] = {"BAAR", "PSI"};  // unknown; wrong type for a temperature input
    for (uint8_t k = 0; k < 2; k++) {
        setUp();
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        makeInputRecord(o, 0, 4);
        o["units"] = bad[k];
        TEST_ASSERT_TRUE_MESSAGE(importInputFromJSON(o, 0), bad[k]);   // degrade, never reject
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(chtDefault, getInputByPin(4)->unitsIndex, bad[k]);
    }
}

// A record without `units` degrades like a bad one instead of failing.
static void test_json_import_missing_units_uses_app_default(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    makeInputRecord(o, 0, 4);
    o.remove("units");
    TEST_ASSERT_TRUE(importInputFromJSON(o, 0));
    TEST_ASSERT_EQUAL_UINT8(READ_APP_DEFAULT_UNITS(getApplicationByIndex(appCHT())), getInputByPin(4)->unitsIndex);
}

// Units saved before #458 could be of another quantity; loading repairs them.
static void test_eeprom_load_repairs_wrong_type_units(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appCHT()));
    Input* in = getInputByPin(4);
    in->unitsIndex = findUnitsIndexByName("PSI");   // what the old SET UNITS allowed
    InputEEPROM rec;
    buildInputRecord(*in, rec);
    Input loaded;
    memset(&loaded, 0, sizeof(loaded));
    applyInputRecord(rec, loaded);
    TEST_ASSERT_EQUAL_UINT8(READ_APP_DEFAULT_UNITS(getApplicationByIndex(appCHT())), loaded.unitsIndex);

    // A valid choice survives the round trip untouched.
    in->unitsIndex = findUnitsIndexByName("F");
    buildInputRecord(*in, rec);
    memset(&loaded, 0, sizeof(loaded));
    applyInputRecord(rec, loaded);
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("F"), loaded.unitsIndex);
}

// #460: every typed profile is refused on an input whose sensor reads another
// type (one mutant per applyCal*() — the review found only 2 of 8 guarded).
// Arguments are all valid, so SENSOR_TYPE is the only possible refusal, and
// the input must be left exactly as it was.
static void test_every_typed_profile_refuses_a_wrong_sensor(void) {
    TEST_ASSERT_TRUE(setInputApplication(4, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(4, getSensorIndexByName("VDO_5BAR_CURVE")));   // CAL_PRESSURE_POLYNOMIAL
    Input* poly = getInputByPin(4);
    TEST_ASSERT_TRUE(setInputApplication(5, appOIL_P()));
    TEST_ASSERT_TRUE(setInputSensor(5, getSensorIndexByName("MPX4250AP")));        // CAL_LINEAR
    Input* lin = getInputByPin(5);
    const Input polyBefore = *poly, linBefore = *lin;

    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalSteinhart(lin, 10000.0f, 1.1e-3f, 2.3e-4f, 8.7e-8f));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalBeta(lin, 10000.0f, 3950.0f, 10000.0f, 25.0f));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalPressurePoly(lin, 220.0f, 1.0f, 2.0f, 3.0f));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalLinear(poly, 0.5f, 3.0f, 0.0f, 5.0f));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalRpm(lin, 12, 3.0f, 1.0f, 2000, 100, 8000));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalVoltageDivider(lin, 100000.0f, 22000.0f, 1.0f, 0.0f));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalSpeed(lin, 100, 2008, 3.73f, 1.0f, 2000, 300));
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_ERR_SENSOR_TYPE, applyCalCanImport(lin, 0x7E8, 0x0C, 0, 2, true, 0.25f, 0.0f, 2000));

    TEST_ASSERT_EQUAL_INT(0, memcmp(&polyBefore, poly, sizeof(Input)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&linBefore, lin, sizeof(Input)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_available);
    RUN_TEST(test_accepts_a_valid_new_input);
    RUN_TEST(test_refused_new_input_claims_no_slot);
    RUN_TEST(test_refused_existing_input_is_byte_identical);
    RUN_TEST(test_refused_existing_input_stays_disabled);
    RUN_TEST(test_refused_on_pin_conflict_leaves_target_intact);
    RUN_TEST(test_refused_bad_application_claims_no_slot);
    RUN_TEST(test_successful_reapplication_updates_existing_input);
    RUN_TEST(test_two_step_refuses_measurement_type_mismatch);
    RUN_TEST(test_two_step_accepts_matching_measurement_type);
    RUN_TEST(test_json_import_accepts_a_valid_record);
    RUN_TEST(test_json_import_refuses_measurement_type_mismatch);
    RUN_TEST(test_json_import_refuses_a_bus_reserved_pin);
    RUN_TEST(test_json_import_refusal_leaves_earlier_records_intact);
    RUN_TEST(test_out_of_range_indices_fail_closed);
    RUN_TEST(test_unset_indices_are_compatible);
    RUN_TEST(test_cal_linear_pairs_with_read_linear_sensor);
    RUN_TEST(test_divider_honored_only_by_linear_sensors);
    RUN_TEST(test_divider_follows_sensor_not_custom_cal);
    RUN_TEST(test_cal_preset_restores_sensor_type);
    RUN_TEST(test_every_typed_profile_refuses_a_wrong_sensor);
    RUN_TEST(test_eeprom_load_drops_mismatched_custom_cal);
    RUN_TEST(test_units_strict_lookup);
    RUN_TEST(test_application_default_units_match_their_type);
    RUN_TEST(test_set_units_refuses_unknown_and_wrong_type);
    RUN_TEST(test_percent_family_shares_the_percent_unit);
    RUN_TEST(test_json_import_degrades_bad_units_to_app_default);
    RUN_TEST(test_json_import_missing_units_uses_app_default);
    RUN_TEST(test_eeprom_load_repairs_wrong_type_units);
    return UNITY_END();
}
