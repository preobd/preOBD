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
// it reads (APPLICATION_PRESETS / SENSOR_LIBRARY) are header-resident PROGMEM
// tables that the stub turns into plain RAM. What it needs from the rest of the
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

#include "../../src/inputs/input_manager.cpp"
#include "../../src/inputs/calibration_apply.cpp"
#include "../../src/lib/json_config.cpp"

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
    RUN_TEST(test_json_import_accepts_a_valid_record);
    RUN_TEST(test_json_import_refuses_a_bus_reserved_pin);
    RUN_TEST(test_json_import_refusal_leaves_earlier_records_intact);
    return UNITY_END();
}
