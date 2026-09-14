// Native unit test: custom calibration survives SYSTEM DUMP JSON -> JSON IMPORT
// (#373).
//
// importCalibration() used to parse nothing and return true, and its caller
// dropped the return value, so a profile Save/Load silently put every
// hand-calibrated sender back on its preset curve and reported success.
//
// Each round-trip test below builds an input, applies a custom calibration
// through the shared validating core (src/inputs/calibration_apply.cpp — the same
// code path `SET <pin> CAL ...` uses), exports the record exactly as SYSTEM DUMP
// JSON does, serialises it to text, wipes the input table, imports the text back,
// and asserts the override union and flag came back byte-identical.
//
// Host scaffolding is shared with test_input_manager (test/stubs/firmware_stubs.h).
#include <unity.h>
#include <Arduino.h>
#include <string.h>

// SENSOR_LIBRARY / UNITS_REGISTRY / APPLICATION_PRESETS are now defined in
// their own .cpp (#400), not inline in the header — pull the data in.
// SENSOR_CATEGORIES too (missed by #400, fixed separately) — pull it in.
#include "../../src/lib/sensor_library.cpp"
#include "../../src/lib/sensor_library/sensor_categories.cpp"
#include "../../src/lib/units_registry.cpp"
#include "../../src/lib/application_presets.cpp"
#include "../../src/inputs/input_manager.cpp"
#include "../../src/inputs/calibration_apply.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/lib/json_config.cpp"

#include "../stubs/firmware_stubs.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

static const uint8_t TEST_PIN = 4;

static void resetInputs() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        memset(&inputs[i], 0, sizeof(Input));
        inputs[i].pin = 0xFF;
    }
    numActiveInputs = 0;
}

void setUp(void) {
    memset(g_pinReserved, 0, sizeof(g_pinReserved));
    resetInputs();
    // See test_input_manager's setUp(): this suite also drives inputs[]
    // directly, so inputHoldsPin() (#385) needs to be told the table is live.
    inputsTableIsLive = true;
}
void tearDown(void) {}

static uint8_t appCHT() { return getApplicationIndexByName("CHT"); }

// First application in the registry that expects `want` (#383: setInputSensor()
// now refuses a sensor whose measurement type doesn't match the input's
// application, so a calibration round trip has to pick a sensor/application
// pair that actually agrees rather than hard-coding CHT for everything).
static uint8_t appForMeasurementType(MeasurementType want) {
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        if (getApplicationExpectedMeasurementType(i) == want) return i;
    }
    return 0;
}

static uint8_t occupiedSlots() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) if (inputs[i].pin != 0xFF) n++;
    return n;
}

// The record-level contract: a refused record claimed no slot, enabled nothing,
// and installed no read function anywhere.
static void assertRecordLeftNothing(uint8_t pin) {
    TEST_ASSERT_NULL_MESSAGE(getInputByPin(pin), "refused record left an input on the pin");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, occupiedSlots(), "refused record claimed a slot");
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        TEST_ASSERT_FALSE(inputs[i].flags.isEnabled);
        TEST_ASSERT_NULL(inputs[i].readFunction);
    }
}

// First sensor in the library whose preset carries the wanted calibration type.
// Looking it up beats hard-coding a sensor name that may be renamed later.
static uint8_t sensorIndexForCal(CalibrationType wanted) {
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* flash = getSensorByIndex(i);
        if (flash == nullptr) continue;
        SensorInfo info;
        loadSensorInfo(flash, &info);
        if (info.calibrationType == wanted) return i;
    }
    return 0;
}

