// Native unit test: the warn-tier write paths on the per-input alarm facade
// (src/rules/rules_manager.cpp) — SET <pin> ALARM WARN's engine, and the
// limit/template-reseed paths that must DEGRADE a stranded SET side rather
// than reject the edit (#425, slice B, amendment A11).
//
// test_rules_engine already covers the pure predicates (rulesWarnSideValid,
// rulesNormalizeWarn, rulesRuleWarnEdge) against hand-built Rule literals.
// This suite is the layer above that: the REAL rules[] table plus the manager
// entry points a CLI/facade command actually calls — rulesSetAlarmRuleForInput,
// rulesSetAlarmWarnForInput, rulesRuleReset, rulesReseedRulesForInput — because
// that's where the "reject vs degrade" split (amendment A2) and the "no re-arm"
// contract (#295) actually live.
//
// The manager reaches for inputs, application presets, and messaging; those are
// stubbed here (minimal single-entry preset table), same shape as
// test_membership's stub set. -Wl,-dead_strip drops the paths this suite never
// calls (rulesActionsInit, rulesSyncActionPins, the pin registry, ...).
#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>

#include "../../src/rules/rules_engine.cpp"
#include "../../src/rules/rules_manager.cpp"

// ---- messaging: nothing here prints, but the manager links against msg ----
MessageAPI    msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
MessageRouter router;
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::warn(const char* tag, const char* fmt, ...) {
    (void)tag; (void)fmt; return 0;
}

// ---- stub inputs[] table: rules resolve their source by PIN ----------------
static Input   g_inputs[MAX_INPUTS];
static uint8_t g_inputCount = 0;

Input* getInputByPin(uint8_t pin) {
    for (uint8_t i = 0; i < g_inputCount; i++)
        if (g_inputs[i].pin == pin) return &g_inputs[i];
    return nullptr;
}

// Plain input, no application template (index 0xFF resolves to nullptr via
// getApplicationByIndex's bounds check) — exercises the "no app template,
// fall back to hand-entry defaults" path most facade calls take.
static void addPlainInput(uint8_t pin) {
    memset(&g_inputs[g_inputCount], 0, sizeof(Input));
    g_inputs[g_inputCount].pin = pin;
    g_inputs[g_inputCount].applicationIndex = 0xFF;
    g_inputCount++;
}

// Input wired to the one stub application preset below (index 0) — exercises
// RULE RESET / SET APPLICATION reseed, which need a real template.
static void addTemplatedInput(uint8_t pin) {
    memset(&g_inputs[g_inputCount], 0, sizeof(Input));
    g_inputs[g_inputCount].pin = pin;
    g_inputs[g_inputCount].applicationIndex = 0;
    g_inputCount++;
}

// ---- minimal APPLICATION_PRESETS[] stub ------------------------------------
// rules_manager.cpp's buildRuleConfigFromApplication() reads this through the
// real (inline, header-defined) getApplicationByIndex()/loadApplicationPreset()
// — only the backing PROGMEM array is firmware-external, so that's all this
// suite needs to supply. One entry, index 0, with a real [min,max] band and
// alarms enabled so RULE RESET / reseed have something to rewrite to.
static const char PSTR_APP_NAME[]  PROGMEM = "TEST_ALARM_APP";
static const char PSTR_APP_ABBR[]  PROGMEM = "TST";
static const char PSTR_APP_LABEL[] PROGMEM = "Test Alarm App";

const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    {
        PSTR_APP_NAME, PSTR_APP_ABBR, PSTR_APP_LABEL, nullptr,
        /*defaultSensor*/ 0, /*defaultUnits*/ 0,
        /*defaultMinValue*/ 10.0f, /*defaultMaxValue*/ 90.0f,
        /*obd2pid*/ 0, /*obd2length*/ 0,
        /*defaultAlarmEnabled*/ true, /*defaultDisplayEnabled*/ true,
        MEASURE_TEMPERATURE,
        /*nameHash*/ 0,
        /*warmupTime_ms*/ 1000, /*persistTime_ms*/ 500,
        APP_GROUP_OTHER,
    },
};

