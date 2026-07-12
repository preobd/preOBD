// Native unit test: Rule evaluation engine (src/control/control_engine.cpp).
//
// controlRuleUpdate() takes `now` as a parameter (never calls millis()), so the
// INIT->WARMUP->READY->ACTIVE machine and the condition/hysteresis/debounce
// logic are fully deterministic on the host.
#include <unity.h>
#include <Arduino.h>
#include <math.h>

#include "../../src/control/control_engine.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- helpers ------------------------------------------------------------

// Enabled rule; warmup=500ms, debounce=200ms, hysteresis=2.0 by default.
static Rule makeRule(uint8_t cond, float lo, float hi = 0.0f,
                     float hyst = 2.0f, uint16_t warmup = 500, uint16_t debounce = 200) {
    Rule r{};
    r.id = 1;
    r.sourceInput = 10;
    r.condition = cond;
    r.severity = RULE_SEV_ALARM;
    r.flags = RULE_FLAG_ENABLED;
    r.limitLo = lo;
    r.limitHi = hi;
    r.hysteresis = hyst;
    r.warmupMs = warmup;
    r.debounceMs = debounce;
    return r;
}

// Walk a rule from INIT to READY holding a settled, in-spec value.
static void toReady(const Rule& r, RuleContext& ctx, float settledValue) {
    controlRuleInit(ctx, r, 0);
    controlRuleUpdate(r, ctx, settledValue, 0);      // INIT
    controlRuleUpdate(r, ctx, settledValue, 1001);   // INIT -> WARMUP (>1000ms)
    controlRuleUpdate(r, ctx, settledValue, 1602);   // WARMUP -> READY (601 > 500)
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

// --- init / arming (#251/#254) -----------------------------------------

static void test_init_enabled_arms(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    controlRuleInit(ctx, r, 0);
    TEST_ASSERT_EQUAL(RULE_INIT, ctx.state);
    TEST_ASSERT_EQUAL_UINT16(500, ctx.warmupTime_ms);
    TEST_ASSERT_EQUAL_UINT16(200, ctx.persistTime_ms);
}

static void test_init_disabled_stays_disabled(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.flags = 0;  // disabled
    RuleContext ctx{};
    controlRuleInit(ctx, r, 0);
    TEST_ASSERT_EQUAL(RULE_DISABLED, ctx.state);
}

static void test_disabled_rule_never_activates(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.flags = 0;
    RuleContext ctx{};
    controlRuleInit(ctx, r, 0);
    controlRuleUpdate(r, ctx, 9999.0f, 5000);
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_DISABLED, ctx.state);
}

// --- warmup gating ------------------------------------------------------

static void test_warmup_blocks_assertion(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    controlRuleInit(ctx, r, 0);
    controlRuleUpdate(r, ctx, 200.0f, 0);     // INIT, over threshold but blocked
    controlRuleUpdate(r, ctx, 200.0f, 500);   // still INIT
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
    controlRuleUpdate(r, ctx, 200.0f, 1001);  // INIT -> WARMUP
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
}

// --- ABOVE + hysteresis + debounce -------------------------------------

static void test_above_asserts_after_debounce(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    controlRuleUpdate(r, ctx, 120.0f, 2000);  // violation begins (debounce starts)
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));   // not yet debounced
    controlRuleUpdate(r, ctx, 120.0f, 2100);  // 100ms < 200ms debounce
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
    controlRuleUpdate(r, ctx, 120.0f, 2200);  // 200ms >= debounce -> ACTIVE
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
}

