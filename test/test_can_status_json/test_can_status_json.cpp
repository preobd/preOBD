// Native unit test: machine-readable CAN scan and bus status (#510).
//
// Pins the JSON contract the webapp and mobile app build their CAN UIs on
// (key names, enum spellings, null ages, hex payloads) and the scan's frame
// accounting: true frame counts, the real payload length, and no missed IDs
// when more are active than the frame cache holds (the old scan sampled the
// cache every loop pass).
#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#ifndef MAX_SCAN_RESULTS
#define MAX_SCAN_RESULTS 32
#endif

#include "../../src/inputs/can_status_json.cpp"
#include "../../src/sensors/drivers/can/can_scan.cpp"

// can_scan.cpp prints through msg.control; a null-transport router makes
// every print a no-op.
MessageRouter router;
MessageAPI msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

void setUp(void) { cancelCANScan(); }
void tearDown(void) {}

static void test_scan_status_json(void) {
    CanScanSnapshot s = {CAN_SCAN_JSON_LISTENING, 3, 10000, 6200, 5};
    JsonDocument doc;
    canScanStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("listening", doc["state"]);
    TEST_ASSERT_EQUAL_UINT8(3, doc["scanId"]);
    TEST_ASSERT_EQUAL_UINT16(10000, doc["durationMs"]);
    TEST_ASSERT_EQUAL_UINT32(6200, doc["remainingMs"]);
    TEST_ASSERT_EQUAL_UINT8(5, doc["found"]);
    TEST_ASSERT_TRUE(doc["passive"]);

    s.state = CAN_SCAN_JSON_DONE;
    canScanStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("done", doc["state"]);
    s.state = CAN_SCAN_JSON_IDLE;
    canScanStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("idle", doc["state"]);
}