void setUp(void) {
    rulesResetTables();
    memset(g_inputs, 0, sizeof(g_inputs));
    g_inputCount = 0;
    g_test_now = 0;
}
void tearDown(void) {}

// =====================================================================
// rulesSetAlarmWarnForInput — accept / reject per side, no-rule case
// =====================================================================

static void test_warn_facade_no_alarm_rule_reports_badside_0xFF(void) {
    addPlainInput(10);
    uint8_t badSide = 0;
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 210.0f, RULE_WARN_AUTO, 0.0f, &badSide);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT8(0xFF, badSide);
}

static void test_warn_facade_accepts_legal_lo_on_above(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 230.0f, 0.0f, 0);  // ceiling at 230
    uint8_t badSide = 5;
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 210.0f, RULE_WARN_AUTO, 0.0f, &badSide);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(0xFF, badSide);  // reset to "no bad side" sentinel, then left alone on success
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(210.0f, rules[slot].warnLo);
}

static void test_warn_facade_rejects_illegal_lo_on_above(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 230.0f, 0.0f, 0);
    uint8_t badSide = 9;
    // 240 is ABOVE the 230 limit — illegal for an ABOVE rule's warn edge.
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 240.0f, RULE_WARN_AUTO, 0.0f, &badSide);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT8(0, badSide);
    // Rejected: the Rule is untouched (still AUTO).
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
}

static void test_warn_facade_rejects_hi_on_one_sided(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_BELOW, 5.0f, 0.0f, 0);  // floor, one-sided
    uint8_t badSide = 9;
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_AUTO, 0.0f, RULE_WARN_SET, 7.0f, &badSide);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT8(1, badSide);  // hi side named as the offender
}

static void test_warn_facade_accepts_both_sides_on_band(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0);
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 10.0f, RULE_WARN_SET, 90.0f, nullptr);
    TEST_ASSERT_TRUE(ok);
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, rules[slot].warnLo);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], true));
    TEST_ASSERT_EQUAL_FLOAT(90.0f, rules[slot].warnHi);
}

static void test_warn_facade_rejects_lo_ge_hi_on_band(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0);
    uint8_t badSide = 9;
    // Both in-band individually, but lo >= hi — the cross-check must reject.
    bool ok = rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 60.0f, RULE_WARN_SET, 40.0f, &badSide);
    TEST_ASSERT_FALSE(ok);
}

static void test_warn_facade_does_not_rearm(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 230.0f, 0.0f, 1000);
    int slot = rulesAlarmRuleSlotForInput(10);
    g_ruleContexts[slot].state = RULE_ACTIVE;  // pretend it has been live for a while
    rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 210.0f, RULE_WARN_AUTO, 0.0f, nullptr);
    TEST_ASSERT_EQUAL_UINT8(RULE_ACTIVE, g_ruleContexts[slot].state);  // untouched
}

// =====================================================================
// applyAlarmShape (via rulesSetAlarmRuleForInput) — degrade, not reject
// =====================================================================

static void test_shape_edit_degrades_crossed_set_side_reports_mask(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 230.0f, 0.0f, 0);
    rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 210.0f, RULE_WARN_AUTO, 0.0f, nullptr);
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));

    // Move the ceiling DOWN past the SET warn edge (230 -> 200; warn was 210).
    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 200.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0x01, warnReset & 0x01);  // lo side reported degraded
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnLo);
}

static void test_shape_edit_leaves_still_legal_set_side_alone(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 230.0f, 0.0f, 0);
    rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 210.0f, RULE_WARN_AUTO, 0.0f, nullptr);
    int slot = rulesAlarmRuleSlotForInput(10);

    // Move the ceiling UP — 210 is still legal (< 250).
    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(10, RULE_ABOVE, 250.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(210.0f, rules[slot].warnLo);
}

