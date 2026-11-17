// Native unit test: JSON IMPORT restores a SYSTEM DUMP JSON (#561, #562).
//
// #562: an "inputs" array used to MERGE into the input table, so restoring a
// backup kept every input added since, and a pin the file carried at another
// idx could end up live in two slots. It now replaces the table, as "rules"
// and "actions" already did.
// #561: the log filter (system.log since #612) was exported and never read back. (system.serial is
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
#include "../../src/config/input_store.cpp"
#include "../../src/outputs/table/output_table.cpp"   // the stored outputs (#612)
#include "../../src/outputs/table/output_admission.cpp"   // import admission (#612)
#include "../../src/inputs/calibration_apply.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"
#include "../../src/sensors/can_sensor_library/obd_output_pids.cpp"
#include "../../src/buses/pinctrl/pin_label.cpp"
#include "../../src/config/json_config.cpp"
#include "../../src/rules/rules_engine.cpp"  // rulesNormalizeWarn, for the converter below
#include "../../src/rules/rules_json.cpp"   // the real table-free converters (#567 checks them)

#define TEST_LINKS_RULES_ENGINE
#include "../stubs/firmware_stubs.h"

// Control engine and config generation: not in this binary. The import calls
// them on a rules/actions array, and since #562 on an inputs array (re-arm).
static int g_generationBumps = 0;
void configGenerationBump() { g_generationBumps++; }
// obd_query.cpp is not in this binary; count the rebuilds instead (#562).
static int g_obdTableBuilds = 0;
void obdQuery_buildLookupTable() { g_obdTableBuilds++; }
void obdQuery_markStale() {}
#if INCLUDE_RULES_ENGINE
Action actions[MAX_ACTIONS];
ActionLink actionLinks[MAX_ACTION_LINKS];
static int g_rulesInitCalls = 0;
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
    g_syncOutputsCalls = 0;
    g_stubDigitalPinMax = 0xFF;
    g_stubAnalogFirst = 0xFF;
    g_stubAnalogCount = 0;
    g_obdTableBuilds = 0;
    g_generationBumps = 0;
#if INCLUDE_RULES_ENGINE
    g_rulesInitCalls = 0;
    g_quiesceCalls = 0;
    memset(g_pinAction, 0, sizeof(g_pinAction));
    for (uint8_t i = 0; i < MAX_RULES; i++) rules[i].id = RULES_ID_NONE;
    for (uint8_t i = 0; i < MAX_ACTIONS; i++) actions[i].id = RULES_ID_NONE;
    for (uint8_t i = 0; i < MAX_ACTION_LINKS; i++) actionLinks[i].actionId = RULES_ID_NONE;
#endif
    systemConfig.buses.active_i2c = 0xFF;
    systemConfig.serial.enabled_mask = 0;
    g_stubBusRefused = false;
    g_stubRegistryCapacity = 64;
    systemConfig.logFilter.console_level = 3;
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
    doc["firmware"]["platform"] = "NATIVE";   // this board (the stub's boardName())
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
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"inputs\":"
        "[{\"idx\":0,\"pin\":6,\"app\":\"NO_SUCH_APP\",\"sensor\":\"NO_SUCH_SENSOR\"}]}"));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_NOT_NULL(getInputByPin(5));
    TEST_ASSERT_EQUAL_UINT8(2, occupiedSlots());
}

// #567: all or nothing. A refused input refuses the system block too: the
// live outputs (#610) and the stored settings stay as they were.
static void test_a_refused_input_leaves_the_system_block_alone(void) {
    configure(4);
    outputsClear();
    TEST_ASSERT_FALSE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},"
        "\"system\":{\"outputs\":[{\"id\":2,\"format\":\"CSV\",\"transport\":\"USB\",\"enabled\":true}]},\"inputs\":"
        "[{\"idx\":0,\"pin\":6,\"app\":\"NO_SUCH_APP\",\"sensor\":\"NO_SUCH_SENSOR\"}]}"));
    TEST_ASSERT_FALSE(outputExists(2));
    TEST_ASSERT_EQUAL_INT(0, g_syncOutputsCalls);
    TEST_ASSERT_EQUAL_INT(0, g_generationBumps);
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
}

// The OBD-II PID table points into inputs[]; an inputs import rebuilds it.
static void test_an_inputs_import_rebuilds_the_pid_table(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_INT(1, g_obdTableBuilds);
}

static void test_an_empty_inputs_array_clears_the_table_and_succeeds(void) {
    configure(4);
    configure(5);
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
    TEST_ASSERT_EQUAL_UINT8(0, numActiveInputs);
}

