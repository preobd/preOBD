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
    // Reported as the MAX (ceiling) side: on an ABOVE rule the lo slot IS the
    // ceiling's edge, and the NOTE must name the side the operator set (#427).
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_RESET_MAX, warnReset);
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
    TEST_ASSERT_TRUE((warnReset & RULE_WARN_DROPPED_MAX) != 0);
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
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_DROPPED_MAX, warnReset);
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
// #427 — warn slots are remapped by MEANING across a shape change. The lo
// slot is the CEILING's edge on ABOVE but the FLOOR's on BELOW/OUTSIDE_BAND;
// carrying it verbatim turned a ceiling pre-warn into a floor pre-warn.
// ==============================================================}

// =====================================================================

// The issue's repro: MAX 120 / WARN 110 / ALARM 0 120 left lo=SET 110 on the
// band (WARNING at every reading). The ceiling edge must land in the hi slot.
static void test_shape_change_above_to_band_moves_ceiling_edge_to_hi_slot(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_ABOVE, 120.0f, 0.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_SET, 110.0f, RULE_WARN_AUTO, 0.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 9;
    rulesSetAlarmRuleForInput(14, RULE_OUTSIDE_BAND, 0.0f, 120.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0, warnReset);                                    // nothing lost — no NOTE
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false)); // floor: never set
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnLo);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], true));   // ceiling: still 110
    TEST_ASSERT_EQUAL_FLOAT(110.0f, rules[slot].warnHi);
    // And the engine agrees: a mid-band value is NOT in a warning band.
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_NONE, rulesRuleWarnSideHit(rules[slot], 60.0f));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_HI,   rulesRuleWarnSideHit(rules[slot], 115.0f));
}

// Reverse repro: ALARM 0 120 / WARN 5 110 / MAX 120 — the ceiling edge (hi
// slot, 110) must move INTO the lo slot; the floor edge (5) is dropped and
// reported as such (a MAX-only alarm has no lower side).
static void test_shape_change_band_to_above_moves_ceiling_edge_to_lo_slot(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_OUTSIDE_BAND, 0.0f, 120.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_SET, 5.0f, RULE_WARN_SET, 110.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(14, RULE_ABOVE, 120.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_DROPPED_MIN, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));  // lo slot = ceiling = 110
    TEST_ASSERT_EQUAL_FLOAT(110.0f, rules[slot].warnLo);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnHi);
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_NONE, rulesRuleWarnSideHit(rules[slot], 50.0f));  // 5 is gone
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO,   rulesRuleWarnSideHit(rules[slot], 112.0f));
}

// A NONE ceiling ("no pre-warn on the ceiling") must survive band->ABOVE as
// NONE, not silently become AUTO — the issue's `WARN NONE 110` variant.
static void test_shape_change_band_to_above_keeps_none_ceiling(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_OUTSIDE_BAND, 0.0f, 120.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_AUTO, 0.0f, RULE_WARN_NONE, 0.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 9;
    rulesSetAlarmRuleForInput(14, RULE_ABOVE, 120.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0, warnReset);  // floor was AUTO: nothing dropped
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_NONE, ruleWarnMode(rules[slot], false));
}

// ABOVE -> BELOW: the lo slot changes meaning (ceiling -> floor). 110 > 5 is
// a directionally VALID floor edge to rulesWarnSideValid, so only the remap
// can catch this — the ceiling edge is dropped, not kept as a floor edge.
static void test_shape_change_above_to_below_drops_ceiling_edge(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_ABOVE, 120.0f, 0.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_SET, 110.0f, RULE_WARN_AUTO, 0.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(14, RULE_BELOW, 5.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_DROPPED_MAX, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnLo);
}

// BELOW -> band: the floor edge keeps its slot AND its meaning; nothing lost.
static void test_shape_change_below_to_band_keeps_floor_edge(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_BELOW, 5.0f, 0.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_SET, 8.0f, RULE_WARN_AUTO, 0.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 9;
    rulesSetAlarmRuleForInput(14, RULE_OUTSIDE_BAND, 5.0f, 100.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(0, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(8.0f, rules[slot].warnLo);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], true));
}