static void test_above_hysteresis_holds_until_clear_band(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // hyst 2.0 -> clears below 98.0
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    controlRuleUpdate(r, ctx, 120.0f, 2000);
    controlRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    // dip to 99.0: above the clear band (98.0) -> still active
    controlRuleUpdate(r, ctx, 99.0f, 2300);
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    // drop to 97.0: below clear band -> de-asserts
    controlRuleUpdate(r, ctx, 97.0f, 2400);
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

// --- BELOW --------------------------------------------------------------

static void test_below_asserts_and_clears(void) {
    Rule r = makeRule(RULE_BELOW, 10.0f);  // hyst 2.0 -> clears above 12.0
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    controlRuleUpdate(r, ctx, 5.0f, 2000);
    controlRuleUpdate(r, ctx, 5.0f, 2200);  // debounced
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    controlRuleUpdate(r, ctx, 11.0f, 2300); // within clear band -> still active
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    controlRuleUpdate(r, ctx, 13.0f, 2400); // above clear band -> clears
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
}

// --- OUTSIDE_BAND (both edges) -----------------------------------------

static void test_outside_band_both_edges(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    // high edge
    controlRuleUpdate(r, ctx, 120.0f, 2000);
    controlRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    controlRuleUpdate(r, ctx, 50.0f, 2300);   // back in band
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
    // low edge
    controlRuleUpdate(r, ctx, 5.0f, 2400);
    controlRuleUpdate(r, ctx, 5.0f, 2600);
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
}

// --- NaN safety ---------------------------------------------------------

static void test_nan_never_asserts_and_clears_active(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    controlRuleUpdate(r, ctx, 120.0f, 2000);
    controlRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(controlRuleIsActive(ctx));
    // sensor goes NaN -> de-asserts (failsafe intent)
    controlRuleUpdate(r, ctx, NAN, 2300);
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));
}

// --- INSIDE_BAND deferred ----------------------------------------------

static void test_inside_band_deferred_never_asserts(void) {
    Rule r = makeRule(RULE_INSIDE_BAND, 10.0f, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);  // squarely inside the band
    controlRuleUpdate(r, ctx, 50.0f, 2000);
    controlRuleUpdate(r, ctx, 50.0f, 2200);
    TEST_ASSERT_FALSE(controlRuleIsActive(ctx));  // deferred shape: inert in v1
}

// --- severity rollup ----------------------------------------------------

static void test_severity_reflects_active_state(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_WARNING;
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, controlRuleSeverity(r, ctx));  // not active
    controlRuleUpdate(r, ctx, 120.0f, 2000);
    controlRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleSeverity(r, ctx));  // active -> configured severity
}

// --- Derived warning tier (annunciator severity) ----------------------

static void test_annun_active_alarm_is_alarm(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // severity ALARM
    RuleContext ctx{};
    ctx.state = RULE_ACTIVE;
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, controlRuleAnnunciatorSeverity(r, ctx, 120.0f));
}

static void test_annun_warning_band_below_limit(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // warn at 90
    RuleContext ctx{};
    ctx.state = RULE_READY;
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleAnnunciatorSeverity(r, ctx, 95.0f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    controlRuleAnnunciatorSeverity(r, ctx, 80.0f));
}

static void test_annun_warning_only_for_alarm_rules(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_WARNING;          // a WARNING rule has no sub-tier
    RuleContext ctx{};
    ctx.state = RULE_READY;
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, controlRuleAnnunciatorSeverity(r, ctx, 95.0f));
}

static void test_annun_no_warning_during_warmup(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    ctx.state = RULE_WARMUP;
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, controlRuleAnnunciatorSeverity(r, ctx, 95.0f));
}

static void test_annun_outside_band_low_warning(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);  // low warn at 11 (10*1.1)
    RuleContext ctx{};
    ctx.state = RULE_READY;
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleAnnunciatorSeverity(r, ctx, 10.5f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    controlRuleAnnunciatorSeverity(r, ctx, 50.0f));
}

// Regression: warning band must stay on the PRE-limit side for non-positive
// limits. A magnitude-based inset (fabsf) keeps WARNING ahead of ALARM; the
// old limit*pct form flipped sides once the limit went <= 0.
static void test_annun_warning_band_negative_above(void) {
    Rule r = makeRule(RULE_ABOVE, -10.0f);  // warn at -10 - 0.1*10 = -11
    RuleContext ctx{};
    ctx.state = RULE_READY;
    // approaching -10 from below: -10.5 is in [-11, -10) -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleAnnunciatorSeverity(r, ctx, -10.5f));
    // well below the limit -> not yet warning
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    controlRuleAnnunciatorSeverity(r, ctx, -20.0f));
}