// A configured input on TEST_PIN whose sensor uses `type`, with a zeroed
// override (so a byte-comparison after import is meaningful). The application
// is picked to match the chosen sensor's measurement type (#383) rather than
// hard-coded, so setInputSensor()'s new compatibility gate doesn't refuse the
// very fixture every round-trip test is built on.
static Input* buildInput(CalibrationType type) {
    uint8_t sensorIdx = sensorIndexForCal(type);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, sensorIdx, "no sensor in the library uses this calibration type");
    MeasurementType measType = getSensorMeasurementType(sensorIdx);
    uint8_t appIdx = appForMeasurementType(measType);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, appIdx, "no application matches this sensor's measurement type");
    TEST_ASSERT_TRUE(setInputApplication(TEST_PIN, appIdx));
    TEST_ASSERT_TRUE(setInputSensor(TEST_PIN, sensorIdx));
    Input* in = getInputByPin(TEST_PIN);
    TEST_ASSERT_NOT_NULL(in);
    memset(&in->customCalibration, 0, sizeof(CalibrationOverride));
    in->flags.useCustomCalibration = false;
    return in;
}

// Export the input the way SYSTEM DUMP JSON does, through real JSON text.
static void exportToText(const Input* in, char* buf, size_t bufLen) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    exportInputToJSON(obj, in);
    size_t n = serializeJson(doc, buf, bufLen);
    TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < bufLen - 1, "export did not fit the buffer");
}

// Wipe the input table and import the record back into slot 0.
static bool importText(const char* json) {
    resetInputs();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_EQUAL_INT(DeserializationError::Ok, err.code());
    JsonObject obj = doc.as<JsonObject>();
    return importInputFromJSON(obj, 0);
}

// The whole contract in one helper: export -> text -> import -> byte-compare.
static void assertRoundTrips(const Input* src) {
    Input expected = *src;
    char json[3072];
    exportToText(src, json, sizeof(json));

    TEST_ASSERT_TRUE_MESSAGE(importText(json), "import rejected the record it had just exported");

    Input* got = getInputByPin(expected.pin);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_TRUE_MESSAGE(got->flags.useCustomCalibration,
                             "custom calibration flag was lost in the round trip");
    TEST_ASSERT_EQUAL_UINT8(expected.calibrationType, got->calibrationType);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, memcmp(&expected.customCalibration, &got->customCalibration,
                  sizeof(CalibrationOverride)),
        "customCalibration union differs after the round trip");
}

// ---------------------------------------------------------------------------
// Sanity
// ---------------------------------------------------------------------------

static void test_registry_available(void) {
    TEST_ASSERT_NOT_EQUAL(0, appCHT());
    TEST_ASSERT_NOT_EQUAL(0, sensorIndexForCal(CAL_THERMISTOR_STEINHART));
}

// ---------------------------------------------------------------------------
// One round trip per calibration type exportCalibration() emits with params.
// ---------------------------------------------------------------------------

static void test_roundtrip_steinhart(void) {
    Input* in = buildInput(CAL_THERMISTOR_STEINHART);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalSteinhart(in, 2200.0f, 0.001129f, 0.0002341f, 8.775e-8f));
    assertRoundTrips(in);
}

static void test_roundtrip_beta(void) {
    Input* in = buildInput(CAL_THERMISTOR_BETA);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalBeta(in, 10000.0f, 3950.0f, 10000.0f, 25.0f));
    assertRoundTrips(in);
}

static void test_roundtrip_pressure_polynomial(void) {
    Input* in = buildInput(CAL_PRESSURE_POLYNOMIAL);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalPressurePoly(in, 184.0f, -6.75e-4f, 2.54e-6f, 1.87e-9f));
    assertRoundTrips(in);
}

static void test_roundtrip_linear(void) {
    Input* in = buildInput(CAL_LINEAR);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalLinear(in, 0.5f, 3.0f, 0.25f, 6.75f));
    assertRoundTrips(in);
}

static void test_roundtrip_voltage_divider(void) {
    Input* in = buildInput(CAL_VOLTAGE_DIVIDER);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalVoltageDivider(in, 47000.0f, 10000.0f, 1.021f, -0.05f));
    assertRoundTrips(in);
}

static void test_roundtrip_rpm(void) {
    Input* in = buildInput(CAL_RPM);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalRpm(in, 12, 3.25f, 1.02f, 2500, 250, 7500));
    assertRoundTrips(in);
}