// A document with no "inputs" key says nothing about inputs: they stay.
// Replacing the inputs can orphan a rule, so the engine is re-armed, which is
// where an orphan gets its WARNING (rulesManagerInit).
static void test_replacing_inputs_re_arms_the_rules(void) {
#if INCLUDE_RULES_ENGINE
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"inputs\":[]}"));
    TEST_ASSERT_EQUAL_INT(1, g_rulesInitCalls);
#endif
}

static void test_a_document_without_inputs_leaves_them_alone(void) {
    configure(4);
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{}}"));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
}

// ---------------------------------------------------------------------------
// #574: seaLevelPressure is range-checked like SYSTEM SEA_LEVEL
// ---------------------------------------------------------------------------

static void test_sea_level_pressure_is_imported(void) {
    systemConfig.seaLevelPressure = 1013.25f;
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":"
        "{\"constants\":{\"seaLevelPressure\":1025.5}}}"));
    TEST_ASSERT_EQUAL_FLOAT(1025.5f, systemConfig.seaLevelPressure);
}

// 0 is what a pre-#574 `SYSTEM SEA_LEVEL abc` saved and exported.
static void test_an_out_of_range_sea_level_pressure_keeps_the_current_one(void) {
    const char* bad[] = {"0", "849.9", "1100.1", "-1013", "\"abc\""};
    for (const char* v : bad) {
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":"
                 "{\"constants\":{\"seaLevelPressure\":%s}}}", v);
        systemConfig.seaLevelPressure = 1013.25f;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1013.25f, systemConfig.seaLevelPressure, v);
    }
}

// ---------------------------------------------------------------------------
// #633: buses.canPollMs is BUS CAN POLL
// ---------------------------------------------------------------------------

static bool importCanPoll(const char* value) {
    char text[160];
    snprintf(text, sizeof(text),
             "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"buses\":{\"canPollMs\":%s}}}", value);
    return loadConfigFromJSON(text);
}

static void test_a_can_poll_rate_is_imported(void) {
    systemConfig.buses.canPollMs = 50;
    TEST_ASSERT_TRUE(importCanPoll("200"));
    TEST_ASSERT_EQUAL_UINT16(200, systemConfig.buses.canPollMs);
    TEST_ASSERT_TRUE(importCanPoll("1000"));
    TEST_ASSERT_EQUAL_UINT16(1000, systemConfig.buses.canPollMs);
}

// Captures what the console is told during one import
class ConsoleCapture : public TransportInterface {
public:
    char out[1024] = {0};
    size_t len = 0;
    size_t write(uint8_t c) override { if (len < sizeof(out) - 1) out[len++] = (char)c; return 1; }
    size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    const char* getName() const override { return "CAPTURE"; }
    uint8_t getCapabilities() const override { return 0; }
    TransportState getState() const override { return TRANSPORT_CONNECTED; }
    bool begin() override { return true; }
    void end() override {}
    void update() override {}
};

// Out of range, a fraction, not a number or an explicit null: the current
// value stays, and the console is told (a present null is not "absent")
static void test_a_bad_can_poll_rate_keeps_the_current_one(void) {
    const char* bad[] = {"49", "1001", "0", "-50", "99999", "75.5", "\"200\"", "true", "null"};
    for (const char* v : bad) {
        ConsoleCapture cap;
        g_testControlSink = &cap;
        systemConfig.buses.canPollMs = 300;
        TEST_ASSERT_TRUE(importCanPoll(v));
        g_testControlSink = nullptr;
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(300, systemConfig.buses.canPollMs, v);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(cap.out, "NOTE: buses.canPollMs out of range (50-1000); kept 300"), v);
    }
}

// buses without the key says nothing about the rate
static void test_a_buses_object_without_a_can_poll_rate_keeps_the_current_one(void) {
    systemConfig.buses.canPollMs = 300;
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"buses\":{\"i2c\":0}}}"));
    TEST_ASSERT_EQUAL_UINT16(300, systemConfig.buses.canPollMs);
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
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":"
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
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":"
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
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"log\":"
        "{\"consoleLevel\":\"WARN\",\"debugLevel\":\"DEBUG\",\"transport\":\"SERIAL2\","
        "\"enabledTags\":\"0x0000F00F\"}}}"));
    TEST_ASSERT_EQUAL_UINT8(2, systemConfig.logFilter.console_level);
    TEST_ASSERT_EQUAL_UINT8(4, systemConfig.logFilter.debug_level);
    TEST_ASSERT_NOT_EQUAL(OT_SERIAL1 + 1, systemConfig.log.primary);   // transports are export-only
    TEST_ASSERT_EQUAL_HEX32(0x0000F00F, systemConfig.logFilter.enabledTags);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_applyLogFilterCalls, "imported filter never reached the live one");
}

