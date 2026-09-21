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

// One synthetic source input; getInputByPin returns it, getInputIndex is fixed
// — except when g_orphanPin flips them to "no input configured on this pin"
// (getInputByPin -> nullptr, getInputIndex -> 0xFF), simulating CLEAR <pin>
// on a rule's source (#443 review item 2 / #345 parity).
static Input g_srcInput;
static bool  g_orphanPin = false;
Input*  getInputByPin(uint8_t)  { return g_orphanPin ? nullptr : &g_srcInput; }
uint8_t getInputIndex(uint8_t)  { return g_orphanPin ? 0xFF : 3; }

// ---- Capturing transport ----
static int      g_frameCount = 0;
static uint8_t  g_frameType  = 0;
static uint16_t g_frameLen   = 0;
static uint8_t  g_framePayload[64];
static uint8_t  g_ruleIds[MAX_RULES];  // ruleId of each frame, in emit order

// Full (severity, kind) history of every accepted frame, in emit order — the
// #443-review FAULT tests drive the real engine tick-by-tick and need to
// assert "no CLEAR was ever emitted along the way", not just inspect the
// most recent frame.
static uint8_t g_histSeverity[512];
static uint8_t g_histKind[512];
static int     g_histCount = 0;

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    g_frameType = type;
    g_frameLen  = len;
    if (len <= sizeof(g_framePayload)) memcpy(g_framePayload, payload, len);
    if (g_frameCount < (int)MAX_RULES && len == PREOBD_ALARM_EVENT_SIZE)
        g_ruleIds[g_frameCount] =
            reinterpret_cast<const PreobdAlarmEvent*>(payload)->ruleId;
    if (len == PREOBD_ALARM_EVENT_SIZE && g_histCount < (int)(sizeof(g_histSeverity))) {
        const PreobdAlarmEvent* ev = reinterpret_cast<const PreobdAlarmEvent*>(payload);
        g_histSeverity[g_histCount] = ev->severity;
        g_histKind[g_histCount]     = ev->kind;
        ++g_histCount;
    }
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
    g_histCount = 0;
    g_orphanPin = false;
    g_test_now = 0;
    memset(g_framePayload, 0, sizeof(g_framePayload));
    memset(g_ruleIds, 0, sizeof(g_ruleIds));
    memset(g_histSeverity, 0, sizeof(g_histSeverity));
    memset(g_histKind, 0, sizeof(g_histKind));
    memset(&systemConfig, 0, sizeof(systemConfig));
    memset(g_ruleContexts, 0, sizeof(g_ruleContexts));
    memset(&g_srcInput, 0, sizeof(g_srcInput));
    memset(rules, 0, sizeof(rules));
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

// #429: RULE DEL / CLEAR <pin> / ALARM DISABLE zero the slot (id -> 0xFF)
// while the client still holds the rule as firing. The CLEAR must name the
// rule the client is holding, not the emptied slot — a CLEAR for ruleId 0xFF
// is a no-op delete in every client and the alarm stays "firing" forever.
static void test_clear_after_rule_delete_carries_last_announced_identity(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();                 // ALARM, ruleId 1, inputIndex 3
    g_frameCount = 0;
    memset(&rules[0], 0, sizeof(Rule));       // RULE DEL: slot zeroed…
    rules[0].id = RULES_ID_NONE;              // …and vacated
    g_ruleContexts[0].state = RULE_DISABLED;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.ruleId);      // the rule the client holds
    TEST_ASSERT_EQUAL_UINT8(3, e.inputIndex);  // not 0xFF
    // And the emptied slot then stays quiet — no second CLEAR for 0xFF.
    g_frameCount = 0;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// A quiet rule that is deleted has nothing to clear: no frame at all.
static void test_delete_of_quiet_rule_emits_nothing(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 50.0f, /*active*/false);
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
    memset(&rules[0], 0, sizeof(Rule)); rules[0].id = RULES_ID_NONE;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// Slot reused by a DIFFERENT rule that is itself firing before the next tick:
// the old identity is cleared first, then the new one announced — the client
// never sees rule 1's entry silently become rule 2's.
static void test_replaced_rule_in_slot_clears_old_then_announces_new(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();                 // ALARM ruleId 1
    g_frameCount = 0;
    rules[0].id = 2;                          // same slot, new record, still firing
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, lastEvent().kind);
    TEST_ASSERT_EQUAL_UINT8(1, lastEvent().ruleId);
    g_frameCount = 0;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, lastEvent().severity);
    TEST_ASSERT_EQUAL_UINT8(2, lastEvent().ruleId);
    g_frameCount = 0;
    AlarmBroadcaster::tick();                 // settled
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// ---- #443: FAULT (NAN source, INFO tier) --------------------------------