// Remap THEN normalize: band->ABOVE where the moved ceiling edge is also
// stranded by the new limit (hi=SET 110, new ceiling 100) — reported as a
// MAX reset, with the dropped floor alongside.
static void test_shape_change_band_to_above_then_limit_strands_moved_edge(void) {
    addPlainInput(14);
    rulesSetAlarmRuleForInput(14, RULE_OUTSIDE_BAND, 0.0f, 120.0f, 0);
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(14, RULE_WARN_SET, 5.0f, RULE_WARN_SET, 110.0f, nullptr));
    int slot = rulesAlarmRuleSlotForInput(14);

    uint8_t warnReset = 0;
    rulesSetAlarmRuleForInput(14, RULE_ABOVE, 100.0f, 0.0f, 0, &warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_DROPPED_MIN | RULE_WARN_RESET_MAX, warnReset);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, ruleWarnMode(rules[slot], false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[slot].warnLo);
}

// =====================================================================
// #428 — rulesParseWarnToken: a typo must be an error, not SET 0.
// =====================================================================

static void test_parse_warn_token_keywords_and_numbers(void) {
    RuleWarnMode m = RULE_WARN_SET; float v = 99.0f;
    TEST_ASSERT_TRUE(rulesParseWarnToken("NONE", m, v));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_NONE, m); TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
    TEST_ASSERT_TRUE(rulesParseWarnToken("AUTO", m, v));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, m);
    TEST_ASSERT_TRUE(rulesParseWarnToken("none", m, v));  // arguments arrive as typed; keywords are case-insensitive
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_NONE, m);
    TEST_ASSERT_TRUE(rulesParseWarnToken("Auto", m, v));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_AUTO, m);
    TEST_ASSERT_TRUE(rulesParseWarnToken("210", m, v));
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_SET, m); TEST_ASSERT_EQUAL_FLOAT(210.0f, v);
    TEST_ASSERT_TRUE(rulesParseWarnToken("-12.5", m, v));
    TEST_ASSERT_EQUAL_FLOAT(-12.5f, v);
    TEST_ASSERT_TRUE(rulesParseWarnToken("0", m, v));   // an explicit 0 is still a number
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
}

static void test_parse_warn_token_rejects_garbage_and_leaves_outputs(void) {
    RuleWarnMode m = RULE_WARN_NONE; float v = 42.0f;
    const char* bad[] = { "2l0", "1O0", "abc", "", "12x", "nan", "inf", "NONE2", "AUT" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(rulesParseWarnToken(bad[i], m, v), bad[i]);
        TEST_ASSERT_EQUAL_UINT8(RULE_WARN_NONE, m);   // untouched on failure
        TEST_ASSERT_EQUAL_FLOAT(42.0f, v);
    }
}

// =====================================================================
// #433 — per-rule annunciator acknowledgement (rulesAck*)
// =====================================================================

// Two ALARM rules on two inputs; `trip(slot)` puts one into ACTIVE with a value
// past its ceiling so the annunciator severity is ALARM.
static int ackRule(uint8_t pin, float ceiling) {
    addPlainInput(pin);
    rulesSetAlarmRuleForInput(pin, RULE_ABOVE, ceiling, 0.0f, 0);
    return rulesAlarmRuleSlotForInput(pin);
}
static void trip(int slot, float value) {
    getInputByPin(rules[slot].sourceInput)->value = value;
    g_ruleContexts[slot].state = RULE_ACTIVE;
}
static void untrip(int slot) {
    getInputByPin(rules[slot].sourceInput)->value = 0.0f;
    g_ruleContexts[slot].state = RULE_READY;
}

static void test_ack_covers_only_rules_sounding_now(void) {
    int a = ackRule(10, 100.0f), b = ackRule(11, 100.0f);
    trip(a, 150.0f);                                  // b is armed but quiet
    TEST_ASSERT_EQUAL_UINT8(1, rulesAckActiveAlarms(1000));
    TEST_ASSERT_TRUE(rulesRuleAcked(a));
    TEST_ASSERT_FALSE(rulesRuleAcked(b));
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesSystemSeverity());          // LED still sees it
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,  rulesSystemSeverityUnacked());   // buzzer does not
    // A DIFFERENT rule tripping later is not covered — it sounds.
    trip(b, 150.0f);
    rulesAckExpire(2000, 30000);
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesSystemSeverityUnacked());
    TEST_ASSERT_TRUE(rulesRuleAcked(a));                               // a's ack survives
}