// "UNKNOWN" is what the exporter writes for an out-of-range level, and a bad
// tags string is a hand edit: neither may become NONE / 0.
static void test_unreadable_log_filter_values_keep_the_current_ones(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"log\":"
        "{\"consoleLevel\":\"UNKNOWN\",\"debugLevel\":7,\"enabledTags\":\"zz\"}}}"));
    TEST_ASSERT_EQUAL_UINT8(3, systemConfig.logFilter.console_level);
    TEST_ASSERT_EQUAL_UINT8(3, systemConfig.logFilter.debug_level);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, systemConfig.logFilter.enabledTags);
}

// A sign, a leading space or a value wider than 32 bits is a hand edit, not a
// tag mask: strtoul alone would read "-1" as every tag on.
static void test_malformed_tag_masks_are_refused(void) {
    const char* bad[] = {"-1", " 0xF", "+0xF", "0x1FFFFFFFF", "0xF0 "};
    for (const char* t : bad) {
        char json[200];
        snprintf(json, sizeof(json),
                 "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"log\":"
                 "{\"enabledTags\":\"%s\"}}}", t);
        systemConfig.logFilter.enabledTags = 0x12345678;
        TEST_ASSERT_TRUE(loadConfigFromJSON(json));
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x12345678, systemConfig.logFilter.enabledTags, t);
    }
}

// #610: an imported outputs.<name>.enabled used to wait for a reboot. A
// committed import hands the live outputs the new flags; a refused one
// leaves them alone.
static void test_imported_output_enables_reach_the_live_outputs(void) {
    outputsClear();
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"outputs\":"
        "[{\"id\":2,\"format\":\"CSV\",\"transport\":\"USB\",\"enabled\":true}]}}"));
    TEST_ASSERT_TRUE(outputIsEnabled(2));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_syncOutputsCalls, "imported enable never reached the live outputs");
}

// #612 phase 2b-1: the outputs array replaces the stored table, ids kept. An
// enabled output that can't run on the file's own settings (here: ELM327 on
// a serial port this file doesn't turn on) is imported switched off; a record
// this board can't hold is skipped; and with no inputs in the file, an id
// that now means another output is routed to every input, as ADD does.
static void test_the_outputs_array_replaces_the_table(void) {
    systemConfig.serial.enabled_mask = 0;
    systemConfig.console.primary = OT_ESP32_BT;
    outputsClear();
    configure(4);
    getInputByPin(4)->routeMask = 0x00;
    outputCreate(FMT_CSV, OT_USB, true);   // id 0, about to become something else
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"outputs\":"
        "[{\"id\":0,\"format\":\"ELM327\",\"transport\":\"BLE\",\"enabled\":true},"
        "{\"id\":3,\"format\":\"REALDASH\",\"transport\":\"USB\",\"enabled\":true,\"interval\":50},"
        "{\"id\":7,\"format\":\"OBD\",\"transport\":\"CAN1\",\"enabled\":true}]}}"));
    TEST_ASSERT_EQUAL(FMT_ELM327, outputFormatOf(0));
    TEST_ASSERT_TRUE(outputIsEnabled(0));
    TEST_ASSERT_EQUAL(FMT_REALDASH, outputFormatOf(3));
    TEST_ASSERT_EQUAL_UINT16(50, outputRecord(3)->interval);
    TEST_ASSERT_FALSE(outputExists(7));                            // OBD isn't built here: skipped
    TEST_ASSERT_EQUAL_UINT8(2, outputCount());
    TEST_ASSERT_EQUAL_HEX8(0x09, getInputByPin(4)->routeMask);     // ids 0 and 3 now mean new outputs

    // A RealDash on the console's stream is imported off: the console wins
    systemConfig.console.primary = OT_USB;
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"outputs\":"
        "[{\"id\":1,\"format\":\"REALDASH\",\"transport\":\"USB\",\"enabled\":true}]}}"));
    TEST_ASSERT_TRUE(outputExists(1));
    TEST_ASSERT_FALSE(outputIsEnabled(1));
    systemConfig.console.primary = OT_ESP32_BT;
    outputsClear();
}

// One per kind on import: a file with two ELM327-on-BLE outputs keeps the
// lower id; the second is skipped with a NOTE, not imported to fight the first
static void test_a_second_one_per_kind_output_is_skipped_on_import(void) {
    outputsClear();
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"outputs\":"
        "[{\"id\":4,\"format\":\"ELM327\",\"transport\":\"BLE\",\"enabled\":true},"
        "{\"id\":1,\"format\":\"ELM327\",\"transport\":\"BLE\",\"enabled\":true}]}}"));
    TEST_ASSERT_EQUAL(FMT_ELM327, outputFormatOf(1));
    TEST_ASSERT_FALSE(outputExists(4));
    TEST_ASSERT_EQUAL_UINT8(1, outputCount());
    outputsClear();
}

