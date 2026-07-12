// Native unit test: AlarmBroadcaster wire payload (src/lib/alarm_broadcaster.cpp).
//
// Alarms are now control-engine Rules (§11): the broadcaster scans
// systemConfig.rules[] and emits a PreobdAlarmEvent on each alarm-Rule
// severity transition. We capture the bytes handed to the transport and assert
// the PreobdAlarmEvent layout/values. Transport + control manager are faked.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/alarm_broadcaster.cpp"
#include "../../src/control/control_engine.cpp"   // real annunciator-severity logic

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
SystemConfig  systemConfig;
RuleContext   g_ruleContexts[MAX_RULES];

// One synthetic source input; getInputByPin returns it, getInputIndex is fixed.
static Input g_srcInput;
Input*  getInputByPin(uint8_t)  { return &g_srcInput; }
uint8_t getInputIndex(uint8_t)  { return 3; }  // arbitrary stable index

// ---- Capturing transport ----
static int      g_frameCount = 0;
static uint8_t  g_frameType  = 0;
static uint16_t g_frameLen   = 0;
static uint8_t  g_framePayload[64];

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    g_frameType = type;
    g_frameLen  = len;
    if (len <= sizeof(g_framePayload)) memcpy(g_framePayload, payload, len);
    ++g_frameCount;
    return true;
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

static const PreobdAlarmEvent& lastEvent() {
    return *reinterpret_cast<const PreobdAlarmEvent*>(g_framePayload);
}

void setUp(void) {
    g_frameCount = 0; g_frameType = 0; g_frameLen = 0;
    memset(g_framePayload, 0, sizeof(g_framePayload));
    memset(&systemConfig, 0, sizeof(systemConfig));
    memset(g_ruleContexts, 0, sizeof(g_ruleContexts));
    memset(&g_srcInput, 0, sizeof(g_srcInput));
    for (int i = 0; i < MAX_RULES; ++i) systemConfig.rules[i].id = CONTROL_ID_NONE;
    AlarmBroadcaster::init();
}
void tearDown(void) {}

// Configure rule slot 0 as an OUTSIDE_BAND alarm and set its runtime active state.
static void configRule(uint8_t severity, float lo, float hi, float value, bool active) {
    Rule& r = systemConfig.rules[0];
    r.id = 1; r.sourceInput = 21; r.condition = RULE_OUTSIDE_BAND;
    r.severity = severity; r.flags = RULE_FLAG_ENABLED;
    r.limitLo = lo; r.limitHi = hi;
    g_srcInput.value = value;
    g_ruleContexts[0].state = active ? RULE_ACTIVE : RULE_READY;
}

static void test_inactive_rule_emits_nothing(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 50.0f, /*active*/false);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

static void test_active_alarm_high_payload(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_ALARM, g_frameType);
    TEST_ASSERT_EQUAL_UINT16(PREOBD_ALARM_EVENT_SIZE, g_frameLen);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, e.kind);
    TEST_ASSERT_EQUAL_UINT8(3, e.inputIndex);          // getInputIndex stub
    TEST_ASSERT_EQUAL_FLOAT(120.0f, e.value);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, e.threshold);      // limitHi
}

static void test_low_side_kind_and_threshold(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, -5.0f, /*active*/true);
    AlarmBroadcaster::tick();
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_LOW, e.kind);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, e.threshold);       // limitLo
}

static void test_warning_severity(void) {
    configRule(RULE_SEV_WARNING, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_WARNING, lastEvent().severity);
}

// Derived WARNING tier (§11.2): an armed (READY, not active) ALARM rule whose
// value is in the warning band emits a WARNING event before it trips.
static void test_derived_warning_band(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 95.0f, /*active*/false);  // 95 >= 90% of 100
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_WARNING, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, e.kind);
}

static void test_none_severity_rule_is_silent(void) {
    // A control Rule (severity NONE) is not an alarm — never broadcasts.
    configRule(RULE_SEV_NONE, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

static void test_return_to_normal_emits_clear(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();                 // ALARM
    g_frameCount = 0;
    g_ruleContexts[0].state = RULE_READY;     // rule clears
    g_srcInput.value = 50.0f;
    AlarmBroadcaster::tick();                 // CLEAR
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, e.kind);
}

static void test_idempotent_tick(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();
    g_frameCount = 0;
    AlarmBroadcaster::tick();                 // no transition
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_inactive_rule_emits_nothing);
    RUN_TEST(test_active_alarm_high_payload);
    RUN_TEST(test_low_side_kind_and_threshold);
    RUN_TEST(test_warning_severity);
    RUN_TEST(test_derived_warning_band);
    RUN_TEST(test_none_severity_rule_is_silent);
    RUN_TEST(test_return_to_normal_emits_clear);
    RUN_TEST(test_idempotent_tick);
    return UNITY_END();
}
