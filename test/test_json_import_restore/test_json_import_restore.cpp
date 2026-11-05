// Native unit test: JSON IMPORT restores a SYSTEM DUMP JSON (#561, #562).
//
// #562: an "inputs" array used to MERGE into the input table, so restoring a
// backup kept every input added since, and a pin the file carried at another
// idx could end up live in two slots. It now replaces the table, as "rules"
// and "actions" already did.
// #561: system.logFilter was exported and never read back. (system.serial is
// imported too, but the native platform has no serial ports: bench-tested.)
// #567: a file must fit this board -- every physical pin it uses exists here,
// and an input it labels A<n> is on this board's A<n> -- or nothing changes.
//
// Host scaffolding is shared with test_json_calibration (test/stubs/firmware_stubs.h).
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/input_manager.cpp"
#include "../../src/storage/input_store.cpp"
#include "../../src/inputs/calibration_apply.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"
#include "../../src/sensors/can_sensor_library/obd_output_pids.cpp"
#include "../../src/pinctrl/pin_label.cpp"
#include "../../src/storage/json_config.cpp"

#include "../stubs/firmware_stubs.h"

// Control engine and config generation: not in this binary. The import calls
// them on a rules/actions array, and since #562 on an inputs array (re-arm).
void configGenerationBump() {}
// obd_query.cpp is not in this binary; count the rebuilds instead (#562).
static int g_obdTableBuilds = 0;
void obdQuery_buildLookupTable() { g_obdTableBuilds++; }
#if INCLUDE_RULES_ENGINE
Action actions[MAX_ACTIONS];
ActionLink actionLinks[MAX_ACTION_LINKS];
static int g_rulesInitCalls = 0;
bool rulesImportRule(JsonObjectConst, Rule&) { return false; }
bool rulesImportAction(JsonObjectConst, Action&, uint8_t*, uint8_t, uint8_t*) { return false; }
bool rulesActionAddRule(uint8_t, uint8_t, bool*) { return false; }
uint8_t rulesSweepActionRules() { return 0; }
void rulesManagerInit(uint32_t) { g_rulesInitCalls++; }
void rulesActionsInit() {}
uint8_t rulesSyncActionPins() { return 0; }
#endif

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

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
    inputsTableIsLive = true;
    g_applyLogFilterCalls = 0;
    g_stubDigitalPinMax = 0xFF;
    g_stubAnalogFirst = 0xFF;
    g_stubAnalogCount = 0;
    g_obdTableBuilds = 0;
#if INCLUDE_RULES_ENGINE
    g_rulesInitCalls = 0;
#endif
    systemConfig.logFilter.control_level = 3;
    systemConfig.logFilter.data_level = 3;
    systemConfig.logFilter.debug_level = 3;
    systemConfig.logFilter.enabledTags = 0xFFFFFFFF;
}
void tearDown(void) {}

static uint8_t slotsHoldingPin(uint8_t pin) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) if (inputs[i].pin == pin) n++;
    return n;
}

static uint8_t occupiedSlots() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) if (inputs[i].pin != 0xFF) n++;
    return n;
}

// A configured input on `pin`: the first application whose measurement type
// some sensor reads, and that sensor (#383 refuses a mismatched pair).
static void configure(uint8_t pin) {
    for (uint8_t s = 1; s < NUM_SENSORS; s++) {
        const MeasurementType mt = getSensorMeasurementType(s);
        for (uint8_t a = 1; a < NUM_APPLICATION_PRESETS; a++) {
            if (getApplicationExpectedMeasurementType(a) != mt) continue;
            if (setInputApplication(pin, a) && setInputSensor(pin, s)) return;
            clearInput(pin);
        }
    }
    TEST_FAIL_MESSAGE("no application/sensor pair could be configured");
}

// A full runtime document whose inputs are the given pins, exported the way
// SYSTEM DUMP JSON writes them, at idx 0..n-1.
static void buildDoc(const uint8_t* pins, uint8_t n, char* out, size_t outLen) {
    JsonDocument doc;
    doc["schemaVersion"] = 1;
    doc["mode"] = "runtime";
    JsonArray arr = doc["inputs"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++) {
        const Input* in = getInputByPin(pins[i]);
        TEST_ASSERT_NOT_NULL(in);
        JsonObject o = arr.add<JsonObject>();
        exportInputToJSON(o, in);
        o["idx"] = i;
    }
    TEST_ASSERT_TRUE(serializeJson(doc, out, outLen) < outLen - 1);
}

// ---------------------------------------------------------------------------
// #562: inputs are replaced, not merged
// ---------------------------------------------------------------------------