// An ACTIVE alarm whose source goes NAN and stays NAN past the debounce
// announces FAULT/INFO — NOT a CLEAR (an ACTIVE rule going NAN would
// otherwise de-assert and look like "all clear" for a dead sensor).
static void test_active_rule_nan_past_debounce_emits_fault(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    g_test_now = 1000;
    AlarmBroadcaster::tick();                 // establish held ALARM
    g_frameCount = 0;

    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_INFO, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.ruleId);
    TEST_ASSERT_EQUAL_UINT8(3, e.inputIndex);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.value);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.threshold);
    TEST_ASSERT_NOT_EQUAL(PREOBD_ALARM_KIND_CLEAR, e.kind);
}

// Same, but still inside the debounce window: the broadcaster now HOLDS the
// last accepted key (ALARM) instead of emitting whatever evalSlot() would
// say right now — holdAcrossFaultTransition()'s Window 1 (#443 review):
// ctx.nanSince is stamped but rulesRuleSourceFaulted() isn't true yet, so the
// slot holds until either the debounce elapses (-> FAULT) or the reading
// returns. See test_fault_a_no_clear_before_debounce_then_one_fault() below
// for the same window driven through the REAL rulesRuleUpdate() state
// machine tick-by-tick, which is what motivated the hold in the first place
// (rulesManagerUpdate() de-asserts an ACTIVE rule to READY on the very FIRST
// NaN tick, long before the FAULT debounce elapses).
static void test_active_rule_nan_within_debounce_no_frame(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    g_test_now = 1000;
    AlarmBroadcaster::tick();                 // establish held ALARM
    g_frameCount = 0;

    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS - 1;  // not yet debounced
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// A READY (not yet active) rule whose source is NAN past the debounce also
// announces FAULT/INFO.
static void test_ready_rule_nan_emits_fault(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 50.0f, /*active*/false);  // READY
    g_test_now = 1000;
    AlarmBroadcaster::tick();                 // quiet
    g_frameCount = 0;

    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_INFO, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, e.kind);
}

// The reading returning while the rule is READY reports the truthful key
// (NORMAL/CLEAR), not FAULT.
static void test_reading_returns_ready_emits_clear(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 50.0f, /*active*/false);
    g_test_now = 1000;
    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();                 // FAULT held
    g_frameCount = 0;

    g_srcInput.value = 50.0f;                 // reading restored
    g_ruleContexts[0].nanSince = 0;
    g_ruleContexts[0].state = RULE_READY;
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, e.kind);
}

// The reading returning while the rule is (still/again) ACTIVE reports ALARM
// again, not a lingering FAULT.
static void test_reading_returns_active_emits_alarm_again(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    g_test_now = 1000;
    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();                 // FAULT held
    g_frameCount = 0;

    g_srcInput.value = 120.0f;                // reading restored, still tripped
    g_ruleContexts[0].nanSince = 0;
    g_ruleContexts[0].state = RULE_ACTIVE;
    AlarmBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, e.kind);
}