// #382: CAL_SPEED used to fall to getCalibrationType()'s "UNKNOWN" default and
// exportCalibration()'s no-params default, so this round trip lost the input
// entirely (importCalibration() refused the resulting self-contradictory
// "source":"CUSTOM","type":"UNKNOWN" record).
//
// MUTANT TARGET: reverting either the CAL_SPEED case in getCalibrationType()/
// exportCalibration() (json_config.cpp) or the SPEED branch in
// importCalibration() must fail this test.
static void test_roundtrip_speed(void) {
    Input* in = buildInput(CAL_SPEED);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalSpeed(in, 100, 2008, 3.73f, 1.05f, 2000, 300));
    assertRoundTrips(in);
}

// Same bug, CAL_CAN_IMPORT. Built directly (not via buildInput()'s
// sensorIndexForCal lookup): the CAN_IMPORT sensor library entry is gated by
// INCLUDE_CAN (sensor_library.h), which the native test profile does not set,
// so no sensor in this test binary's library carries CAL_CAN_IMPORT as its
// OWN type. applyCalCanImport() doesn't care what sensor the input started
// on -- like every other applyCal*(), it sets calibrationType itself -- so
// any already-admitted input is a fine host for the round trip.
static void test_roundtrip_can_import(void) {
    TEST_ASSERT_TRUE(setInputApplication(TEST_PIN, appCHT()));
    Input* in = getInputByPin(TEST_PIN);
    memset(&in->customCalibration, 0, sizeof(CalibrationOverride));
    in->flags.useCustomCalibration = false;
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalCanImport(in, 0x7E8, 0x0C, 0, 2, true, 0.25f, 0.0f, 2000));
    assertRoundTrips(in);
}

// Table sensors keep their curve in flash; the bias resistor is the one
// overridable field, so that is the whole round trip for this type.
static void test_roundtrip_thermistor_table_bias(void) {
    Input* in = buildInput(CAL_THERMISTOR_TABLE);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK, applyCalBias(in, 1500.0f));
    assertRoundTrips(in);
}

// #382: CAL_LEVEL_TABLE's bias resistor is inert -- like CAL_PRESSURE_TABLE,
// its read path (sensors/level/table.cpp) takes the bias from the PROGMEM
// preset unconditionally, so applyCalBias() refuses to set one (there is no
// CLI/JSON path that can normally get an input into useCustomCalibration=true
// on this type). exportCalibration() must still defend against it -- e.g. a
// legacy/corrupt EEPROM image with the flag set -- by writing "source":
// "PRESET" rather than a CUSTOM block importCalibration() would refuse anyway
// (the exact self-contradictory, unimportable record #382 is about).
//
// MUTANT TARGET: removing the `calibrationType == CAL_LEVEL_TABLE` early
// return in exportCalibration() (json_config.cpp) must fail this test.
static void test_level_table_export_forces_preset_source(void) {
    Input* in = buildInput(CAL_LEVEL_TABLE);
    in->flags.useCustomCalibration = true;  // forced: no setter reaches this state

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    exportInputToJSON(obj, in);

    JsonObject cal = obj["calibration"];
    TEST_ASSERT_FALSE(cal.isNull());
    TEST_ASSERT_EQUAL_STRING("PRESET", (const char*)cal["source"]);
    TEST_ASSERT_TRUE(cal["params"].isNull());
}

// ---------------------------------------------------------------------------
// source: "PRESET" must actively clear a custom override rather than assume the
// setters upstream did it.
//
// Note the exporter only writes a "calibration" block for an input that IS on a
// custom calibration, so a PRESET block reaches import from hand-written or
// third-party JSON — which is exactly the input that must not be trusted.
// ---------------------------------------------------------------------------

