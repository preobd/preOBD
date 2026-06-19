// Native unit test: per-input alarm state machine (src/inputs/alarm_logic.cpp).
//
// updateInputAlarmState() takes `now` as a parameter (never calls millis()),
// so the whole INIT->WARMUP->READY->ACTIVE machine is deterministic on the
// host. We compile the implementation in and feed it synthetic Input structs.
#include <unity.h>
#include <Arduino.h>
#include <math.h>

#include "../../src/inputs/alarm_logic.cpp"

// alarm_logic.cpp references the global inputs[] array (via updateAllInputAlarms);
// provide the definition the linker expects.
Input inputs[MAX_INPUTS];

void setUp(void) {}
void tearDown(void) {}

// Fresh enabled+alarm-armed input with a high threshold at `maxV`.
// warmup=500ms, persist=200ms.
static Input makeInput(float maxV = 100.0f, float minV = -999.0f) {
    Input in{};
    in.flags.isEnabled = 1;
    in.flags.alarm = 1;
    in.maxValue = maxV;
    in.minValue = minV;
    in.value = 0.0f;
    initInputAlarmContext(&in, /*now*/ 0, /*warmup_ms*/ 500, /*persist_ms*/ 200);
    return in;
}

// Walk an input from INIT to READY with an in-range value.
static void toReady(Input& in) {
    in.value = 50.0f;                    // valid, within range
    updateInputAlarmState(&in, 0);       // INIT
    updateInputAlarmState(&in, 1001);    // INIT -> WARMUP (>1000ms)
    updateInputAlarmState(&in, 1602);    // WARMUP -> READY (601ms > 500ms warmup)
    TEST_ASSERT_EQUAL(ALARM_READY, in.alarmContext.state);
}

static void test_disabled_input_never_alarms(void) {
    Input in = makeInput();
    in.flags.alarm = 0;                  // alarm not armed
    in.value = 9999.0f;                  // way over threshold
    updateInputAlarmState(&in, 5000);
    TEST_ASSERT_FALSE(in.flags.isInAlarm);
    TEST_ASSERT_EQUAL(ALARM_DISABLED, in.alarmContext.state);
}

static void test_init_to_warmup_after_one_second(void) {
    Input in = makeInput();
    in.value = 50.0f;
    updateInputAlarmState(&in, 999);     // still inside 1s init window
    TEST_ASSERT_EQUAL(ALARM_INIT, in.alarmContext.state);
    updateInputAlarmState(&in, 1001);    // past it
    TEST_ASSERT_EQUAL(ALARM_WARMUP, in.alarmContext.state);
}

static void test_warmup_to_ready_after_warmup(void) {
    Input in = makeInput();
    in.value = 50.0f;
    updateInputAlarmState(&in, 1001);    // -> WARMUP, entry@1001
    updateInputAlarmState(&in, 1400);    // 399ms < 500ms warmup -> still WARMUP
    TEST_ASSERT_EQUAL(ALARM_WARMUP, in.alarmContext.state);
    updateInputAlarmState(&in, 1602);    // 601ms > 500ms -> READY
    TEST_ASSERT_EQUAL(ALARM_READY, in.alarmContext.state);
}

static void test_violation_must_persist_before_active(void) {
    Input in = makeInput(100.0f);
    toReady(in);

    in.value = 105.0f;                   // over threshold
    updateInputAlarmState(&in, 1602);    // fault timer starts; not yet persisted
    TEST_ASSERT_FALSE(in.flags.isInAlarm);
    TEST_ASSERT_EQUAL(ALARM_READY, in.alarmContext.state);

    updateInputAlarmState(&in, 1801);    // 199ms < 200ms persist -> still not active
    TEST_ASSERT_FALSE(in.flags.isInAlarm);

    updateInputAlarmState(&in, 1802);    // 200ms >= persist -> ACTIVE
    TEST_ASSERT_TRUE(in.flags.isInAlarm);
    TEST_ASSERT_EQUAL(ALARM_ACTIVE, in.alarmContext.state);
}

static void test_return_to_normal_clears_alarm(void) {
    Input in = makeInput(100.0f);
    toReady(in);
    in.value = 105.0f;
    updateInputAlarmState(&in, 1602);
    updateInputAlarmState(&in, 1802);    // ACTIVE
    TEST_ASSERT_TRUE(in.flags.isInAlarm);

    in.value = 50.0f;                    // back to normal
    updateInputAlarmState(&in, 1900);
    TEST_ASSERT_FALSE(in.flags.isInAlarm);
    TEST_ASSERT_EQUAL(ALARM_READY, in.alarmContext.state);
    TEST_ASSERT_EQUAL(0, in.alarmContext.faultStartTime);  // fault timer reset
}

static void test_nan_never_alarms(void) {
    Input in = makeInput(100.0f);
    toReady(in);
    in.value = NAN;                      // sensor fault
    updateInputAlarmState(&in, 1602);
    updateInputAlarmState(&in, 5000);    // long past any persist window
    TEST_ASSERT_FALSE(in.flags.isInAlarm);
    TEST_ASSERT_EQUAL(SEVERITY_NORMAL, in.currentSeverity);
}

static void test_warning_severity_at_ninety_percent(void) {
    Input in = makeInput(100.0f);        // warning band starts at 90
    toReady(in);
    in.value = 92.0f;                    // in warning band, below alarm threshold
    updateInputAlarmState(&in, 1602);
    TEST_ASSERT_FALSE(in.flags.isInAlarm);            // not a hard alarm
    TEST_ASSERT_EQUAL(SEVERITY_WARNING, in.currentSeverity);
}

static void test_alarm_severity_after_persist(void) {
    Input in = makeInput(100.0f);
    toReady(in);
    in.value = 105.0f;
    updateInputAlarmState(&in, 1602);                 // violation begins
    TEST_ASSERT_EQUAL(SEVERITY_WARNING, in.currentSeverity);  // warning during persist wait
    updateInputAlarmState(&in, 1802);                 // persisted
    TEST_ASSERT_EQUAL(SEVERITY_ALARM, in.currentSeverity);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_input_never_alarms);
    RUN_TEST(test_init_to_warmup_after_one_second);
    RUN_TEST(test_warmup_to_ready_after_warmup);
    RUN_TEST(test_violation_must_persist_before_active);
    RUN_TEST(test_return_to_normal_clears_alarm);
    RUN_TEST(test_nan_never_alarms);
    RUN_TEST(test_warning_severity_at_ninety_percent);
    RUN_TEST(test_alarm_severity_after_persist);
    return UNITY_END();
}