// A NONE-severity control rule (drives Actions only, never notifies) stays
// silent even while its source is NAN past the debounce.
static void test_none_severity_rule_nan_is_silent(void) {
    configRule(RULE_SEV_NONE, -1.0f, 100.0f, 120.0f, /*active*/true);
    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// A rule still in RULE_INIT never reports FAULT even with nanSince stamped
// and the debounce elapsed — matches rulesRuleSourceFaulted's INIT exclusion.
static void test_rule_init_state_nan_past_debounce_is_silent(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/false);
    g_ruleContexts[0].state = RULE_INIT;
    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// ---- #443 review: real-engine-driven FAULT transitions ------------------
//
// The tests above pin ctx.state/ctx.nanSince by hand, which cannot exercise
// holdAcrossFaultTransition() honestly: rulesManagerUpdate() runs every
// 50ms in the firmware and de-asserts an ACTIVE rule to READY on the FIRST
// NaN tick (NaN never asserts, §11.3) — long before the 2s FAULT debounce
// elapses. Without the hold, that produces the exact bug the review caught:
// ALARM -> CLEAR (<=50ms) -> FAULT (+2s) instead of ALARM -> FAULT. These
// tests drive the REAL rulesRuleUpdate() state machine tick-by-tick (the
// harness includes rules_engine.cpp) to prove the hold actually closes that
// window, not just that the broadcaster's key math is internally consistent.

// Fully-specified Rule for these tests: unlike configRule() above (which
// only sets what the older pinned-state tests need), driving the REAL state
// machine means every timing field it reads — hysteresis, warmupMs,
// debounceMs — must be explicit rather than left over from a previous test.
static void configRealRule(uint8_t severity, float lo, float hi,
                            uint16_t warmupMs = 500, uint16_t debounceMs = 200,
                            float hysteresis = 2.0f) {
    Rule& r = rules[0];
    memset(&r, 0, sizeof(Rule));
    r.id = 1; r.sourceInput = 21; r.condition = RULE_OUTSIDE_BAND;
    r.severity = severity; r.flags = RULE_FLAG_ENABLED;
    r.limitLo = lo; r.limitHi = hi; r.hysteresis = hysteresis;
    r.warmupMs = warmupMs; r.debounceMs = debounceMs;
}

// Advance g_test_now by stepMs, run the REAL rulesRuleUpdate() against the
// CURRENT g_srcInput.value (mirroring rulesManagerUpdate()'s per-loop-pass
// cadence), then run one broadcaster tick. This is the driver every test in
// this section uses instead of pinning ctx.state/ctx.nanSince by hand.
static void stepEngine(uint32_t stepMs = 50) {
    g_test_now += stepMs;
    rulesRuleUpdate(rules[0], g_ruleContexts[0], g_srcInput.value, g_test_now);
    AlarmBroadcaster::tick();
}

// Arm rule 0 and drive it INIT -> WARMUP -> READY with a settled, in-band
// reading, using the real engine — the same sequence rules_manager.cpp's
// boot-load path runs.
static void armRuleToReady(float settledValue) {
    g_srcInput.value = settledValue;
    rulesRuleInit(g_ruleContexts[0], rules[0], g_test_now);
    stepEngine(RULES_INIT_MS + 1);              // INIT -> WARMUP
    stepEngine(rules[0].warmupMs + 1);          // WARMUP -> READY
    TEST_ASSERT_EQUAL(RULE_READY, g_ruleContexts[0].state);
}

// (a) An ACTIVE alarm rule, real-armed and real-tripped, whose source then
// goes NAN: no frame at all until the FAULT debounce elapses, then exactly
// one INFO/FAULT — and no CLEAR is emitted anywhere along the way.
static void test_fault_a_no_clear_before_debounce_then_one_fault(void) {
    configRealRule(RULE_SEV_ALARM, -1.0f, 100.0f);
    armRuleToReady(50.0f);

    g_srcInput.value = 120.0f;                  // trip
    stepEngine(10);                             // begin assertion (faultStartTime set)
    stepEngine(rules[0].debounceMs + 1);        // cross the persist debounce -> ACTIVE
    TEST_ASSERT_EQUAL(RULE_ACTIVE, g_ruleContexts[0].state);
    TEST_ASSERT_TRUE(g_histCount >= 1);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, g_histSeverity[g_histCount - 1]);
    const int histBefore = g_histCount;

    g_srcInput.value = NAN;                     // source dies
    for (uint32_t t = 0; t < RULES_FAULT_DEBOUNCE_MS + 100 && g_histCount == histBefore; t += 50) {
        stepEngine(50);
    }

    TEST_ASSERT_EQUAL_INT(histBefore + 1, g_histCount);   // exactly one new frame
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_INFO, g_histSeverity[g_histCount - 1]);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, g_histKind[g_histCount - 1]);
    for (int i = 0; i < g_histCount; ++i) {
        const bool isClear = g_histSeverity[i] == PREOBD_ALARM_SEVERITY_NORMAL &&
                              g_histKind[i] == PREOBD_ALARM_KIND_CLEAR;
        TEST_ASSERT_FALSE(isClear);             // never a misleading CLEAR
    }
}