// A backup restored onto a freshly reset unit (whose table is the defaults)
// keeps every input's routes as the file has them (bench 2026-11-09: a restore
// onto a reset teensy40 came back with SD and ELM327-serial routes missing)
static void test_a_restore_onto_a_reset_unit_keeps_the_files_routes(void) {
    outputsClear();
    outputCreate(FMT_ELM327, OT_BLE, true);            // 0
    outputCreate(FMT_CSV, OT_USB, false);              // 1, deleted below
    outputCreate(FMT_CSV, OT_USB, false);              // 2 (disabled outputs may share a stream)
    outputCreate(FMT_REALDASH, OT_USB, false);         // 3
    outputCreate(FMT_CSV, OT_USB, false);              // 4
    outputCreate(FMT_REALDASH, OT_USB, false);         // 5
    outputErase(1);
    configure(4);
    getInputByPin(4)->routeMask = 0x3D;                // 0, 2, 3, 4, 5
    JsonDocument doc;
    doc["schemaVersion"] = 1;
    doc["mode"] = "runtime";
    doc["firmware"]["platform"] = "NATIVE";   // this board (the stub's boardName())
    JsonArray outs = doc["system"]["outputs"].to<JsonArray>();
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) if (outputExists(id)) outputToJson(outs.add<JsonObject>(), id);
    JsonObject in = doc["inputs"].to<JsonArray>().add<JsonObject>();
    exportInputToJSON(in, getInputByPin(4));
    in["idx"] = 0;
    static char json[6144];
    TEST_ASSERT_TRUE(serializeJson(doc, json, sizeof(json)) < sizeof(json) - 1);

    clearInput(4);                                     // the reset unit: defaults only
    outputsClear();
    outputCreate(FMT_ELM327, OT_BLE, true);
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_UINT8(5, outputCount());
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_EQUAL_HEX8(0x3D, getInputByPin(4)->routeMask);
    outputsClear();
}

// BLE carries one data output (#612 2c): a file with ELM327 and RealDash both
// enabled on BLE imports the second one switched off
static void test_ble_imports_one_enabled_data_output(void) {
    outputsClear();
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{\"outputs\":"
        "[{\"id\":0,\"format\":\"ELM327\",\"transport\":\"BLE\",\"enabled\":true},"
        "{\"id\":1,\"format\":\"REALDASH\",\"transport\":\"BLE\",\"enabled\":true,\"interval\":100}]}}"));
    TEST_ASSERT_TRUE(outputIsEnabled(0));
    TEST_ASSERT_TRUE(outputExists(1));
    TEST_ASSERT_FALSE(outputIsEnabled(1));
    TEST_ASSERT_EQUAL_UINT16(100, outputRecord(1)->interval);
    outputsClear();
}

static void test_no_log_filter_key_leaves_the_live_filter_alone(void) {
    TEST_ASSERT_TRUE(loadConfigFromJSON("{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"system\":{}}"));
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_syncOutputsCalls, "a refused file touched the live outputs (#610)");
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
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"actions\":"
        "[{\"id\":1,\"type\":\"relay\",\"target\":60}]}"));
    TEST_ASSERT_EQUAL_INT(0, g_rulesInitCalls);
    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},\"actions\":"
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

// No imports across hardware (owner, 2026-11-14): a file from another board
// is refused even where every pin fits, and so is one that doesn't say.
static void test_a_file_from_another_board_is_refused_even_if_it_fits(void) {
    useSmallBoard();
    configure(4);
    const uint8_t pins[1] = {4};
    static char json[4096];
    buildDoc(pins, 1, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d["firmware"]["platform"] = "TEENSY41";
    serializeJson(d, json, sizeof(json));
    clearInput(4);
    configure(6);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(4));
    TEST_ASSERT_NOT_NULL(getInputByPin(6));
}