static void test_inputs_absent_from_the_file_are_removed(void) {
    configure(4);
    const uint8_t keep[1] = {4};
    static char json[4096];
    buildDoc(keep, 1, json, sizeof(json));

    configure(5);                          // added after the backup was taken
    TEST_ASSERT_EQUAL_UINT8(2, occupiedSlots());

    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_NULL_MESSAGE(getInputByPin(5), "an input missing from the file survived the import");
    TEST_ASSERT_EQUAL_UINT8(1, occupiedSlots());
}

// The device holds pin 4 at slot 1; the file has it at idx 0. It must end up
// in exactly one slot (updated in place), with pin 5 removed.
static void test_a_pin_at_another_idx_is_not_duplicated(void) {
    configure(5);
    configure(4);                          // slot 1
    const uint8_t order[1] = {4};
    static char json[4096];
    buildDoc(order, 1, json, sizeof(json));   // idx 0

    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_UINT8(1, slotsHoldingPin(4));
    TEST_ASSERT_EQUAL_UINT8(1, occupiedSlots());
}

// A file whose every record is refused must not empty the table: the import
// reports failure and the inputs that were there stay (review of #562).
static void test_a_file_whose_records_all_fail_leaves_the_inputs_alone(void) {
    configure(4);
    configure(5);
    TEST_ASSERT_FALSE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"inputs\":"
        "[{\"idx\":0,\"pin\":6,\"app\":\"NO_SUCH_APP\",\"sensor\":\"NO_SUCH_SENSOR\"}]}"));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_NOT_NULL(getInputByPin(5));
    TEST_ASSERT_EQUAL_UINT8(2, occupiedSlots());
}

// The OBD-II PID table points into inputs[]; an inputs import rebuilds it.
static void test_an_inputs_import_rebuilds_the_pid_table(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_INT(1, g_obdTableBuilds);
}

static void test_an_empty_inputs_array_clears_the_table_and_succeeds(void) {
    configure(4);
    configure(5);
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
}

// A document with no "inputs" key says nothing about inputs: they stay.
// Replacing the inputs can orphan a rule, so the engine is re-armed, which is
// where an orphan gets its WARNING (rulesManagerInit).
static void test_replacing_inputs_re_arms_the_rules(void) {
#if INCLUDE_RULES_ENGINE
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_INT(1, g_rulesInitCalls);
#endif
}

static void test_a_document_without_inputs_leaves_them_alone(void) {
    configure(4);
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":{}}"));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
}

// ---------------------------------------------------------------------------
// #574: seaLevelPressure is range-checked like SYSTEM SEA_LEVEL
// ---------------------------------------------------------------------------

static void test_sea_level_pressure_is_imported(void) {
    systemConfig.seaLevelPressure = 1013.25f;
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":"
        "{\"constants\":{\"seaLevelPressure\":1025.5}}}"));
    TEST_ASSERT_EQUAL_FLOAT(1025.5f, systemConfig.seaLevelPressure);
}

// 0 is what a pre-#574 `SYSTEM SEA_LEVEL abc` saved and exported.
static void test_an_out_of_range_sea_level_pressure_keeps_the_current_one(void) {
    const char* bad[] = {"0", "849.9", "1100.1", "-1013", "\"abc\""};
    for (const char* v : bad) {
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":"
                 "{\"constants\":{\"seaLevelPressure\":%s}}}", v);
        systemConfig.seaLevelPressure = 1013.25f;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1013.25f, systemConfig.seaLevelPressure, v);
    }
}

// ---------------------------------------------------------------------------
// #590: display.address is a PCF8574/PCF8574A address, like DISPLAY ADDRESS
// ---------------------------------------------------------------------------

static void test_an_lcd_address_in_either_backpack_range_is_imported(void) {
    const char* good[] = {"0x20", "0x27", "0x38", "0x3F", "3f"};
    const uint8_t want[] = {0x20, 0x27, 0x38, 0x3F, 0x3F};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":"
                 "{\"display\":{\"address\":\"%s\"}}}", good[i]);
        systemConfig.lcdI2CAddress = 0x21;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i], systemConfig.lcdI2CAddress, good[i]);
    }
}

static void test_a_non_lcd_address_keeps_the_current_one(void) {
    const char* bad[] = {"\"0x1F\"", "\"0x28\"", "\"0x37\"", "\"0x40\"", "\"0x3C0\"",
                         "\"0x3G\"", "\"\"", "\"zz\"", "39"};
    for (const char* v : bad) {
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":"
                 "{\"display\":{\"address\":%s}}}", v);
        systemConfig.lcdI2CAddress = 0x27;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x27, systemConfig.lcdI2CAddress, v);
    }
}