// Shared setup for (b)/(c): real-arm, real-trip to ALARM, then drive the
// source to NAN until the real engine (via the broadcaster's hold) reports
// FAULT.
static void armRuleActiveThenFault(void) {
    configRealRule(RULE_SEV_ALARM, -1.0f, 100.0f);
    armRuleToReady(50.0f);
    g_srcInput.value = 120.0f;
    stepEngine(10);                             // begin assertion (faultStartTime set)
    stepEngine(rules[0].debounceMs + 1);        // cross the persist debounce -> ACTIVE
    TEST_ASSERT_EQUAL(RULE_ACTIVE, g_ruleContexts[0].state);

    g_srcInput.value = NAN;
    for (uint32_t t = 0; t < RULES_FAULT_DEBOUNCE_MS + 100; t += 50) stepEngine(50);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, lastEvent().kind);
}

// (b) From a held FAULT, the reading returns still-tripped: no CLEAR while
// the re-assertion re-debounces (Window 2), then exactly one ALARM once the
// persist debounce elapses.
static void test_fault_b_reading_returns_tripped_then_alarm(void) {
    armRuleActiveThenFault();
    const int histBefore = g_histCount;

    g_srcInput.value = 130.0f;                  // reading restored, still tripped
    for (uint32_t t = 0; t <= rules[0].debounceMs + 50; t += 50) stepEngine(50);

    TEST_ASSERT_EQUAL_INT(histBefore + 1, g_histCount);   // exactly one new frame
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, g_histSeverity[g_histCount - 1]);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, g_histKind[g_histCount - 1]);
}

// (c) From a held FAULT, the reading returns NORMAL (not tripped): one CLEAR,
// immediately — no re-debounce window applies since the rule never re-asserts.
static void test_fault_c_reading_returns_normal_then_clear(void) {
    armRuleActiveThenFault();
    const int histBefore = g_histCount;

    g_srcInput.value = 50.0f;                   // reading restored, well in-band
    stepEngine(50);

    TEST_ASSERT_EQUAL_INT(histBefore + 1, g_histCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, g_histSeverity[g_histCount - 1]);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, g_histKind[g_histCount - 1]);
}

// (d) An orphan rule (no Input configured on the source pin — CLEAR <pin>
// parity, #345) whose source reads NAN forever never announces a fault: no
// frame at all, ever. Mirrors sourceFaultedForRuleSlot()'s INERT treatment.
static void test_fault_d_orphan_rule_never_faults(void) {
    configRealRule(RULE_SEV_ALARM, -1.0f, 100.0f);
    g_orphanPin = true;
    rulesRuleInit(g_ruleContexts[0], rules[0], g_test_now);
    g_srcInput.value = NAN;
    for (uint32_t t = 0; t < RULES_FAULT_DEBOUNCE_MS * 3; t += 50) stepEngine(50);
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// Bonus (#443 review item 2, "confirm what happens instead"): CLEAR <pin> on
// a LIVE faulted input removes the input but leaves the rule record in
// place, so nextKey()'s replaced/deleted-identity check (keyed on ruleId)
// never fires — the in!=nullptr gate is what turns this into one truthful
// CLEAR (inputIndex 0xFF, the input is genuinely gone) instead of a
// permanent FAULT frozen at the stale inputIndex.
static void test_fault_orphaned_mid_fault_clears_not_stuck_faulted(void) {
    armRuleActiveThenFault();
    const int histBefore = g_histCount;

    g_orphanPin = true;                         // CLEAR <pin>: input gone, rule stays
    stepEngine(50);
    TEST_ASSERT_EQUAL_INT(histBefore + 1, g_histCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_NORMAL, g_histSeverity[g_histCount - 1]);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, g_histKind[g_histCount - 1]);

    const int afterClear = g_histCount;
    stepEngine(50);                             // stays quiet: no repeat, no re-FAULT
    TEST_ASSERT_EQUAL_INT(afterClear, g_histCount);
}

// (e) WARMUP is warmup-aware: a rule with a 30s warmup (a CAN/OBD-II PID
// that reads NaN until its first frame) does not FAULT at the ordinary 2s —
// only past its own warmupMs.
static void test_fault_e_warmup_uses_warmup_debounce(void) {
    configRealRule(RULE_SEV_ALARM, -1.0f, 100.0f, /*warmupMs*/30000);
    g_srcInput.value = NAN;                     // dead from boot
    rulesRuleInit(g_ruleContexts[0], rules[0], g_test_now);
    stepEngine(RULES_INIT_MS + 1);              // INIT -> WARMUP
    TEST_ASSERT_EQUAL(RULE_WARMUP, g_ruleContexts[0].state);

    for (uint32_t t = 0; t < 5000; t += 50) stepEngine(50);
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);     // 5s in: still nothing

    for (uint32_t t = 5000; t < 30000 + 100; t += 50) stepEngine(50);
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);     // past 30s: exactly one FAULT
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_INFO, lastEvent().severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, lastEvent().kind);
}