static void test_a_file_without_a_platform_is_refused(void) {
    configure(4);
    const uint8_t pins[1] = {4};
    static char json[4096];
    buildDoc(pins, 1, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d.remove("firmware");
    serializeJson(d, json, sizeof(json));
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    d["firmware"]["platform"] = "NATIVE";
    serializeJson(d, json, sizeof(json));
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
}


// ---------------------------------------------------------------------------
// #567 Part B / #603: every check before the first write
// ---------------------------------------------------------------------------

// The file's inputs as a document, from inputs configured on `pins`, which
// are then cleared again: the table the test starts from is its own.
static void docFrom(const uint8_t* pins, uint8_t n, char* out, size_t outLen) {
    for (uint8_t i = 0; i < n; i++) configure(pins[i]);
    buildDoc(pins, n, out, outLen);
    for (uint8_t i = 0; i < n; i++) clearInput(pins[i]);
}

// Insert `extra` (`"key":value,...`) as top-level members of a document
static void addMembers(char* json, size_t len, const char* extra) {
    JsonDocument d, e;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    char wrapped[2048];
    snprintf(wrapped, sizeof(wrapped), "{%s}", extra);
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(e, wrapped), wrapped);
    for (JsonPairConst kv : e.as<JsonObjectConst>()) d[kv.key()] = kv.value();
    TEST_ASSERT_TRUE(serializeJson(d, json, len) < len - 1);
}

// #603 item 3: one refused record refuses the file; the old table stays whole
static void test_one_bad_record_refuses_the_whole_file(void) {
    static char json[4096];
    const uint8_t pins[2] = {7, 8};
    docFrom(pins, 2, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d["inputs"][1]["sensor"] = "NO_SUCH_SENSOR";
    serializeJson(d, json, sizeof(json));
    configure(4);
    configure(8);                          // the bad record's pin: kept as it was
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(7));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_NOT_NULL(getInputByPin(8));
    TEST_ASSERT_EQUAL_INT(0, g_generationBumps);
}

// #603 item 2: the old inputs give up their slots before the new ones arrive
static void test_a_full_table_takes_a_file_of_different_pins(void) {
    static char json[16384];
    uint8_t filePins[MAX_INPUTS], oldPins[MAX_INPUTS];
    for (uint8_t i = 0; i < MAX_INPUTS; i++) { filePins[i] = (uint8_t)(60 + i); oldPins[i] = i; }
    docFrom(filePins, MAX_INPUTS, json, sizeof(json));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) configure(oldPins[i]);
    TEST_ASSERT_EQUAL_UINT8(MAX_INPUTS, occupiedSlots());
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_UINT8(MAX_INPUTS, occupiedSlots());
    for (uint8_t i = 0; i < MAX_INPUTS; i++) TEST_ASSERT_NOT_NULL(getInputByPin(filePins[i]));
}

static void test_more_inputs_than_slots_is_refused(void) {
    static char json[16384];
    uint8_t pins[MAX_INPUTS];
    for (uint8_t i = 0; i < MAX_INPUTS; i++) pins[i] = (uint8_t)(10 + i);
    docFrom(pins, MAX_INPUTS, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    JsonObject extra = d["inputs"].add<JsonObject>();
    extra.set(d["inputs"][0].as<JsonObjectConst>());
    extra["pin"] = 100;
    serializeJson(d, json, sizeof(json));
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_UINT8(0, occupiedSlots());
}

static void test_a_pin_named_twice_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d["inputs"].add(d["inputs"][0].as<JsonObjectConst>());
    serializeJson(d, json, sizeof(json));
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(7));
}

// A pin something else holds (a running bus, a serial port, SD's chip
// select) refuses the file before anything is written.
static void test_an_input_on_a_held_pin_refuses_the_file(void) {
    static char json[4096];
    const uint8_t pins[2] = {7, 9};
    docFrom(pins, 2, json, sizeof(json));
    configure(4);
    g_pinReserved[9] = true;
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(4));
    TEST_ASSERT_NULL(getInputByPin(7));
    TEST_ASSERT_EQUAL_INT(0, g_generationBumps);
}

// The buses the file selects count, though they start only at the next boot
static void test_an_input_on_the_files_own_bus_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {18};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"system\":{\"buses\":{\"i2c\":0}}");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(18));
    TEST_ASSERT_EQUAL_UINT8(0xFF, systemConfig.buses.active_i2c);   // not committed

    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"system\":{\"buses\":{\"i2c\":255}}");
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(18));
}


#if NUM_SERIAL_PORTS >= 2
// A serial port the file turns on holds its pins from the next boot
static void test_an_input_on_a_serial_port_the_file_enables_is_refused(void) {
    const uint8_t rx = getDefaultSerialRX(2);
    if (rx == 0xFF || rx > PIN_PHYSICAL_MAX) TEST_IGNORE_MESSAGE("no Serial2 pins on this build");
    systemConfig.serial.enabled_mask = 0;
    static char json[4096];
    const uint8_t pins[1] = {rx};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"system\":{\"serial\":{\"ports\":[{\"port\":2,\"enabled\":true}]}}");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(rx));
    TEST_ASSERT_EQUAL_UINT8(0, systemConfig.serial.enabled_mask);
}
#endif