static void test_ack_expires_on_timer_and_repress_does_not_extend(void) {
    int a = ackRule(10, 100.0f);
    trip(a, 150.0f);
    TEST_ASSERT_EQUAL_UINT8(1, rulesAckActiveAlarms(1000));
    TEST_ASSERT_EQUAL_UINT8(0, rulesAckActiveAlarms(20000));   // re-press: nothing new, clock NOT restarted
    rulesAckExpire(30999, 30000);
    TEST_ASSERT_TRUE(rulesRuleAcked(a));
    TEST_ASSERT_EQUAL_UINT32(29999, rulesAckElapsedMs(a, 30999));
    rulesAckExpire(31000, 30000);                     // 1000 + 30000
    TEST_ASSERT_FALSE(rulesRuleAcked(a));
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesSystemSeverityUnacked());   // sounds again
}

// Timed policy = "mute this rule for N s" — it rides through a clear-and-retrip
// inside the window (a NaN glitch de-asserts an ACTIVE rule instantly and it
// re-trips after debounce; the old global window covered that, so must this).
static void test_ack_timed_survives_clear_and_retrip_within_window(void) {
    int a = ackRule(10, 100.0f);
    trip(a, 150.0f);
    rulesAckActiveAlarms(1000);
    untrip(a);                                        // glitch
    rulesAckExpire(2000, 30000);
    TEST_ASSERT_TRUE(rulesRuleAcked(a));
    trip(a, 150.0f);                                  // re-trip at 2 s: still muted
    rulesAckExpire(3000, 30000);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesSystemSeverityUnacked());
    rulesAckExpire(31000, 30000);                     // window ends
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesSystemSeverityUnacked());
}

static void test_ack_until_clear_holds_indefinitely_then_rearms(void) {
    int a = ackRule(10, 100.0f);
    trip(a, 150.0f);
    rulesAckActiveAlarms(1000);
    rulesAckExpire(1000UL + 3600UL * 1000UL * 6UL, /*untilClear*/0);   // six hours later
    TEST_ASSERT_TRUE(rulesRuleAcked(a));
    untrip(a);                                        // sensor recovers
    rulesAckExpire(1, 0);
    TEST_ASSERT_FALSE(rulesRuleAcked(a));
    trip(a, 150.0f);                                  // re-trip sounds again
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesSystemSeverityUnacked());
}

static void test_ack_dropped_when_rule_deleted_or_slot_reused(void) {
    int a = ackRule(10, 100.0f);
    trip(a, 150.0f);
    rulesAckActiveAlarms(1000);
    const uint8_t oldId = rules[a].id;
    rules[a].id = (uint8_t)(oldId + 1);               // slot now holds a different record
    TEST_ASSERT_FALSE(rulesRuleAcked(a));             // keyed by id, so not inherited
    rulesAckExpire(1001, 30000);
    rules[a].id = oldId;                              // even restoring the id: ack is gone
    TEST_ASSERT_FALSE(rulesRuleAcked(a));
}

static void test_ack_count_and_reset(void) {
    int a = ackRule(10, 100.0f), b = ackRule(11, 100.0f);
    trip(a, 150.0f); trip(b, 150.0f);
    TEST_ASSERT_EQUAL_UINT8(2, rulesAckActiveAlarms(0));
    TEST_ASSERT_EQUAL_UINT8(2, rulesAckCount());
    rulesResetTables();                               // a reload forgets every ack
    TEST_ASSERT_EQUAL_UINT8(0, rulesAckCount());
}

// The status frame pushes rulesAckGeneration() so clients drop the ALARM STATUS
// poll (#455): it must move on EVERY acked-set change and on nothing else —
// a missed bump strands a client's SILENCED chip, a spurious one is a refresh.
static void test_ack_generation_moves_on_every_set_change_only(void) {
    int a = ackRule(10, 100.0f), b = ackRule(11, 100.0f);
    uint8_t g = rulesAckGeneration();
    rulesAckExpire(500, 30000);                       // nothing acked: no change
    TEST_ASSERT_EQUAL_UINT8(g, rulesAckGeneration());

    trip(a, 150.0f);
    rulesAckActiveAlarms(1000);
    TEST_ASSERT_EQUAL_UINT8(++g, rulesAckGeneration());
    rulesAckActiveAlarms(2000);                       // re-press: set unchanged
    TEST_ASSERT_EQUAL_UINT8(g, rulesAckGeneration());
    rulesAckExpire(2000, 30000);                      // still inside the window
    TEST_ASSERT_EQUAL_UINT8(g, rulesAckGeneration());

    trip(b, 150.0f);                                  // second rule acked later
    rulesAckActiveAlarms(3000);
    TEST_ASSERT_EQUAL_UINT8(++g, rulesAckGeneration());

    rulesAckExpire(31000, 30000);                     // a lapses (timed), b holds
    TEST_ASSERT_EQUAL_UINT8(++g, rulesAckGeneration());
    TEST_ASSERT_EQUAL_UINT8(1, rulesAckCount());

    untrip(b);                                        // until-clear lapse
    rulesAckExpire(31001, 0);
    TEST_ASSERT_EQUAL_UINT8(++g, rulesAckGeneration());
    TEST_ASSERT_EQUAL_UINT8(0, rulesAckCount());

    trip(a, 150.0f);                                  // deleted rule: dropped on expire
    rulesAckActiveAlarms(40000);
    ++g;
    rules[a].id = (uint8_t)(rules[a].id + 1);
    rulesAckExpire(40001, 30000);
    TEST_ASSERT_EQUAL_UINT8(++g, rulesAckGeneration());

    rulesAckReset();                                  // nothing acked: no change
    TEST_ASSERT_EQUAL_UINT8(g, rulesAckGeneration());
}

