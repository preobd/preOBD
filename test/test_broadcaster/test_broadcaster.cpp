// Native unit test: AlarmBroadcaster wire payload (src/lib/alarm_broadcaster.cpp).
//
// This is the automatable half of issue #221: instead of subscribing to the
// BLE characteristic from nRF Connect, we capture the exact bytes the
// broadcaster hands to the transport and assert the PreobdAlarmEvent layout
// and values. The transport boundary (MuxTransport::sendFrame) is the only
// thing we fake — its real definition lives in transport_mux.cpp, which we do
// NOT compile, so our capturing definition below satisfies the linker.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/alarm_broadcaster.cpp"

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
Input inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;

// ---- Capturing transport ----
// One slot is enough; tests assert on the most-recent frame plus a count.
static int      g_frameCount = 0;
static uint8_t  g_frameType  = 0;
static uint16_t g_frameLen   = 0;
static uint8_t  g_framePayload[64];

// Minimal out-of-line virtuals so MuxTransport is a concrete, vtable-complete
// type we can instantiate. Only sendFrame does anything.
bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    g_frameType = type;
    g_frameLen  = len;
    if (len <= sizeof(g_framePayload)) memcpy(g_framePayload, payload, len);
    ++g_frameCount;
    return true;  // pretend framed mode is up and the TX buffer accepted it
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}

// The broadcaster references this exact symbol via `extern MuxTransport hwSerial1;`
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

static const PreobdAlarmEvent& lastEvent() {
    return *reinterpret_cast<const PreobdAlarmEvent*>(g_framePayload);
}

void setUp(void) {
    g_frameCount = 0;
    g_frameType  = 0;
    g_frameLen   = 0;
    memset(g_framePayload, 0, sizeof(g_framePayload));
    memset(inputs, 0, sizeof(inputs));
    numActiveInputs = 0;
    AlarmBroadcaster::init();  // reset severity/kind cache + sequence
}
void tearDown(void) {}

// Arm input 0 with a high-side threshold; severity is set directly so this
// suite is decoupled from the alarm state machine (covered by test_alarm).
static void armHighInput(uint8_t idx, float maxV, float value, AlarmSeverity sev) {
    inputs[idx].flags.isEnabled = 1;
    inputs[idx].maxValue = maxV;
    inputs[idx].minValue = -999.0f;  // low side disabled
    inputs[idx].value = value;
    inputs[idx].currentSeverity = sev;
    if (idx + 1 > numActiveInputs) numActiveInputs = idx + 1;
}

// Severity unchanged from the post-init NORMAL baseline => nothing on the wire.
static void test_normal_input_emits_nothing(void) {
    armHighInput(0, 100.0f, 50.0f, SEVERITY_NORMAL);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// A high-side alarm produces one type-0x04 frame with the full 18-byte event.
static void test_alarm_emits_correct_payload(void) {
    armHighInput(0, 100.0f, 105.0f, SEVERITY_ALARM);
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_ALARM, g_frameType);
    TEST_ASSERT_EQUAL_UINT16(PREOBD_ALARM_EVENT_SIZE, g_frameLen);  // 18

    const PreobdAlarmEvent& ev = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, ev.protocolVersion);
    TEST_ASSERT_EQUAL_UINT8(0, ev.inputIndex);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, ev.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, ev.kind);
    TEST_ASSERT_EQUAL_UINT16(0, ev.sequence);          // first event
    TEST_ASSERT_EQUAL_FLOAT(105.0f, ev.value);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, ev.threshold);     // the maxValue violated
}

// Dropping back to NORMAL emits a return-to-normal (CLEAR) event, sequence++.
static void test_return_to_normal_emits_clear(void) {
    armHighInput(0, 100.0f, 105.0f, SEVERITY_ALARM);
    AlarmBroadcaster::tick();                            // ALARM, seq 0
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);

    inputs[0].value = 50.0f;
    inputs[0].currentSeverity = SEVERITY_NORMAL;
    AlarmBroadcaster::tick();                            // CLEAR, seq 1

    TEST_ASSERT_EQUAL_INT(2, g_frameCount);
    const PreobdAlarmEvent& ev = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, ev.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, ev.kind);
    TEST_ASSERT_EQUAL_UINT16(1, ev.sequence);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ev.threshold);        // 0 on CLEAR
}

// No state change across ticks => no duplicate frames (cache works).
static void test_idempotent_tick(void) {
    armHighInput(0, 100.0f, 105.0f, SEVERITY_ALARM);
    AlarmBroadcaster::tick();
    AlarmBroadcaster::tick();
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
}

// Low-side alarm reports KIND_LOW and the minValue threshold.
static void test_low_side_alarm_kind_and_threshold(void) {
    inputs[0].flags.isEnabled = 1;
    inputs[0].maxValue = 999.0f;   // high side disabled
    inputs[0].minValue = 10.0f;
    inputs[0].value = 5.0f;
    inputs[0].currentSeverity = SEVERITY_ALARM;
    numActiveInputs = 1;

    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& ev = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_LOW, ev.kind);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, ev.threshold);
}

// Two inputs tripping in one tick get strictly increasing sequence numbers.
static void test_sequence_monotonic_across_inputs(void) {
    armHighInput(0, 100.0f, 105.0f, SEVERITY_ALARM);
    armHighInput(1, 100.0f, 95.0f,  SEVERITY_WARNING);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(2, g_frameCount);
    // Last frame is input 1; it must carry the higher sequence number.
    const PreobdAlarmEvent& ev = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(1, ev.inputIndex);
    TEST_ASSERT_EQUAL_UINT16(1, ev.sequence);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_input_emits_nothing);
    RUN_TEST(test_alarm_emits_correct_payload);
    RUN_TEST(test_return_to_normal_emits_clear);
    RUN_TEST(test_idempotent_tick);
    RUN_TEST(test_low_side_alarm_kind_and_threshold);
    RUN_TEST(test_sequence_monotonic_across_inputs);
    return UNITY_END();
}