#if INCLUDE_RULES_ENGINE
// An old relay's pin is free for the file's input: the old action targets are
// released before the inputs are written, not after.
static void test_an_input_may_take_an_old_relays_pin(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[]");
    actions[0].id = 1;
    actions[0].type = ACT_RELAY;
    actions[0].target = 7;
    actions[0].ruleRef = RULES_ID_NONE;
    g_pinAction[7] = true;                 // its registry claim
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(7));
    TEST_ASSERT_EQUAL_INT(1, g_quiesceCalls);
}

// ...but not while that relay stays: the file has no actions, so it keeps it
static void test_an_input_on_a_kept_relays_pin_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    actions[0].id = 1;
    actions[0].type = ACT_RELAY;
    actions[0].target = 7;
    actions[0].ruleRef = RULES_ID_NONE;
    g_pinAction[7] = true;
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_NULL(getInputByPin(7));
    TEST_ASSERT_EQUAL_INT(0, g_quiesceCalls);
}

// A file with no inputs, rules or actions leaves the relays alone (#648):
// only a table replacement switches them off.
static void test_a_system_only_import_leaves_relays_alone(void) {
    static char json[4096];
    docFrom(nullptr, 0, json, sizeof(json));
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, json));
    d.remove("inputs");
    d.remove("rules");
    d.remove("actions");
    serializeJson(d, json, sizeof(json));
    actions[0].id = 1;
    actions[0].type = ACT_RELAY;
    actions[0].target = 7;
    actions[0].ruleRef = RULES_ID_NONE;
    g_pinAction[7] = true;
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_INT(0, g_quiesceCalls);
}

// A refused file switches nothing off (#648): quiesce runs only after admission.
static void test_a_refused_import_leaves_relays_alone(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"firmware\":{\"platform\":\"SOME_OTHER_BOARD\"}");
    actions[0].id = 1;
    actions[0].type = ACT_RELAY;
    actions[0].target = 9;
    actions[0].ruleRef = RULES_ID_NONE;
    g_pinAction[9] = true;
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_INT(0, g_quiesceCalls);
}

static void test_a_relay_on_an_input_or_another_relay_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[{\"id\":1,\"type\":\"RELAY\",\"target\":7}]");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));

    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[{\"id\":1,\"type\":\"RELAY\",\"target\":30},"
                                   "{\"id\":2,\"type\":\"RELAY\",\"target\":30}]");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));

    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[{\"id\":1,\"type\":\"RELAY\",\"target\":30}]");
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
}

// Owner, 2026-11-14: a rule whose input the result won't have refuses the
// file, rather than a NOTE and a rule that never fires.
static void test_a_rule_without_its_input_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"rules\":[{\"id\":1,\"input\":8,\"condition\":\"ABOVE\",\"limitLo\":100}]");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));

    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"rules\":[{\"id\":1,\"input\":7,\"condition\":\"ABOVE\",\"limitLo\":100}]");
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_UINT8(1, rules[0].id);
}

// A file with inputs but no rules keeps the current rules, so they must
// still have their inputs; a file with only rules checks them against the
// inputs already there.
static void test_kept_rules_must_keep_their_inputs(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    configure(8);
    rules[0].id = 3;
    rules[0].sourceInput = 8;
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));   // the file drops pin 8
    TEST_ASSERT_NOT_NULL(getInputByPin(8));

    TEST_ASSERT_TRUE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},"
        "\"rules\":[{\"id\":4,\"input\":8,\"condition\":\"BELOW\",\"limitLo\":1}]}"));
    TEST_ASSERT_FALSE(loadConfigFromJSON(
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},"
        "\"rules\":[{\"id\":4,\"input\":9,\"condition\":\"BELOW\",\"limitLo\":1}]}"));
}

static void test_an_action_following_a_missing_rule_is_refused(void) {
    const char* head = "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},";
    char json[512];
    snprintf(json, sizeof(json), "%s\"rules\":[],\"actions\":[{\"id\":1,\"type\":\"LED\",\"target\":0,\"rule\":5}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    snprintf(json, sizeof(json), "%s\"rules\":[],\"actions\":[{\"id\":1,\"type\":\"LED\",\"target\":0}]}", head);
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    // Kept actions too: replacing the rules may not strand one
    actions[0].id = 1;
    actions[0].type = ACT_LED;
    actions[0].ruleRef = 5;
    snprintf(json, sizeof(json), "%s\"rules\":[]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
}

static void test_a_bad_or_duplicate_rule_record_is_refused(void) {
    const char* head = "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},";
    char json[512];
    configure(8);
    snprintf(json, sizeof(json), "%s\"rules\":[{\"id\":1,\"input\":8,\"condition\":\"SIDEWAYS\"}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    snprintf(json, sizeof(json), "%s\"rules\":[{\"id\":1,\"input\":8,\"condition\":\"ABOVE\"},"
                                 "{\"id\":1,\"input\":8,\"condition\":\"BELOW\"}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    TEST_ASSERT_EQUAL_INT(0, g_generationBumps);
}
#endif


// A bus the current config selects but that was refused this boot (#564)
// holds nothing, as at commit: re-importing the board's own dump, input and
// all, is not refused for it. A file that newly selects it still is.
static void test_a_refused_bus_does_not_refuse_the_boards_own_dump(void) {
    static char json[4096];
    const uint8_t pins[1] = {18};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"system\":{\"buses\":{\"i2c\":0}}");
    systemConfig.buses.active_i2c = 0;     // selected now, but refused at boot
    g_stubBusRefused = true;
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
    TEST_ASSERT_NOT_NULL(getInputByPin(18));

    systemConfig.buses.active_i2c = 0xFF;  // not selected now: the file asks for it
    clearInput(18);
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"system\":{\"buses\":{\"i2c\":0}}");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
}