static void test_an_out_of_range_stored_lcd_address_drives_the_default(void) {
    systemConfig.lcdI2CAddress = 0x50;   // a pre-#590 DISPLAY ADDRESS could save this
    TEST_ASSERT_EQUAL_HEX8(0x27, effectiveLcdI2CAddress());
    systemConfig.lcdI2CAddress = 0x3F;
    TEST_ASSERT_EQUAL_HEX8(0x3F, effectiveLcdI2CAddress());
    systemConfig.lcdI2CAddress = 0x27;
}

// ---------------------------------------------------------------------------
// #561: logFilter comes back
// ---------------------------------------------------------------------------

static void test_log_filter_is_imported_and_applied_live(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":{\"logFilter\":"
        "{\"controlLevel\":\"WARN\",\"dataLevel\":\"NONE\",\"debugLevel\":\"DEBUG\","
        "\"enabledTags\":\"0x0000F00F\"}}}"));
    TEST_ASSERT_EQUAL_UINT8(2, systemConfig.logFilter.control_level);
    TEST_ASSERT_EQUAL_UINT8(0, systemConfig.logFilter.data_level);
    TEST_ASSERT_EQUAL_UINT8(4, systemConfig.logFilter.debug_level);
    TEST_ASSERT_EQUAL_HEX32(0x0000F00F, systemConfig.logFilter.enabledTags);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_applyLogFilterCalls, "imported filter never reached the live one");
}

// "UNKNOWN" is what the exporter writes for an out-of-range level, and a bad
// tags string is a hand edit: neither may become NONE / 0.
static void test_unreadable_log_filter_values_keep_the_current_ones(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":{\"logFilter\":"
        "{\"controlLevel\":\"UNKNOWN\",\"dataLevel\":7,\"enabledTags\":\"zz\"}}}"));
    TEST_ASSERT_EQUAL_UINT8(3, systemConfig.logFilter.control_level);
    TEST_ASSERT_EQUAL_UINT8(3, systemConfig.logFilter.data_level);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, systemConfig.logFilter.enabledTags);
}

// A sign, a leading space or a value wider than 32 bits is a hand edit, not a
// tag mask: strtoul alone would read "-1" as every tag on.
static void test_malformed_tag_masks_are_refused(void) {
    const char* bad[] = {"-1", " 0xF", "+0xF", "0x1FFFFFFFF", "0xF0 "};
    for (const char* t : bad) {
        char json[200];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":{\"logFilter\":"
                 "{\"enabledTags\":\"%s\"}}}", t);
        systemConfig.logFilter.enabledTags = 0x12345678;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x12345678, systemConfig.logFilter.enabledTags, t);
    }
}

static void test_no_log_filter_key_leaves_the_live_filter_alone(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"system\":{}}"));
    TEST_ASSERT_EQUAL_INT(0, g_applyLogFilterCalls);
}

// ---------------------------------------------------------------------------
// #567: the file must fit this board
// ---------------------------------------------------------------------------

// Stand-in board: digital pins 0..39, A0..A3 on pins 14..17.
static void useSmallBoard() {
    g_stubDigitalPinMax = 39;
    g_stubAnalogFirst = 14;
    g_stubAnalogCount = 4;
}

// A file naming a pin this board lacks is refused before anything is written.
static void test_a_pin_this_board_lacks_refuses_the_whole_file(void) {
    configure(4);
    configure(45);
    const uint8_t pins[2] = {4, 45};
    static char json[4096];
    buildDoc(pins, 2, json, sizeof(json));
    clearInput(45);
    configure(6);                          // must survive: nothing may change

    useSmallBoard();
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL_MESSAGE(getInputByPin(6), "a refused file still replaced the inputs");
    TEST_ASSERT_NULL(getInputByPin(45));
    TEST_ASSERT_EQUAL_INT(0, g_obdTableBuilds);
}

// An input the file calls A<n> must be on this board's A<n>; a number-labelled
// input only needs its pin to exist, even where that pin is analog here.
static void test_an_analog_input_must_land_on_the_same_a_pin(void) {
    useSmallBoard();
    configure(16);                         // A2 on this board
    const uint8_t pins[1] = {16};
    static char json[4096];

    const char* labels[] = {"A2", "16", "A6"};
    const bool fits[] = {true, true, false};
    for (uint8_t i = 0; i < 3; i++) {
        buildDoc(pins, 1, json, sizeof(json));
        JsonDocument d;
        TEST_ASSERT_FALSE(deserializeJson(d, json));
        d["inputs"][0]["pinLabel"] = labels[i];
        serializeJson(d, json, sizeof(json));
        TEST_ASSERT_EQUAL_MESSAGE(fits[i], loadConfigFromJSON(json), labels[i]);
    }
}