static void test_preset_source_clears_custom_calibration(void) {
    // Slot already carrying a custom override.
    TEST_ASSERT_TRUE(setInputApplication(TEST_PIN, appCHT()));
    Input* victim = getInputByPin(TEST_PIN);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalSteinhart(victim, 2200.0f, 0.001129f, 0.0002341f, 8.775e-8f));

    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["pin"] = TEST_PIN;
    o["app"] = "CHT";
    o["sensor"] = "MAX6675";
    o["units"] = "C";
    JsonObject cal = o["calibration"].to<JsonObject>();
    cal["type"] = "THERMISTOR_STEINHART";
    cal["source"] = "PRESET";

    TEST_ASSERT_TRUE(importInputFromJSON(o, 0));

    Input* got = getInputByPin(TEST_PIN);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_FALSE(got->flags.useCustomCalibration);
    for (uint8_t i = 0; i < sizeof(CalibrationOverride); i++) {
        TEST_ASSERT_EQUAL_UINT8(0, got->customCalibration.raw[i]);
    }
}

// ---------------------------------------------------------------------------
// Malformed / rejected calibrations
// ---------------------------------------------------------------------------

// A calibration object with no "source" key used to reach strcmp(nullptr, ...)
// and fault the board. It must be refused instead.
static void test_missing_source_is_refused_not_dereferenced(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["pin"] = TEST_PIN;
    o["app"] = "CHT";
    o["sensor"] = "MAX6675";
    o["units"] = "C";
    JsonObject cal = o["calibration"].to<JsonObject>();
    cal["type"] = "THERMISTOR_STEINHART";   // no "source"

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));
    assertRecordLeftNothing(TEST_PIN);
}

// A record whose calibration is rejected by the validating core must fail the
// record — the same contract #363 gave a rejected application — instead of
// importing an input that silently reverted to the preset curve.
static void test_rejected_calibration_fails_the_record(void) {
    Input* in = buildInput(CAL_THERMISTOR_STEINHART);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalSteinhart(in, 2200.0f, 0.001129f, 0.0002341f, 8.775e-8f));
    char json[3072];
    exportToText(in, json, sizeof(json));

    // Corrupt the exported bias to a value the CLI would refuse.
    resetInputs();
    JsonDocument doc;
    TEST_ASSERT_EQUAL_INT(DeserializationError::Ok, deserializeJson(doc, json).code());
    JsonObject obj = doc.as<JsonObject>();
    obj["calibration"]["params"]["biasResistor"] = 0.0f;

    TEST_ASSERT_FALSE(importInputFromJSON(obj, 0));

    // ...and leaves NOTHING behind. The calibration is judged before the first
    // write, so the record that failed never claimed a slot: it must not appear
    // in LIST INPUTS, and above all must not be sitting there enabled with a
    // live readFunction while the summary reports "0 of 1 inputs imported".
    assertRecordLeftNothing(TEST_PIN);
}

// The same contract for a record that would have MODIFIED an existing input:
// a refused calibration must leave that input byte-for-byte as it was, not
// half-rewritten with the record's application, sensor, units and flags.
static void test_rejected_calibration_leaves_existing_input_untouched(void) {
    // An input the operator already configured and deliberately disabled.
    Input* in = buildInput(CAL_THERMISTOR_STEINHART);
    TEST_ASSERT_EQUAL_UINT8(CAL_APPLY_OK,
        applyCalSteinhart(in, 3300.0f, 0.0011f, 0.00023f, 9.0e-8f));
    in->flags.isEnabled = false;
    in->unitsIndex = 3;
    in->obd2pid = 0x5C;
    Input before = *in;
    uint8_t activeBefore = numActiveInputs;

    // A record for the same pin whose calibration this firmware refuses.
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["pin"] = TEST_PIN;
    o["app"] = "CHT";
    o["sensor"] = "MAX6675";
    o["units"] = "C";
    JsonObject cal = o["calibration"].to<JsonObject>();
    cal["type"] = "THERMISTOR_STEINHART";
    cal["source"] = "CUSTOM";
    cal["params"]["biasResistor"] = 0.0f;    // refused: bias must be > 0
    cal["params"]["steinhartA"] = 0.001129f;
    cal["params"]["steinhartB"] = 0.0002341f;
    cal["params"]["steinhartC"] = 8.775e-8f;

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));

    Input* after = getInputByPin(TEST_PIN);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(&before, after, sizeof(Input)),
                                  "refused record modified the existing input");
    TEST_ASSERT_EQUAL_UINT8(activeBefore, numActiveInputs);
}

