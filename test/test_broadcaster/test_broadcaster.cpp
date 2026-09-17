// Native unit test: AlarmBroadcaster wire payload (src/outputs/ble/alarm_broadcaster.cpp).
//
// Alarms are now control-engine Rules (§11): the broadcaster scans
// rules[] and emits a PreobdAlarmEvent on each alarm-Rule
// severity transition. We capture the bytes handed to the transport and assert
// the PreobdAlarmEvent layout/values. Transport + control manager are faked.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "boot_order_fixture.h"  // inputsTableIsLiveForAssert() stub (#352, #369)
#include "../../src/outputs/ble/alarm_broadcaster.cpp"
#include "../../src/rules/rules_engine.cpp"   // real annunciator-severity logic

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
SystemConfig  systemConfig;
Rule          rules[MAX_RULES];      // standalone table (was systemConfig.rules[])
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
static uint8_t  g_ruleIds[MAX_RULES];  // ruleId of each frame, in emit order

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    g_frameType = type;
    g_frameLen  = len;
    if (len <= sizeof(g_framePayload)) memcpy(g_framePayload, payload, len);
    if (g_frameCount < (int)MAX_RULES && len == PREOBD_ALARM_EVENT_SIZE)
        g_ruleIds[g_frameCount] =
            reinterpret_cast<const PreobdAlarmEvent*>(payload)->ruleId;
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
    memset(g_ruleIds, 0, sizeof(g_ruleIds));
    memset(&systemConfig, 0, sizeof(systemConfig));
    memset(g_ruleContexts, 0, sizeof(g_ruleContexts));
    memset(&g_srcInput, 0, sizeof(g_srcInput));
    for (int i = 0; i < MAX_RULES; ++i) rules[i].id = RULES_ID_NONE;
    setInputsTableLive(true);  // every test but the boot-order one below
    AlarmBroadcaster::init();
}
void tearDown(void) {}

// Reproduce the real boot order (main.cpp: AlarmBroadcaster::init() runs
// alongside the other broadcasters, BEFORE initInputManager() loads rules[]):
// at init() time rules[] is still BSS-zeroed (every id == 0, a valid record
// id — RULES_ID_NONE is 0xFF) and the config load fills it afterwards
// (#352's twin for alarms; see boot_order_fixture.h / #369). init() must not
// read rules[] — it primes NORMAL/CLEAR unconditionally.
static void bootAsFirmwareDoes(void) {
    memset(rules, 0, sizeof(rules));             // BSS: every id == 0
    memset(g_ruleContexts, 0, sizeof(g_ruleContexts));
    setInputsTableLive(false);                   // table not ground truth yet (#369)
    AlarmBroadcaster::init();                    // …before the table exists
    setInputsTableLive(true);                    // initInputManager() flips this
    for (int i = 0; i < MAX_RULES; ++i) rules[i].id = RULES_ID_NONE;
    g_frameCount = 0;
}

// The pre-load table (rules[] BSS-zero) must never leak into a broadcast: no
// frame is due until a real rule transitions after boot completes.
static void test_boot_order_emits_nothing_before_config_loads(void) {
    bootAsFirmwareDoes();
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// Configure rule slot 0 as an OUTSIDE_BAND alarm and set its runtime active state.
static void configRule(uint8_t severity, float lo, float hi, float value, bool active) {
    Rule& r = rules[0];
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
    TEST_ASSERT_EQUAL_UINT8(1, e.ruleId);              // Rule.id set by configRule
    TEST_ASSERT_EQUAL_FLOAT(120.0f, e.value);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, e.threshold);      // limitHi
}

// #310: two rules on the SAME source input must be distinguishable on the wire
// by ruleId — inputIndex alone is identical for both, so keying firing state by
// inputIndex let one rule's CLEAR wipe the other. Both rules fire on one tick;
// assert the two frames carry distinct ruleIds.
static void test_two_rules_one_input_distinct_ruleid(void) {
    // Slot 0 (id 1) via configRule, plus slot 1 (id 2) on the same source input.
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    Rule& r2 = rules[1];
    r2.id = 2; r2.sourceInput = 21;  // same input as slot 0
    r2.condition = RULE_OUTSIDE_BAND; r2.severity = RULE_SEV_WARNING;
    r2.flags = RULE_FLAG_ENABLED; r2.limitLo = -1.0f; r2.limitHi = 100.0f;
    g_ruleContexts[1].state = RULE_ACTIVE;

    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(2, g_frameCount);            // one frame per rule
    TEST_ASSERT_EQUAL_UINT8(1, g_ruleIds[0]);
    TEST_ASSERT_EQUAL_UINT8(2, g_ruleIds[1]);
    TEST_ASSERT_NOT_EQUAL(g_ruleIds[0], g_ruleIds[1]); // distinguishable on the wire
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

// #425: a SET approach-tier edge can sit off the band midpoint. The OLD
// kindFor() picked HIGH/LOW purely by which half of the band the value was
// in (75 is above the [0,100] midpoint 50 -> HIGH), which is wrong once the
// per-side warn edges are explicit and asymmetric: 75 has crossed the lo
// warn edge (80) from below, not the hi one (90), so it must report LOW.
static void test_outside_band_set_warn_uses_correct_side(void) {
    configRule(RULE_SEV_ALARM, 0.0f, 100.0f, 75.0f, /*active*/false);
    Rule& r = rules[0];
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    ruleSetWarnMode(r, true, RULE_WARN_SET);
    r.warnLo = 80.0f;
    r.warnHi = 90.0f;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_WARNING, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_LOW, e.kind);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.threshold);  // limitLo, the LOW-side alarm limit
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
    RUN_TEST(test_two_rules_one_input_distinct_ruleid);
    RUN_TEST(test_low_side_kind_and_threshold);
    RUN_TEST(test_warning_severity);
    RUN_TEST(test_derived_warning_band);
    RUN_TEST(test_outside_band_set_warn_uses_correct_side);
    RUN_TEST(test_none_severity_rule_is_silent);
    RUN_TEST(test_return_to_normal_emits_clear);
    RUN_TEST(test_idempotent_tick);
    RUN_TEST(test_boot_order_emits_nothing_before_config_loads);
    return UNITY_END();
}