static void test_annun_warning_band_negative_below(void) {
    Rule r = makeRule(RULE_BELOW, -10.0f);  // warn at -10 + 0.1*10 = -9
    RuleContext ctx{};
    ctx.state = RULE_READY;
    // approaching -10 from above: -9.5 is in (-10, -9] -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleAnnunciatorSeverity(r, ctx, -9.5f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    controlRuleAnnunciatorSeverity(r, ctx, 0.0f));
}

static void test_annun_outside_band_negative_low_edge(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, -20.0f, 100.0f);  // low warn at -20 + 0.1*20 = -18
    RuleContext ctx{};
    ctx.state = RULE_READY;
    // approaching the -20 low edge from inside the band -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, controlRuleAnnunciatorSeverity(r, ctx, -19.0f));
    // mid-band -> no warning
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    controlRuleAnnunciatorSeverity(r, ctx, 0.0f));
}

// --- Action resolution -------------------------------------------------

static Action makeAction(uint8_t type, uint8_t ruleRef, uint8_t failsafe = ACT_FAILSAFE_OFF) {
    Action a{};
    a.id = 0;
    a.type = type;
    a.target = 9;
    a.ruleRef = ruleRef;
    a.behavior = 0;
    a.failsafe = failsafe;
    return a;
}

static void test_action_manual_follows_manual_state(void) {
    Action a = makeAction(ACT_RELAY, CONTROL_ID_NONE);
    // ruleActive / sourceTrustworthy are ignored for a manual action
    TEST_ASSERT_TRUE(controlActionDesired(a, false, false, true));
    TEST_ASSERT_FALSE(controlActionDesired(a, true, true, false));
}

static void test_action_follows_rule_when_trustworthy(void) {
    Action a = makeAction(ACT_RELAY, 5);  // linked to rule id 5
    TEST_ASSERT_TRUE(controlActionDesired(a, true, true, false));
    TEST_ASSERT_FALSE(controlActionDesired(a, false, true, false));
}

static void test_action_relay_failsafe_on_untrustworthy(void) {
    Action aOn  = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    Action aOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    // source untrustworthy: ignore ruleActive, assume failsafe
    TEST_ASSERT_TRUE(controlActionDesired(aOn,  false, false, false));
    TEST_ASSERT_FALSE(controlActionDesired(aOff, true,  false, false));
}

static void test_action_nonrelay_deasserts_on_untrustworthy(void) {
    Action a = makeAction(ACT_LED, 5, ACT_FAILSAFE_ON);  // failsafe ignored for non-RELAY
    TEST_ASSERT_FALSE(controlActionDesired(a, true, false, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_init_enabled_arms);
    RUN_TEST(test_init_disabled_stays_disabled);
    RUN_TEST(test_disabled_rule_never_activates);
    RUN_TEST(test_warmup_blocks_assertion);
    RUN_TEST(test_above_asserts_after_debounce);
    RUN_TEST(test_above_hysteresis_holds_until_clear_band);
    RUN_TEST(test_below_asserts_and_clears);
    RUN_TEST(test_outside_band_both_edges);
    RUN_TEST(test_nan_never_asserts_and_clears_active);
    RUN_TEST(test_inside_band_deferred_never_asserts);
    RUN_TEST(test_severity_reflects_active_state);
    RUN_TEST(test_annun_active_alarm_is_alarm);
    RUN_TEST(test_annun_warning_band_below_limit);
    RUN_TEST(test_annun_warning_only_for_alarm_rules);
    RUN_TEST(test_annun_no_warning_during_warmup);
    RUN_TEST(test_annun_outside_band_low_warning);
    RUN_TEST(test_annun_warning_band_negative_above);
    RUN_TEST(test_annun_warning_band_negative_below);
    RUN_TEST(test_annun_outside_band_negative_low_edge);
    RUN_TEST(test_action_manual_follows_manual_state);
    RUN_TEST(test_action_follows_rule_when_trustworthy);
    RUN_TEST(test_action_relay_failsafe_on_untrustworthy);
    RUN_TEST(test_action_nonrelay_deasserts_on_untrustworthy);
    return UNITY_END();
}