// A CUSTOM calibration with no params block is malformed and must be refused
// (the old `if (!calObj["params"].isNull() == false)` collapsed to plain
// isNull(), and the caller dropped the result anyway).
static void test_custom_without_params_is_refused(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["pin"] = TEST_PIN;
    o["app"] = "CHT";
    o["sensor"] = "MAX6675";
    o["units"] = "C";
    JsonObject cal = o["calibration"].to<JsonObject>();
    cal["type"] = "THERMISTOR_STEINHART";
    cal["source"] = "CUSTOM";

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));
    assertRecordLeftNothing(TEST_PIN);
}

// PRESSURE_TABLE is the one exported type that cannot be re-applied: its read
// path takes the bias resistor from the PROGMEM preset unconditionally, so an
// override is inert, and the CLI refuses to set one. Refuse loudly rather than
// accept a value that would do nothing.
// ---------------------------------------------------------------------------
// JSON import must not launder a platform default into a stored SELECTION
// (#377). `buses["i2c"] | DEFAULT_I2C_BUS` used to write bus 0 whenever the key
// was absent, so importing a profile that says nothing about I2C left
// active_i2c == 0 — which every later boot reads as "the user picked Wire" and
// claims 18/19 for good. That is the exact guess #377 deletes, re-entering
// through the mobile app's profile flow.
// ---------------------------------------------------------------------------

// MUTANT TARGET: restore `| DEFAULT_I2C_BUS` / `| DEFAULT_SPI_BUS` in either
// branch of the buses block and these must fail.
static void test_import_without_buses_object_selects_no_bus(void) {
    systemConfig.buses.active_i2c = 1;   // stale non-default values, to prove
    systemConfig.buses.active_spi = 2;   // the import actually writes these
    JsonDocument doc;
    JsonObject sys = doc.to<JsonObject>();   // an old config: no "buses" at all
    importSystemConfigFromJSON(sys);
    TEST_ASSERT_EQUAL_UINT8(0xFF, systemConfig.buses.active_i2c);
    TEST_ASSERT_EQUAL_UINT8(0xFF, systemConfig.buses.active_spi);
}

static void test_import_with_absent_bus_keys_selects_no_bus(void) {
    systemConfig.buses.active_i2c = 1;
    systemConfig.buses.active_spi = 2;
    JsonDocument doc;
    JsonObject sys = doc.to<JsonObject>();
    JsonObject buses = sys["buses"].to<JsonObject>();
    buses["i2cClock"] = 400;             // a "buses" object that names no bus
    importSystemConfigFromJSON(sys);
    TEST_ASSERT_EQUAL_UINT8(0xFF, systemConfig.buses.active_i2c);
    TEST_ASSERT_EQUAL_UINT8(0xFF, systemConfig.buses.active_spi);
}

// The other half of the contract: a config that DOES state a selection is a
// user statement and must survive the round trip untouched.
static void test_import_preserves_an_explicit_bus_selection(void) {
    JsonDocument doc;
    JsonObject sys = doc.to<JsonObject>();
    JsonObject buses = sys["buses"].to<JsonObject>();
    buses["i2c"] = 2;
    buses["spi"] = 1;
    importSystemConfigFromJSON(sys);
    TEST_ASSERT_EQUAL_UINT8(2, systemConfig.buses.active_i2c);
    TEST_ASSERT_EQUAL_UINT8(1, systemConfig.buses.active_spi);
}