static void test_scan_entry_json(void) {
    const uint8_t data[] = {0x1A, 0xF8};
    CanScanEntrySnapshot e = {0x7E8, 0x0C, 2, 143, 40, data};
    JsonDocument doc;
    canScanEntryToJson(e, doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_UINT16(2024, doc["canId"]);
    TEST_ASSERT_EQUAL_UINT8(12, doc["pid"]);
    TEST_ASSERT_TRUE(doc["obd"]);
    TEST_ASSERT_EQUAL_UINT8(2, doc["dataLength"]);
    TEST_ASSERT_EQUAL_UINT32(143, doc["frames"]);
    TEST_ASSERT_EQUAL_UINT32(40, doc["ageMs"]);
    TEST_ASSERT_EQUAL_STRING("1AF8", doc["data"]);

    e.canId = 0x400;               // custom broadcast: not OBD
    canScanEntryToJson(e, doc.to<JsonObject>());
    TEST_ASSERT_FALSE(doc["obd"]);
}

static void test_bus_can_json(void) {
    CanBusSnapshot s;
    memset(&s, 0, sizeof(s));
    s.buses = 2; s.maxScanResults = 32;
    s.inBus = 0; s.inMode = 1; s.inBaudrate = 500000; s.inRunning = true;
    s.framesRx = 81234; s.haveFrame = true; s.lastFrameAgeMs = 12;
    s.pollActive = true; s.missStreak = 0; s.requests = 5120; s.answers = 5101;
    s.outBus = 0xFF; s.outEnabled = false; s.outModule = false; s.outBaudrate = 500000;

    JsonDocument doc;
    JsonArray imports = canBusStatusToJson(s, doc.to<JsonObject>());
    CanImportSnapshot a = {0xC0, "CAN:0", 0x7E8, 0x0C, true, true, 35};
    CanImportSnapshot b = {0xC1, "CAN:1", 0x400, 0x10, false, false, 0};
    canImportToJson(a, imports.add<JsonObject>());
    canImportToJson(b, imports.add<JsonObject>());

    TEST_ASSERT_EQUAL_UINT8(2, doc["caps"]["buses"]);
    TEST_ASSERT_EQUAL_UINT32(1000000, doc["caps"]["baudrates"][3]);
    TEST_ASSERT_FALSE(doc["caps"]["listenShared"]);
    TEST_ASSERT_EQUAL_UINT16(60000, doc["caps"]["scanMs"][1]);

    TEST_ASSERT_EQUAL_UINT8(0, doc["input"]["bus"]);
    TEST_ASSERT_EQUAL_STRING("NORMAL", doc["input"]["mode"]);
    TEST_ASSERT_EQUAL_UINT32(500000, doc["input"]["baudrate"]);
    TEST_ASSERT_TRUE(doc["input"]["running"]);
    TEST_ASSERT_FALSE(doc["input"]["rebootPending"]);
    TEST_ASSERT_EQUAL_UINT32(12, doc["input"]["lastFrameAgeMs"]);
    TEST_ASSERT_EQUAL_UINT32(5101, doc["input"]["poll"]["answers"]);
    TEST_ASSERT_FALSE(doc["input"]["poll"]["backoff"]);

    TEST_ASSERT_EQUAL_UINT8(255, doc["output"]["bus"]);   // none, as in the dump
    TEST_ASSERT_FALSE(doc["output"]["module"]);

    TEST_ASSERT_EQUAL_STRING("CAN:0", doc["imports"][0]["pinLabel"]);
    TEST_ASSERT_EQUAL_UINT8(192, doc["imports"][0]["pin"]);
    TEST_ASSERT_EQUAL_UINT16(2024, doc["imports"][0]["sourceCanId"]);
    TEST_ASSERT_EQUAL_UINT32(35, doc["imports"][0]["ageMs"]);
    TEST_ASSERT_TRUE(doc["imports"][1]["ageMs"].isNull());   // never seen

    s.inMode = 2; s.haveFrame = false; s.rebootPending = true;
    canBusStatusToJson(s, doc.to<JsonObject>());
    TEST_ASSERT_TRUE(doc["input"]["rebootPending"]);
    TEST_ASSERT_EQUAL_STRING("LISTEN", doc["input"]["mode"]);
    TEST_ASSERT_TRUE(doc["input"]["lastFrameAgeMs"].isNull());
}

// The scan counts frames, keeps the real payload length, and records IDs
// independently of the frame cache's size.
static void test_scan_counts_frames(void) {
    startCANScan(10000, true);
    const uint8_t rpm[] = {0x1A, 0xF8};
    const uint8_t ect[] = {0x7B};
    for (int i = 0; i < 5; i++) canScanOnFrame(0x7E8, 0x0C, rpm, 2, 100 + i);
    canScanOnFrame(0x7E8, 0x05, ect, 1, 200);

    uint8_t n = 0;
    const CANScanResult* r = getCANScanResults(&n);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT32(5, r[0].sample_count);
    TEST_ASSERT_EQUAL_UINT8(2, r[0].data_length);
    TEST_ASSERT_EQUAL_UINT32(104, r[0].last_seen_ms);
    TEST_ASSERT_EQUAL_UINT8(1, r[1].data_length);
    TEST_ASSERT_EQUAL_HEX8(0x7B, r[1].last_data[0]);

    // More distinct IDs than a 16-entry frame cache: all recorded, up to the cap
    for (uint16_t id = 0x100; id < 0x100 + MAX_SCAN_RESULTS + 4; id++) {
        canScanOnFrame(id, 0x01, ect, 1, 300);
    }
    getCANScanResults(&n);
    TEST_ASSERT_EQUAL_UINT8(MAX_SCAN_RESULTS, n);
}

// Frames are ignored unless listening; each start bumps scanId.
static void test_scan_lifecycle(void) {
    const uint8_t d[] = {0x00};
    canScanOnFrame(0x7E8, 0x0C, d, 1, 0);
    uint8_t n = 0;
    getCANScanResults(&n);
    TEST_ASSERT_EQUAL_UINT8(0, n);          // idle: not recorded

    uint8_t id0 = getCANScanId();
    g_test_now = 1000;
    startCANScan(5000, true);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(id0 + 1), getCANScanId());
    TEST_ASSERT_EQUAL_INT(SCAN_LISTENING, getCANScanState());
    TEST_ASSERT_EQUAL_UINT32(3000, getCANScanRemainingMs(3000));

    g_test_now = 6001;                       // window elapsed
    updateCANScan();
    TEST_ASSERT_EQUAL_INT(SCAN_DONE, getCANScanState());
    TEST_ASSERT_EQUAL_UINT32(0, getCANScanRemainingMs(6001));
    canScanOnFrame(0x7E8, 0x0C, d, 1, 6002);
    getCANScanResults(&n);
    TEST_ASSERT_EQUAL_UINT8(0, n);          // done: not recorded
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_scan_status_json);
    RUN_TEST(test_scan_entry_json);
    RUN_TEST(test_bus_can_json);
    RUN_TEST(test_scan_counts_frames);
    RUN_TEST(test_scan_lifecycle);
    return UNITY_END();
}