// Only the ALARM tier sounds, so only it can be acknowledged: an ALARM rule in
// its approach (WARNING) band, and a WARNING-severity rule, are not acked.
static void test_ack_ignores_warning_tier(void) {
    int a = ackRule(10, 100.0f);
    getInputByPin(10)->value = 95.0f;                 // approach tier: 95 >= 90% of 100
    g_ruleContexts[a].state = RULE_READY;
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesSystemSeverity());
    TEST_ASSERT_EQUAL_UINT8(0, rulesAckActiveAlarms(0));
    TEST_ASSERT_FALSE(rulesRuleAcked(a));
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

// =====================================================================
// RULE <id> LIMIT — rulesSetRuleLimit (#457)
// =====================================================================

// A standalone WARNING rule (not the canonical alarm) with an Action on it.
static uint8_t addLinkedWarningRule(uint8_t pin, uint8_t actionId) {
    int slot = rulesRuleFreeSlot();
    Rule& r = rules[slot];
    memset(&r, 0, sizeof(r));
    r.id = rulesRuleAllocId();
    r.sourceInput = pin;
    r.condition = RULE_ABOVE;
    r.severity = RULE_SEV_WARNING;
    r.limitLo = 100.0f;
    r.hysteresis = 5.0f;
    r.warmupMs = 1234;
    r.flags = RULE_FLAG_ENABLED;
    g_ruleContexts[slot].state = RULE_ACTIVE;
    int aslot = rulesActionFreeSlot();
    Action& a = actions[aslot];
    memset(&a, 0, sizeof(a));
    a.id = actionId;
    a.type = ACT_RELAY;
    a.target = 6;
    a.ruleRef = RULES_ID_NONE;
    TEST_ASSERT_TRUE(rulesActionAddRule(actionId, r.id));
    return r.id;
}

static void test_limit_keeps_id_links_severity_and_state(void) {
    addPlainInput(10);
    const uint8_t id = addLinkedWarningRule(10, 3);
    const int slot = rulesRuleSlot(id);

    TEST_ASSERT_TRUE(rulesSetRuleLimit(id, RULE_ABOVE, 120.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT(slot, rulesRuleSlot(id));              // same record, same id
    TEST_ASSERT_EQUAL_FLOAT(120.0f, rules[slot].limitLo);
    TEST_ASSERT_EQUAL_UINT8(RULE_SEV_WARNING, rules[slot].severity);  // not forced to ALARM
    TEST_ASSERT_EQUAL_UINT32(1234, rules[slot].warmupMs);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, rules[slot].hysteresis);
    TEST_ASSERT_EQUAL_UINT8(RULE_ACTIVE, g_ruleContexts[slot].state);  // no re-arm
    int aslot = rulesActionSlot(3);
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(aslot));
    TEST_ASSERT_EQUAL_UINT8(id, rulesActionRuleAt(aslot, 0));   // link survives
}

static void test_limit_leaves_a_disabled_rule_disabled(void) {
    addPlainInput(10);
    const uint8_t id = addLinkedWarningRule(10, 3);
    const int slot = rulesRuleSlot(id);
    rules[slot].flags &= ~RULE_FLAG_ENABLED;
    g_ruleContexts[slot].state = RULE_DISABLED;
    TEST_ASSERT_TRUE(rulesSetRuleLimit(id, RULE_BELOW, 10.0f, 0.0f));
    TEST_ASSERT_FALSE(rules[slot].flags & RULE_FLAG_ENABLED);   // the facade would have enabled it
    TEST_ASSERT_EQUAL_UINT8(RULE_DISABLED, g_ruleContexts[slot].state);
}