// ---------------------------------------------------------------------------
// #382 — importSystemConfigFromJSON()'s admission/commit split.
//
// A "display" object present without a "type" key used to make
// `const char* displayType = display["type"]` come back nullptr, and
// strcmp(nullptr, "LCD") is undefined behaviour. Guard it and prove the
// current display type survives instead of being force-coerced.
//
// MUTANT TARGET: dropping the `display["type"].isNull() == false` guard
// (json_config.cpp) must fail this test (and, on a real board, crash).
static void test_import_display_object_without_type_preserves_current_type(void) {
    systemConfig.displayType = DISPLAY_OLED;
    JsonDocument doc;
    JsonObject sys = doc.to<JsonObject>();
    JsonObject display = sys["display"].to<JsonObject>();
    display["enabled"] = true;   // "display" present, but no "type" key

    TEST_ASSERT_TRUE(importSystemConfigFromJSON(sys));
    TEST_ASSERT_EQUAL_UINT8(DISPLAY_OLED, systemConfig.displayType);
    TEST_ASSERT_TRUE(systemConfig.displayEnabled);
}

// A "timing" object present with only ONE of sensorRead/alarmCheck used to
// coerce the other, absent key to 0 -- zeroing an interval a config never
// mentioned, rather than leaving it alone.
//
// MUTANT TARGET: reverting either timing field back to an unguarded
// `staged.X = timing["X"];` must fail this test.
static void test_import_timing_partial_keys_preserve_the_other(void) {
    systemConfig.sensorReadInterval = 250;
    systemConfig.alarmCheckInterval = 500;
    JsonDocument doc;
    JsonObject sys = doc.to<JsonObject>();
    JsonObject timing = sys["timing"].to<JsonObject>();
    timing["sensorRead"] = 100;   // states sensorRead only

    TEST_ASSERT_TRUE(importSystemConfigFromJSON(sys));
    TEST_ASSERT_EQUAL_UINT16(100, systemConfig.sensorReadInterval);
    TEST_ASSERT_EQUAL_UINT16(500, systemConfig.alarmCheckInterval);  // untouched
}

static void test_pressure_table_custom_is_refused(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["pin"] = TEST_PIN;
    // Pressure-measuring application (#383: an app/sensor measurement-type
    // mismatch would refuse this record for the WRONG reason and mask the
    // PRESSURE_TABLE-specific refusal this test targets).
    o["app"] = "OIL_PRESSURE";
    o["sensor"] = (const char*)getSensorNameByIndex(sensorIndexForCal(CAL_PRESSURE_TABLE));
    o["units"] = "BAR";
    JsonObject cal = o["calibration"].to<JsonObject>();
    cal["type"] = "PRESSURE_TABLE";
    cal["source"] = "CUSTOM";
    cal["params"]["biasResistor"] = 1500.0f;

    TEST_ASSERT_FALSE(importInputFromJSON(o, 0));
    assertRecordLeftNothing(TEST_PIN);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_available);
    RUN_TEST(test_roundtrip_steinhart);
    RUN_TEST(test_roundtrip_beta);
    RUN_TEST(test_roundtrip_pressure_polynomial);
    RUN_TEST(test_roundtrip_linear);
    RUN_TEST(test_roundtrip_voltage_divider);
    RUN_TEST(test_roundtrip_rpm);
    RUN_TEST(test_roundtrip_speed);
    RUN_TEST(test_roundtrip_can_import);
    RUN_TEST(test_roundtrip_thermistor_table_bias);
    RUN_TEST(test_level_table_export_forces_preset_source);
    RUN_TEST(test_preset_source_clears_custom_calibration);
    RUN_TEST(test_missing_source_is_refused_not_dereferenced);
    RUN_TEST(test_rejected_calibration_fails_the_record);
    RUN_TEST(test_rejected_calibration_leaves_existing_input_untouched);
    RUN_TEST(test_custom_without_params_is_refused);
    RUN_TEST(test_pressure_table_custom_is_refused);
    RUN_TEST(test_import_without_buses_object_selects_no_bus);
    RUN_TEST(test_import_with_absent_bus_keys_selects_no_bus);
    RUN_TEST(test_import_preserves_an_explicit_bus_selection);
    RUN_TEST(test_import_display_object_without_type_preserves_current_type);
    RUN_TEST(test_import_timing_partial_keys_preserve_the_other);
    return UNITY_END();
}
