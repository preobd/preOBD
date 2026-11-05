// Native unit test: SYSTEM SD JSON (#536), the SD card and logging status the
// mobile app's System tab polls.
//
// Pins the contract the app builds its SD row on: the state precedence (a
// pulled or missing card says so whatever the output setting; an error
// outranks the CONFIG pause), the key names, and null sizes when there is no
// card to measure.
#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#include "../../src/outputs/sd_status_json.cpp"

void setUp(void) {}
void tearDown(void) {}

// Healthy and logging: every other case changes one field from here.
static SdStatusSnapshot logging(void) {
    SdStatusSnapshot s;
    memset(&s, 0, sizeof(s));
    s.supported = true;
    s.mounted = true;
    s.enabled = true;
    s.fileOpen = true;
    s.fileName = "LOG0042.CSV";
    s.rows = 12400;
    s.capacityKnown = true;
    s.freeBytes = 14532000000ULL;
    s.totalBytes = 15931000000ULL;
    return s;
}

static void test_logging(void) {
    TEST_ASSERT_EQUAL(SD_STATE_LOGGING, sdStatusState(logging()));
}

static void test_build_without_sd_logging(void) {
    // teensy40 / Mega: answered as JSON too, so one client code path polls
    // every board instead of parsing an ERROR line.
    SdStatusSnapshot s;
    memset(&s, 0, sizeof(s));
    TEST_ASSERT_EQUAL(SD_STATE_NONE, sdStatusState(s));
    JsonDocument doc;
    sdStatusToJson(s, doc.to<JsonObject>());
    char buf[200];
    serializeJson(doc, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"sd\":{\"state\":\"none\",\"file\":null,\"rows\":0,"
        "\"errors\":0,\"freeMB\":null,\"totalMB\":null}}", buf);
}

static void test_no_card_wins_over_everything(void) {
    SdStatusSnapshot s = logging();
    s.mounted = false;
    s.enabled = false;
    s.faulted = true;   // the logger latches a fault when enabled with no card
    TEST_ASSERT_EQUAL(SD_STATE_NOCARD, sdStatusState(s));
}

static void test_removed_even_when_output_disabled(void) {
    // Cards are mounted at boot only, so a pulled card stays gone after the
    // output is switched off: "off" would hide that it needs a restart.
    SdStatusSnapshot s = logging();
    s.cardRemoved = true;
    s.faulted = true;
    s.fileOpen = false;
    s.enabled = false;
    TEST_ASSERT_EQUAL(SD_STATE_REMOVED, sdStatusState(s));
}

static void test_off(void) {
    SdStatusSnapshot s = logging();
    s.enabled = false;
    s.fileOpen = false;
    TEST_ASSERT_EQUAL(SD_STATE_OFF, sdStatusState(s));
}

static void test_error_outranks_config_pause(void) {
    SdStatusSnapshot s = logging();
    s.faulted = true;
    s.fileOpen = false;
    s.configMode = true;
    TEST_ASSERT_EQUAL(SD_STATE_ERROR, sdStatusState(s));
}

static void test_config_mode_is_idle_with_file_open(void) {
    // The logger keeps its file open across CONFIG but writes nothing.
    SdStatusSnapshot s = logging();
    s.configMode = true;
    TEST_ASSERT_EQUAL(SD_STATE_IDLE, sdStatusState(s));
}

static void test_enabled_without_file_is_idle(void) {
    // Before the first row, or no inputs routed to SD.
    SdStatusSnapshot s = logging();
    s.fileOpen = false;
    s.fileName = nullptr;
    TEST_ASSERT_EQUAL(SD_STATE_IDLE, sdStatusState(s));
}

static void test_json_logging(void) {
    JsonDocument doc;
    sdStatusToJson(logging(), doc.to<JsonObject>());
    char buf[200];
    serializeJson(doc, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"sd\":{\"state\":\"logging\",\"file\":\"LOG0042.CSV\",\"rows\":12400,"
        "\"errors\":0,\"freeMB\":14532,\"totalMB\":15931}}", buf);
}

static void test_json_removed_has_null_sizes(void) {
    SdStatusSnapshot s = logging();
    s.cardRemoved = true;
    s.faulted = true;
    s.fileOpen = false;
    s.errors = 0;
    JsonDocument doc;
    sdStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("removed", doc["sd"]["state"]);
    TEST_ASSERT_EQUAL_STRING("LOG0042.CSV", doc["sd"]["file"]);  // the file that was cut short
    TEST_ASSERT_TRUE(doc["sd"]["freeMB"].isNull());
    TEST_ASSERT_TRUE(doc["sd"]["totalMB"].isNull());
}

static void test_json_unknown_capacity_and_no_file(void) {
    // A platform with no capacity call (stock Arduino SD), before any file.
    SdStatusSnapshot s = logging();
    s.capacityKnown = false;
    s.fileOpen = false;
    s.fileName = nullptr;
    s.rows = 0;
    JsonDocument doc;
    sdStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("idle", doc["sd"]["state"]);
    TEST_ASSERT_TRUE(doc["sd"]["file"].isNull());
    TEST_ASSERT_TRUE(doc["sd"]["freeMB"].isNull());
}

static void test_state_names(void) {
    TEST_ASSERT_EQUAL_STRING("none",    sdStatusStateName(SD_STATE_NONE));
    TEST_ASSERT_EQUAL_STRING("nocard",  sdStatusStateName(SD_STATE_NOCARD));
    TEST_ASSERT_EQUAL_STRING("removed", sdStatusStateName(SD_STATE_REMOVED));
    TEST_ASSERT_EQUAL_STRING("off",     sdStatusStateName(SD_STATE_OFF));
    TEST_ASSERT_EQUAL_STRING("error",   sdStatusStateName(SD_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("idle",    sdStatusStateName(SD_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("logging", sdStatusStateName(SD_STATE_LOGGING));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_logging);
    RUN_TEST(test_build_without_sd_logging);
    RUN_TEST(test_no_card_wins_over_everything);
    RUN_TEST(test_removed_even_when_output_disabled);
    RUN_TEST(test_off);
    RUN_TEST(test_error_outranks_config_pause);
    RUN_TEST(test_config_mode_is_idle_with_file_open);
    RUN_TEST(test_enabled_without_file_is_idle);
    RUN_TEST(test_json_logging);
    RUN_TEST(test_json_removed_has_null_sizes);
    RUN_TEST(test_json_unknown_capacity_and_no_file);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