// #430 resync: a held FAULT is re-announced to a newly (re)connected central,
// same as a held WARNING/ALARM.
static void test_resync_reannounces_held_fault(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    g_srcInput.value = NAN;
    g_ruleContexts[0].nanSince = 1000;
    g_test_now = 1000 + RULES_FAULT_DEBOUNCE_MS;
    AlarmBroadcaster::tick();                 // FAULT sent
    g_frameCount = 0;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);   // steady state: nothing

    AlarmBroadcaster::resync();
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_INFO, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_FAULT, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.ruleId);
}

// #430: no heartbeat, so a central that connects while a rule is held would
// see "all normal" until the next transition. resync() (the mux PING edge —
// which the nRF raises on the 0->1 central transition, i.e. connect or
// reconnect after a drop) re-announces held slots only.
static void test_resync_reannounces_held_alarm(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();                 // ALARM sent to the first central
    g_frameCount = 0;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);   // steady state: nothing
    AlarmBroadcaster::resync();               // a central (re)connects: PING
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);   // exactly the held slot, not MAX_RULES
    const PreobdAlarmEvent& e = lastEvent();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_SEVERITY_ALARM, e.severity);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_HIGH, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.ruleId);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, e.value);
}

static void test_resync_with_nothing_held_is_silent(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 50.0f, /*active*/false);
    AlarmBroadcaster::tick();
    AlarmBroadcaster::resync();
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);   // no CLEAR flood for a quiet table
}

// resync racing a delete: the held slot is invalidated, but by the tick the
// rule is gone. No anonymous (0xFF) frame — the correct CLEAR still goes out
// because the last-announced identity is what the slot clears against.
static void test_resync_then_delete_still_clears_by_identity(void) {
    configRule(RULE_SEV_ALARM, -1.0f, 100.0f, 120.0f, /*active*/true);
    AlarmBroadcaster::tick();
    g_frameCount = 0;
    AlarmBroadcaster::resync();
    memset(&rules[0], 0, sizeof(Rule)); rules[0].id = RULES_ID_NONE;
    AlarmBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ALARM_KIND_CLEAR, lastEvent().kind);
    TEST_ASSERT_EQUAL_UINT8(1, lastEvent().ruleId);
    TEST_ASSERT_NOT_EQUAL(0xFF, lastEvent().ruleId);
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
    RUN_TEST(test_clear_after_rule_delete_carries_last_announced_identity);
    RUN_TEST(test_delete_of_quiet_rule_emits_nothing);
    RUN_TEST(test_replaced_rule_in_slot_clears_old_then_announces_new);
    RUN_TEST(test_resync_reannounces_held_alarm);
    RUN_TEST(test_resync_with_nothing_held_is_silent);
    RUN_TEST(test_resync_then_delete_still_clears_by_identity);
    RUN_TEST(test_boot_order_emits_nothing_before_config_loads);
    RUN_TEST(test_active_rule_nan_past_debounce_emits_fault);
    RUN_TEST(test_active_rule_nan_within_debounce_no_frame);
    RUN_TEST(test_ready_rule_nan_emits_fault);
    RUN_TEST(test_reading_returns_ready_emits_clear);
    RUN_TEST(test_reading_returns_active_emits_alarm_again);
    RUN_TEST(test_none_severity_rule_nan_is_silent);
    RUN_TEST(test_rule_init_state_nan_past_debounce_is_silent);
    RUN_TEST(test_fault_a_no_clear_before_debounce_then_one_fault);
    RUN_TEST(test_fault_b_reading_returns_tripped_then_alarm);
    RUN_TEST(test_fault_c_reading_returns_normal_then_clear);
    RUN_TEST(test_fault_d_orphan_rule_never_faults);
    RUN_TEST(test_fault_orphaned_mid_fault_clears_not_stuck_faulted);
    RUN_TEST(test_fault_e_warmup_uses_warmup_debounce);
    RUN_TEST(test_resync_reannounces_held_fault);
    return UNITY_END();
}