static void test_shape_change_band_to_one_sided_resets_hi_mode_and_zeroes(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0);
    rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 10.0f, RULE_WARN_SET, 90.0f, nullptr);
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], true));

    // Band -> one-sided (Min-only floor): the hi side no longer exists at all.
    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(10, RULE_BELOW, 10.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_TRUE((warnReset & 0x02) != 0);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnHi);
}

// The forcedMask case: a NONE hi side is NOT invalid to rulesNormalizeWarn()
// (only SET sides are validated), so without applyAlarmShape()'s explicit
// one-sided reset the discarded hi mode would vanish from the reported mask
// and the operator's "NONE" would silently become AUTO with no NOTE.
static void test_shape_change_band_to_one_sided_reports_discarded_none_hi(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0);
    rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 10.0f, RULE_WARN_NONE, 0.0f, nullptr);
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_NONE, ruleWarnMode(rules[slot], true));

    // Floor at 5: the SET lo edge (10) stays legal (> 5), so ONLY the hi bit
    // may be reported — proves the mask isn't just "something changed".
    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(10, RULE_BELOW, 5.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0x02, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnHi);
}

static void test_brand_new_alarm_rule_starts_auto_mask_zero(void) {
    addPlainInput(10);
    uint8_t warnReset = 9;
    uint8_t id = rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0, &warnReset);
    TEST_ASSERT_NOT_EQUAL(RULES_ID_NONE, id);
    TEST_ASSERT_EQUAL_UINT8(0, warnReset);
    int slot = rulesAlarmRuleSlotForInput(10);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
}

// =====================================================================
// RULE RESET / SET APPLICATION reseed — templates are AUTO, so both
// return an edited warn mode to AUTO (§11.5, amendment A7).
// =====================================================================

static void test_rule_reset_returns_warn_to_auto(void) {
    addTemplatedInput(20);
    uint8_t id = rulesSeedRuleForInput(20, 0, 0);
    TEST_ASSERT_NOT_EQUAL(RULES_ID_NONE, id);
    int slot = rulesRuleSlot(id);
    ruleSetWarnMode(rules[slot], false, RULE_WARN_SET);
    rules[slot].warnLo = 15.0f;
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));

    TEST_ASSERT_TRUE(rulesRuleReset(id, 0));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnLo);
}

static void test_reseed_for_input_returns_warn_to_auto(void) {
    addTemplatedInput(20);
    uint8_t id = rulesSeedRuleForInput(20, 0, 0);
    int slot = rulesRuleSlot(id);
    ruleSetWarnMode(rules[slot], true, RULE_WARN_SET);
    rules[slot].warnHi = 85.0f;

    TEST_ASSERT_EQUAL_UINT8(1, rulesReseedRulesForInput(20, 0));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnHi);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_warn_facade_no_alarm_rule_reports_badside_0xFF);
    RUN_TEST(test_warn_facade_accepts_legal_lo_on_above);
    RUN_TEST(test_warn_facade_rejects_illegal_lo_on_above);
    RUN_TEST(test_warn_facade_rejects_hi_on_one_sided);
    RUN_TEST(test_warn_facade_accepts_both_sides_on_band);
    RUN_TEST(test_warn_facade_rejects_lo_ge_hi_on_band);
    RUN_TEST(test_warn_facade_does_not_rearm);
    RUN_TEST(test_shape_edit_degrades_crossed_set_side_reports_mask);
    RUN_TEST(test_shape_edit_leaves_still_legal_set_side_alone);
    RUN_TEST(test_shape_change_band_to_one_sided_resets_hi_mode_and_zeroes);
    RUN_TEST(test_shape_change_band_to_one_sided_reports_discarded_none_hi);
    RUN_TEST(test_brand_new_alarm_rule_starts_auto_mask_zero);
    RUN_TEST(test_rule_reset_returns_warn_to_auto);
    RUN_TEST(test_reseed_for_input_returns_warn_to_auto);
    return UNITY_END();
}