// The free-slot marker and the gaps between the pin ranges hold no input
static void test_a_pin_no_input_can_use_is_refused(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    for (int bad : {0xFF, 0x80, 0xBF, 0xE0, 0xFD, 0xFE}) {
        docFrom(pins, 1, json, sizeof(json));
        JsonDocument d;
        TEST_ASSERT_FALSE(deserializeJson(d, json));
        d["inputs"][0]["pin"] = bad;
        d["inputs"][0]["pinLabel"] = "";
        serializeJson(d, json, sizeof(json));
        configure(4);
        ConsoleCapture cap;
        g_testControlSink = &cap;
        TEST_ASSERT_FALSE_MESSAGE(loadConfigFromJSON(json), "pin");
        g_testControlSink = nullptr;
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(cap.out, ") - not a pin an input can use"), cap.out);
        TEST_ASSERT_NOT_NULL(getInputByPin(4));
        TEST_ASSERT_EQUAL_INT(0, g_generationBumps);
        clearInput(4);
    }
}

#if INCLUDE_RULES_ENGINE
// Every action's target is claimed as a pin after the write, not only a
// RELAY's, so the check judges them all
static void test_any_action_target_is_checked_as_a_pin(void) {
    static char json[4096];
    const uint8_t pins[1] = {7};
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[{\"id\":1,\"type\":\"LED\",\"target\":7}]");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    g_pinReserved[30] = true;
    docFrom(pins, 1, json, sizeof(json));
    addMembers(json, sizeof(json), "\"actions\":[{\"id\":1,\"type\":\"BEEPER\",\"target\":30}]");
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
}

// The re-claim needs room in the pin registry
static void test_actions_that_overflow_the_pin_registry_are_refused(void) {
    const char* head = "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},";
    char json[512];
    g_pinReserved[40] = true;
    g_pinReserved[41] = true;
    g_stubRegistryCapacity = 3;            // 2 stay, room for 1
    snprintf(json, sizeof(json), "%s\"actions\":[{\"id\":1,\"type\":\"RELAY\",\"target\":30},"
                                 "{\"id\":2,\"type\":\"RELAY\",\"target\":31}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    snprintf(json, sizeof(json), "%s\"actions\":[{\"id\":1,\"type\":\"RELAY\",\"target\":30}]}", head);
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
}

// A rule id or input that isn't a byte would be read as a real one
static void test_an_unreadable_rule_id_or_input_is_refused(void) {
    const char* head = "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},";
    char json[512];
    configure(0);
    snprintf(json, sizeof(json), "%s\"rules\":[{\"id\":300,\"input\":0,\"condition\":\"ABOVE\"}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    snprintf(json, sizeof(json), "%s\"rules\":[{\"id\":1,\"input\":\"A2\",\"condition\":\"ABOVE\"}]}", head);
    TEST_ASSERT_FALSE(loadConfigFromJSON(json));
    snprintf(json, sizeof(json), "%s\"rules\":[{\"id\":1,\"input\":0,\"condition\":\"ABOVE\"}]}", head);
    TEST_ASSERT_TRUE(loadConfigFromJSON(json));
}
#endif

// The system block's NOTEs describe a file that is applied, never a refused
// one: they print once, and only when the file passes
static void test_system_notes_print_only_for_an_applied_file(void) {
    const char* refused =
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},"
        "\"system\":{\"buses\":{\"canPollMs\":5}},\"inputs\":[{\"idx\":0,\"pin\":6,\"app\":\"NO_SUCH_APP\",\"sensor\":\"X\"}]}";
    const char* applied =
        "{\"schemaVersion\":1,\"mode\":\"runtime\",\"firmware\":{\"platform\":\"NATIVE\"},"
        "\"system\":{\"buses\":{\"canPollMs\":5}}}";
    ConsoleCapture a;
    g_testControlSink = &a;
    TEST_ASSERT_FALSE(loadConfigFromJSON(refused));
    g_testControlSink = nullptr;
    TEST_ASSERT_NULL_MESSAGE(strstr(a.out, "canPollMs"), a.out);
    TEST_ASSERT_NOT_NULL(strstr(a.out, "ERROR: Config not imported; nothing was changed"));

    ConsoleCapture b;
    g_testControlSink = &b;
    TEST_ASSERT_TRUE(loadConfigFromJSON(applied));
    g_testControlSink = nullptr;
    const char* first = strstr(b.out, "NOTE: buses.canPollMs");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL_MESSAGE(strstr(first + 1, "NOTE: buses.canPollMs"), "the NOTE printed twice");
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
    RUN_TEST(test_a_can_poll_rate_is_imported);
    RUN_TEST(test_a_bad_can_poll_rate_keeps_the_current_one);
    RUN_TEST(test_a_buses_object_without_a_can_poll_rate_keeps_the_current_one);
    RUN_TEST(test_an_lcd_address_in_either_backpack_range_is_imported);
    RUN_TEST(test_a_non_lcd_address_keeps_the_current_one);
    RUN_TEST(test_an_out_of_range_stored_lcd_address_drives_the_default);
    RUN_TEST(test_log_filter_is_imported_and_applied_live);
    RUN_TEST(test_unreadable_log_filter_values_keep_the_current_ones);
    RUN_TEST(test_malformed_tag_masks_are_refused);
    RUN_TEST(test_no_log_filter_key_leaves_the_live_filter_alone);
    RUN_TEST(test_imported_output_enables_reach_the_live_outputs);
    RUN_TEST(test_a_refused_input_leaves_the_system_block_alone);
    RUN_TEST(test_a_pin_this_board_lacks_refuses_the_whole_file);
    RUN_TEST(test_an_analog_input_must_land_on_the_same_a_pin);
    RUN_TEST(test_a_relay_on_a_pin_this_board_lacks_is_refused);
    RUN_TEST(test_a_file_from_another_board_is_refused_even_if_it_fits);
    RUN_TEST(test_a_file_without_a_platform_is_refused);
    RUN_TEST(test_the_outputs_array_replaces_the_table);
    RUN_TEST(test_a_second_one_per_kind_output_is_skipped_on_import);
    RUN_TEST(test_a_restore_onto_a_reset_unit_keeps_the_files_routes);
    RUN_TEST(test_ble_imports_one_enabled_data_output);
    RUN_TEST(test_analog_pins_past_the_digital_range_exist);
    RUN_TEST(test_an_unreadable_pin_is_refused);
    RUN_TEST(test_one_bad_record_refuses_the_whole_file);
    RUN_TEST(test_a_full_table_takes_a_file_of_different_pins);
    RUN_TEST(test_more_inputs_than_slots_is_refused);
    RUN_TEST(test_a_pin_named_twice_is_refused);
    RUN_TEST(test_an_input_on_a_held_pin_refuses_the_file);
    RUN_TEST(test_an_input_on_the_files_own_bus_is_refused);
#if NUM_SERIAL_PORTS >= 2
    RUN_TEST(test_an_input_on_a_serial_port_the_file_enables_is_refused);
#endif
    RUN_TEST(test_a_refused_bus_does_not_refuse_the_boards_own_dump);
    RUN_TEST(test_a_pin_no_input_can_use_is_refused);
    RUN_TEST(test_system_notes_print_only_for_an_applied_file);
#if INCLUDE_RULES_ENGINE
    RUN_TEST(test_any_action_target_is_checked_as_a_pin);
    RUN_TEST(test_actions_that_overflow_the_pin_registry_are_refused);
    RUN_TEST(test_an_unreadable_rule_id_or_input_is_refused);
    RUN_TEST(test_an_input_may_take_an_old_relays_pin);
    RUN_TEST(test_a_system_only_import_leaves_relays_alone);
    RUN_TEST(test_a_refused_import_leaves_relays_alone);
    RUN_TEST(test_an_input_on_a_kept_relays_pin_is_refused);
    RUN_TEST(test_a_relay_on_an_input_or_another_relay_is_refused);
    RUN_TEST(test_a_rule_without_its_input_is_refused);
    RUN_TEST(test_kept_rules_must_keep_their_inputs);
    RUN_TEST(test_an_action_following_a_missing_rule_is_refused);
    RUN_TEST(test_a_bad_or_duplicate_rule_record_is_refused);
#endif
    return UNITY_END();
}