static void test_limit_shape_change_and_warn_mask_match_the_facade(void) {
    addPlainInput(10);
    rulesSetAlarmRuleForInput(10, RULE_OUTSIDE_BAND, 0.0f, 100.0f, 0);
    const int slot = rulesAlarmRuleSlotForInput(10);
    const uint8_t id = rules[slot].id;
    TEST_ASSERT_TRUE(rulesSetAlarmWarnForInput(10, RULE_WARN_SET, 10.0f, RULE_WARN_SET, 90.0f));
    uint8_t mask = 0xAA;
    TEST_ASSERT_TRUE(rulesSetRuleLimit(id, RULE_BELOW, 5.0f, 0.0f, &mask));
    TEST_ASSERT_EQUAL_UINT8(RULE_BELOW, rules[slot].condition);
    TEST_ASSERT_EQUAL_UINT8(RULE_WARN_DROPPED_MAX, mask);        // band's ceiling edge has nowhere to go
    TEST_ASSERT_EQUAL_FLOAT(10.0f, rules[slot].warnLo);          // floor edge kept (still above 5)
}

static void test_limit_reports_cleared_hysteresis(void) {
    addPlainInput(10);
    const uint8_t id = addLinkedWarningRule(10, 3);             // HYST 5
    bool cleared = false;
    TEST_ASSERT_TRUE(rulesSetRuleLimit(id, RULE_ABOVE, 3.0f, 0.0f, nullptr, &cleared));
    TEST_ASSERT_TRUE(cleared);                                   // 5 >= |3| would never clear
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rules[rulesRuleSlot(id)].hysteresis);
}

static void test_limit_unknown_id(void) {
    TEST_ASSERT_FALSE(rulesSetRuleLimit(200, RULE_ABOVE, 1.0f, 0.0f));
}

static void test_parse_number_is_strict(void) {
    float v = 0;
    TEST_ASSERT_TRUE(rulesParseNumber("-12.5", v));
    TEST_ASSERT_EQUAL_FLOAT(-12.5f, v);
    TEST_ASSERT_FALSE(rulesParseNumber("12x", v));
    TEST_ASSERT_FALSE(rulesParseNumber("", v));
    TEST_ASSERT_FALSE(rulesParseNumber("nan", v));
    TEST_ASSERT_FALSE(rulesParseNumber(nullptr, v));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_limit_keeps_id_links_severity_and_state);
    RUN_TEST(test_limit_leaves_a_disabled_rule_disabled);
    RUN_TEST(test_limit_shape_change_and_warn_mask_match_the_facade);
    RUN_TEST(test_limit_reports_cleared_hysteresis);
    RUN_TEST(test_limit_unknown_id);
    RUN_TEST(test_parse_number_is_strict);
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
    RUN_TEST(test_shape_change_above_to_band_moves_ceiling_edge_to_hi_slot);
    RUN_TEST(test_shape_change_band_to_above_moves_ceiling_edge_to_lo_slot);
    RUN_TEST(test_shape_change_band_to_above_keeps_none_ceiling);
    RUN_TEST(test_shape_change_above_to_below_drops_ceiling_edge);
    RUN_TEST(test_shape_change_below_to_band_keeps_floor_edge);
    RUN_TEST(test_shape_change_band_to_above_then_limit_strands_moved_edge);
    RUN_TEST(test_parse_warn_token_keywords_and_numbers);
    RUN_TEST(test_parse_warn_token_rejects_garbage_and_leaves_outputs);
    RUN_TEST(test_ack_covers_only_rules_sounding_now);
    RUN_TEST(test_ack_expires_on_timer_and_repress_does_not_extend);
    RUN_TEST(test_ack_timed_survives_clear_and_retrip_within_window);
    RUN_TEST(test_ack_until_clear_holds_indefinitely_then_rearms);
    RUN_TEST(test_ack_dropped_when_rule_deleted_or_slot_reused);
    RUN_TEST(test_ack_count_and_reset);
    RUN_TEST(test_ack_generation_moves_on_every_set_change_only);
    RUN_TEST(test_ack_ignores_warning_tier);
    RUN_TEST(test_rule_reset_returns_warn_to_auto);
    RUN_TEST(test_reseed_for_input_returns_warn_to_auto);
    return UNITY_END();
}