// A RELAY's target is a pin; the other action types name a channel.
static void test_a_relay_on_a_pin_this_board_lacks_is_refused(void) {
#if INCLUDE_RULES_ENGINE
    useSmallBoard();
    TEST_ASSERT_FALSE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"actions\":"
        "[{\"id\":1,\"type\":\"relay\",\"target\":60}]}"));
    TEST_ASSERT_EQUAL_INT(0, g_rulesInitCalls);
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"actions\":"
        "[{\"id\":1,\"type\":\"LED\",\"target\":60}]}"));
#endif
}

// On the mega the A-names (pins 54..69) sit past the digital range (0..53):
// they exist only through the analog map.
static void test_analog_pins_past_the_digital_range_exist(void) {
    g_stubDigitalPinMax = 53;
    g_stubAnalogFirst = 54;
    g_stubAnalogCount = 16;
    configure(60);                         // A6
    const uint8_t pins[1] = {60};
    static char json[4096];
    buildDoc(pins, 1, json, sizeof(json));
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    g_stubAnalogCount = 4;                 // A0..A3 only: pin 60 is gone
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
}

// A pin the import would misread as pin 0 (out of range, or a string) fits
// nowhere, though the rest of its record is valid.
static void test_an_unreadable_pin_is_refused(void) {
    configure(4);
    const uint8_t pins[1] = {4};
    static char json[4096];
    const char* bad[] = {"300", "\"A2\"", "-1"};
    for (const char* p : bad) {
        buildDoc(pins, 1, json, sizeof(json));
        JsonDocument d, v;
        TEST_ASSERT_FALSE(deserializeJson(d, json));
        TEST_ASSERT_FALSE(deserializeJson(v, p));
        d["inputs"][0]["pin"] = v.as<JsonVariantConst>();
        serializeJson(d, json, sizeof(json));
        TEST_ASSERT_FALSE_MESSAGE(loadConfigFromJSON(json), p);
        TEST_ASSERT_NOT_NULL_MESSAGE(getInputByPin(4), p);
    }
}

// Another board's file whose pins all fit is imported (with a NOTE naming both).
static void test_a_file_from_another_board_that_fits_is_imported(void) {
    useSmallBoard();
    configure(4);
    const uint8_t pins[1] = {4};
    static char json[4096];
    buildDoc(pins, 1, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d["firmware"]["platform"] = "TEENSY41";
    serializeJson(d, json, sizeof(json));
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_inputs_absent_from_the_file_are_removed);
    RUN_TEST(test_a_pin_at_another_idx_is_not_duplicated);
    RUN_TEST(test_a_file_whose_records_all_fail_leaves_the_inputs_alone);
    RUN_TEST(test_an_inputs_import_rebuilds_the_pid_table);
    RUN_TEST(test_an_empty_inputs_array_clears_the_table_and_succeeds);
    RUN_TEST(test_replacing_inputs_re_arms_the_rules);
    RUN_TEST(test_a_document_without_inputs_leaves_them_alone);
    RUN_TEST(test_sea_level_pressure_is_imported);
    RUN_TEST(test_an_out_of_range_sea_level_pressure_keeps_the_current_one);
    RUN_TEST(test_an_lcd_address_in_either_backpack_range_is_imported);
    RUN_TEST(test_a_non_lcd_address_keeps_the_current_one);
    RUN_TEST(test_an_out_of_range_stored_lcd_address_drives_the_default);
    RUN_TEST(test_log_filter_is_imported_and_applied_live);
    RUN_TEST(test_unreadable_log_filter_values_keep_the_current_ones);
    RUN_TEST(test_malformed_tag_masks_are_refused);
    RUN_TEST(test_no_log_filter_key_leaves_the_live_filter_alone);
    RUN_TEST(test_a_pin_this_board_lacks_refuses_the_whole_file);
    RUN_TEST(test_an_analog_input_must_land_on_the_same_a_pin);
    RUN_TEST(test_a_relay_on_a_pin_this_board_lacks_is_refused);
    RUN_TEST(test_a_file_from_another_board_that_fits_is_imported);
    RUN_TEST(test_analog_pins_past_the_digital_range_exist);
    RUN_TEST(test_an_unreadable_pin_is_refused);
    return UNITY_END();
}
