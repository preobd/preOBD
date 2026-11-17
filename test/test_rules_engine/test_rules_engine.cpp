// Native unit test: Rule evaluation engine (src/rules/rules_engine.cpp).
//
// rulesRuleUpdate() takes `now` as a parameter (never calls millis()), so the
// INIT->WARMUP->READY->ACTIVE machine and the condition/hysteresis/debounce
// logic are fully deterministic on the host.
#include <unity.h>
#include <Arduino.h>
#include <math.h>

#include "../../src/rules/rules_engine.cpp"

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
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, settledValue, 0);      // INIT
    rulesRuleUpdate(r, ctx, settledValue, 1001);   // INIT -> WARMUP (>1000ms)
    rulesRuleUpdate(r, ctx, settledValue, 1602);   // WARMUP -> READY (601 > 500)
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}


// Tick-driven annunciator severity (#668): the approach tier is a latch that
// rulesRuleUpdate() sets after debounceMs, so a severity check feeds the value
// through the real state machine for debounce + a margin, never pins ctx.
static RuleSeverity annunAt(const Rule& r, float v) {
    RuleContext ctx{};
    toReady(r, ctx, v);
    for (uint32_t t = 1650; t <= 1650u + r.debounceMs + 100u; t += 50) {
        rulesRuleUpdate(r, ctx, v, t);
    }
    return rulesRuleAnnunciatorSeverity(r, ctx);
}

// Feed `v` every 50 ms from `*t` for `ms`, advancing *t.
static void feed(const Rule& r, RuleContext& ctx, float v, uint32_t& t, uint32_t ms) {
    const uint32_t end = t + ms;
    while (t < end) { t += 50; rulesRuleUpdate(r, ctx, v, t); }
}

// --- init / arming (#251/#254) -----------------------------------------

static void test_init_enabled_arms(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    TEST_ASSERT_EQUAL(RULE_INIT, ctx.state);
    TEST_ASSERT_EQUAL_UINT16(500, ctx.warmupTime_ms);
    TEST_ASSERT_EQUAL_UINT16(200, ctx.persistTime_ms);
}

static void test_init_disabled_stays_disabled(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.flags = 0;  // disabled
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    TEST_ASSERT_EQUAL(RULE_DISABLED, ctx.state);
}

static void test_disabled_rule_never_activates(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.flags = 0;
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, 9999.0f, 5000);
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_DISABLED, ctx.state);
}

// --- warmup gating ------------------------------------------------------

static void test_warmup_blocks_assertion(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, 200.0f, 0);     // INIT, over threshold but blocked
    rulesRuleUpdate(r, ctx, 200.0f, 500);   // still INIT
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 200.0f, 1001);  // INIT -> WARMUP
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
}

// --- ABOVE + hysteresis + debounce -------------------------------------

static void test_above_asserts_after_debounce(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 2000);  // violation begins (debounce starts)
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));   // not yet debounced
    rulesRuleUpdate(r, ctx, 120.0f, 2100);  // 100ms < 200ms debounce
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 120.0f, 2200);  // 200ms >= debounce -> ACTIVE
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
}

static void test_above_hysteresis_holds_until_clear_band(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // hyst 2.0 -> clears below 98.0
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 2000);
    rulesRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    // dip to 99.0: above the clear band (98.0) -> still active
    rulesRuleUpdate(r, ctx, 99.0f, 2300);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    // drop to 97.0: below clear band -> the clear timer starts; the rule
    // leaves ACTIVE only after debounceMs (200) of clear readings (#668).
    rulesRuleUpdate(r, ctx, 97.0f, 2400);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 97.0f, 2600);
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

// --- BELOW --------------------------------------------------------------

static void test_below_asserts_and_clears(void) {
    Rule r = makeRule(RULE_BELOW, 10.0f);  // hyst 2.0 -> clears above 12.0
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 5.0f, 2000);
    rulesRuleUpdate(r, ctx, 5.0f, 2200);  // debounced
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 11.0f, 2300); // within clear band -> still active
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 13.0f, 2400); // above clear band -> clear timer starts (#668)
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 13.0f, 2600); // held debounceMs -> clears
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
}

// --- OUTSIDE_BAND (both edges) -----------------------------------------

static void test_outside_band_both_edges(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    // high edge
    rulesRuleUpdate(r, ctx, 120.0f, 2000);
    rulesRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 50.0f, 2300);   // back in band: clear timer starts (#668)
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, 50.0f, 2500);   // held debounceMs
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    // low edge
    rulesRuleUpdate(r, ctx, 5.0f, 2600);
    rulesRuleUpdate(r, ctx, 5.0f, 2800);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
}

// The trip side is latched at the trip and stays put inside the band (the
// clear window), but a reading past the OTHER limit while still ACTIVE (a
// held NaN gap, a coarse jump) re-latches it (#668 pre-merge review).
static void test_trip_side_relatches_past_the_other_limit(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    uint32_t t = 1650;
    feed(r, ctx, 120.0f, t, 300);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_HI, rulesRuleTripSide(ctx));
    feed(r, ctx, 50.0f, t, 100);             // clear window: side stays
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_HI, rulesRuleTripSide(ctx));
    feed(r, ctx, NAN, t, 500);               // short gap, held ACTIVE
    feed(r, ctx, 5.0f, t, 50);               // comes back past limitLo
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO, rulesRuleTripSide(ctx));
}

// --- NaN safety ---------------------------------------------------------

static void test_nan_never_asserts_and_clears_active(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 2000);
    rulesRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    // sensor goes NaN: held for the fault debounce (#650), then de-asserts
    // (failsafe intent, §11.3)
    rulesRuleUpdate(r, ctx, NAN, 2300);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, NAN, 2300 + RULES_FAULT_DEBOUNCE_MS - 1);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    rulesRuleUpdate(r, ctx, NAN, 2300 + RULES_FAULT_DEBOUNCE_MS);
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.faultStartTime);
    // A READY rule never asserts on NaN however long it lasts.
    rulesRuleUpdate(r, ctx, NAN, 9000);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

// #650: a gap shorter than the debounce leaves an ACTIVE rule ACTIVE the whole
// way through -- it never passes through READY, so it never re-qualifies
// for persistTime when the reading returns.
static void test_active_rule_rides_out_a_short_nan_gap(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, 2.0f, 500, /*debounce*/1000);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 2000);
    rulesRuleUpdate(r, ctx, 120.0f, 3000);
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    for (uint32_t t = 3050; t < 3050 + RULES_FAULT_DEBOUNCE_MS - 100; t += 50) {
        rulesRuleUpdate(r, ctx, NAN, t);
        TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "held through the gap");
    }
    rulesRuleUpdate(r, ctx, 120.0f, 3050 + RULES_FAULT_DEBOUNCE_MS - 50);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "no re-qualification after the gap");
    TEST_ASSERT_EQUAL_UINT32(0, ctx.nanSince);
    // A value back on the clear side de-asserts after debounceMs (1000) of
    // clear readings (#668), no longer at once.
    rulesRuleUpdate(r, ctx, 50.0f, 6000);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "clear timer just started");
    rulesRuleUpdate(r, ctx, 50.0f, 7000);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

// #650: a READY rule's persist timer counts observed samples only: a NaN in
// the middle restarts it, so spike, NaN, spike is not a sustained condition.
static void test_nan_gap_restarts_a_ready_rules_persist_timer(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, 2.0f, 500, /*debounce*/1000);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 2000);       // timer starts
    rulesRuleUpdate(r, ctx, NAN, 2500);          // gap
    TEST_ASSERT_EQUAL_UINT32(0, ctx.faultStartTime);
    rulesRuleUpdate(r, ctx, 120.0f, 3100);       // 1100 ms after the first spike
    TEST_ASSERT_EQUAL_MESSAGE(RULE_READY, ctx.state, "the gap is not counted as persistence");
    rulesRuleUpdate(r, ctx, 120.0f, 4100);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
}

// #650 option B: the action path's test is 2 s from the first NaN in every
// armed state, with no warmup stretch; the alarm channel's keeps the stretch.
static void test_source_untrusted_has_no_warmup_stretch(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, 2.0f, /*warmup*/30000);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, NAN, 10);            // INIT, dead from boot
    TEST_ASSERT_FALSE(rulesRuleSourceUntrusted(ctx, 10 + RULES_FAULT_DEBOUNCE_MS - 1));
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleSourceUntrusted(ctx, 10 + RULES_FAULT_DEBOUNCE_MS),
                             "even in INIT");
    TEST_ASSERT_FALSE(rulesRuleSourceFaulted(ctx, 10 + RULES_FAULT_DEBOUNCE_MS));  // INIT: not reported
    rulesRuleUpdate(r, ctx, NAN, 1100);          // -> WARMUP
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_TRUE(rulesRuleSourceUntrusted(ctx, 5000));
    TEST_ASSERT_FALSE_MESSAGE(rulesRuleSourceFaulted(ctx, 5000), "reporting waits out the 30 s warmup");
    TEST_ASSERT_TRUE(rulesRuleSourceFaulted(ctx, 10 + 30000));
    // A disabled rule is never untrusted.
    ctx.state = RULE_DISABLED;
    TEST_ASSERT_FALSE(rulesRuleSourceUntrusted(ctx, 60000));
}

// --- nanSince / rulesRuleSourceFaulted (#443) --------------------------

static void test_nansince_stamps_on_nan_and_clears_on_finite(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.nanSince);
    rulesRuleUpdate(r, ctx, NAN, 3000);
    TEST_ASSERT_EQUAL_UINT32(3000, ctx.nanSince);
    // Stays stamped at the FIRST nan tick, not re-stamped on later nan ticks.
    rulesRuleUpdate(r, ctx, NAN, 3500);
    TEST_ASSERT_EQUAL_UINT32(3000, ctx.nanSince);
    // A finite reading clears it immediately.
    rulesRuleUpdate(r, ctx, 50.0f, 3600);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.nanSince);
}

static void test_nansince_clears_on_disable(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, NAN, 3000);
    TEST_ASSERT_EQUAL_UINT32(3000, ctx.nanSince);
    r.flags = 0;  // disable
    rulesRuleUpdate(r, ctx, NAN, 4000);
    TEST_ASSERT_EQUAL(RULE_DISABLED, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.nanSince);
}

// now == 0 must still stamp nanSince (as 1) rather than being mistaken for
// "reading is finite" — a boot-time tick or a test harness can legitimately
// pass now == 0.
static void test_nansince_now_zero_stamps_one(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, NAN, 0);
    TEST_ASSERT_EQUAL_UINT32(1, ctx.nanSince);
}

// WARMUP/INIT stamp nanSince too (maintained before the state switch) — a
// thermocouple unplugged from boot never leaves WARMUP, and that is exactly
// the case rulesRuleSourceFaulted() must be able to report.
static void test_nansince_stamps_during_warmup(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, NAN, 0);        // INIT
    rulesRuleUpdate(r, ctx, NAN, 1001);     // INIT -> WARMUP; still nan
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_NOT_EQUAL(0, ctx.nanSince);
}

static void test_source_faulted_false_in_init(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, NAN, 0);   // still RULE_INIT (< RULES_INIT_MS)
    TEST_ASSERT_EQUAL(RULE_INIT, ctx.state);
    TEST_ASSERT_FALSE(rulesRuleSourceFaulted(ctx, 0 + RULES_FAULT_DEBOUNCE_MS + 5000));
}

static void test_source_faulted_true_in_warmup_past_debounce(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, NAN, 0);        // INIT
    rulesRuleUpdate(r, ctx, NAN, 1001);     // INIT -> WARMUP, nanSince stamped ~1001
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_FALSE(rulesRuleSourceFaulted(ctx, 1001));  // not yet debounced
    TEST_ASSERT_TRUE(rulesRuleSourceFaulted(ctx, ctx.nanSince + RULES_FAULT_DEBOUNCE_MS));
}

static void test_source_faulted_false_when_disabled(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    ctx.state = RULE_DISABLED;
    ctx.nanSince = 1;
    TEST_ASSERT_FALSE(rulesRuleSourceFaulted(ctx, 999999));
}

// --- INSIDE_BAND deferred ----------------------------------------------

static void test_inside_band_deferred_never_asserts(void) {
    Rule r = makeRule(RULE_INSIDE_BAND, 10.0f, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);  // squarely inside the band
    rulesRuleUpdate(r, ctx, 50.0f, 2000);
    rulesRuleUpdate(r, ctx, 50.0f, 2200);
    TEST_ASSERT_FALSE(rulesRuleIsActive(ctx));  // deferred shape: inert in v1
}

// --- severity rollup ----------------------------------------------------

static void test_severity_reflects_active_state(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_WARNING;
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleSeverity(r, ctx));  // not active
    rulesRuleUpdate(r, ctx, 120.0f, 2000);
    rulesRuleUpdate(r, ctx, 120.0f, 2200);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleSeverity(r, ctx));  // active -> configured severity
}

// --- Derived warning tier (annunciator severity) ----------------------

static void test_annun_active_alarm_is_alarm(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // severity ALARM
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, annunAt(r, 120.0f));
}

static void test_annun_warning_band_below_limit(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // warn at 90
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 95.0f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 80.0f));
}

static void test_annun_warning_only_for_alarm_rules(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_WARNING;          // a WARNING rule has no sub-tier
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, annunAt(r, 95.0f));
}

// An active INFO rule reports INFO to the notification path (the BLE broadcaster
// reads this). The LED/buzzer boundary maps INFO->NORMAL separately, so this
// value never lights the dash — but it must reach the notification log (#308).
static void test_annun_active_info_is_info(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_INFO;
    TEST_ASSERT_EQUAL(RULE_SEV_INFO, annunAt(r, 120.0f));
}

// INFO is not a condition that pre-announces: an INFO rule approaching its limit
// (READY, in the warning band) must NOT raise a derived WARNING — only ALARM
// rules do (#308). Guards against INFO leaking onto the annunciator ladder.
static void test_annun_no_derived_warning_for_info(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // warn band at 90
    r.severity = RULE_SEV_INFO;
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, annunAt(r, 95.0f));
}

// Ordinal invariant: INFO sits below WARNING/ALARM so the LED rollup's bare
// `max` can never let an active INFO rule mask a real ALARM (#308, the §6
// masking bug this reorder eliminates by construction).
static void test_info_below_warning_and_alarm(void) {
    TEST_ASSERT_TRUE(RULE_SEV_INFO < RULE_SEV_WARNING);
    TEST_ASSERT_TRUE(RULE_SEV_INFO < RULE_SEV_ALARM);
    TEST_ASSERT_TRUE(RULE_SEV_NONE < RULE_SEV_INFO);
}

static void test_annun_no_warning_during_warmup(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    rulesRuleUpdate(r, ctx, 95.0f, 0);
    rulesRuleUpdate(r, ctx, 95.0f, 1001);
    rulesRuleUpdate(r, ctx, 95.0f, 1400);
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));
}

static void test_annun_outside_band_low_warning(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);  // low warn at 11 (10*1.1)
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 10.5f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 50.0f));
}

// Regression: warning band must stay on the PRE-limit side for non-positive
// limits. A magnitude-based inset (fabsf) keeps WARNING ahead of ALARM; the
// old limit*pct form flipped sides once the limit went <= 0.
static void test_annun_warning_band_negative_above(void) {
    Rule r = makeRule(RULE_ABOVE, -10.0f);  // warn at -10 - 0.1*10 = -11
    // approaching -10 from below: -10.5 is in [-11, -10) -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, -10.5f));
    // well below the limit -> not yet warning
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, -20.0f));
}

static void test_annun_warning_band_negative_below(void) {
    Rule r = makeRule(RULE_BELOW, -10.0f);  // warn at -10 + 0.1*10 = -9
    // approaching -10 from above: -9.5 is in (-10, -9] -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, -9.5f));
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 0.0f));
}

static void test_annun_outside_band_negative_low_edge(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, -20.0f, 100.0f);  // low warn at -20 + 0.1*20 = -18
    // approaching the -20 low edge from inside the band -> WARNING
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, -19.0f));
    // mid-band -> no warning
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 0.0f));
}

// --- Approach tier: per-side warn mode (AUTO/NONE/SET, §11.2, #425) -----

// SET lo edge on an ABOVE rule: warn strictly at the stored value, not the
// derived 90% figure.
static void test_warn_set_lo_on_above(void) {
    Rule r = makeRule(RULE_ABOVE, 230.0f);
    r.warnLo = 210.0f;
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 205.0f));
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 215.0f));
    // Past the limit and debounced -> ACTIVE -> configured severity (ALARM).
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, annunAt(r, 235.0f));
}

// SET lo edge on a BELOW rule (approaching from above).
static void test_warn_set_lo_on_below(void) {
    Rule r = makeRule(RULE_BELOW, 10.0f);
    r.warnLo = 15.0f;
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 20.0f));
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 12.0f));
}

// NONE: no approach tier at all — a READY rule past its limit but not yet
// debounced (still asserted, still READY) stays silent instead of going
// through a WARNING step; the FIRST thing it ever annunciates is ALARM once
// it debounces to ACTIVE. This is an intentional visible behaviour change
// from the pre-#425 always-derived tier (§11.2 amendment A6).
static void test_warn_none_never_warns_goes_straight_to_alarm(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_NONE);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, annunAt(r, 99.0f));
    // Past the limit but not yet debounced: still READY, still silent.
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 150.0f, 2000);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));
    rulesRuleUpdate(r, ctx, 150.0f, 2200);
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesRuleAnnunciatorSeverity(r, ctx));  // first tier it ever sees is ALARM
}

// OUTSIDE_BAND with one side SET, the other AUTO: each side derives independently.
static void test_warn_outside_set_lo_auto_hi(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    r.warnLo = 15.0f;  // SET, well inside the AUTO-derived 11
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    // hi side stays AUTO: derived edge = 100 - 0.1*100 = 90
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 15.0f));   // hits SET lo edge
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 20.0f));   // above SET lo edge -> not yet warning
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 95.0f));   // hits AUTO-derived hi edge
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 50.0f));   // mid-band
}

// OUTSIDE_BAND with one side NONE, the other SET.
static void test_warn_outside_none_lo_set_hi(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_NONE);
    r.warnHi = 90.0f;
    ruleSetWarnMode(r, true, RULE_WARN_SET);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE,    annunAt(r, 10.5f));  // near lo edge -> NONE side, silent
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, annunAt(r, 95.0f));  // hi SET edge hit
}

// OUTSIDE_BAND with both sides SET, deliberately asymmetric around the band
// midpoint — this is the shape the AlarmBroadcaster midpoint bug got wrong
// (see test_broadcaster::test_outside_band_set_warn_uses_correct_side).
static void test_warn_outside_both_set_asymmetric(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 0.0f, 100.0f);
    r.warnLo = 80.0f;
    r.warnHi = 90.0f;
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    ruleSetWarnMode(r, true, RULE_WARN_SET);
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO, rulesRuleWarnSideHit(r, 75.0f));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_HI, rulesRuleWarnSideHit(r, 95.0f));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_NONE, rulesRuleWarnSideHit(r, 85.0f));  // strictly between the two edges
}

// rulesRuleWarnSideHit directly, across conditions.
static void test_warn_side_hit_above_below(void) {
    Rule above = makeRule(RULE_ABOVE, 100.0f);  // AUTO warn at 90
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO,   rulesRuleWarnSideHit(above, 95.0f));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_NONE, rulesRuleWarnSideHit(above, 50.0f));
    Rule below = makeRule(RULE_BELOW, 10.0f);   // AUTO warn at 11
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO,   rulesRuleWarnSideHit(below, 10.5f));
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_NONE, rulesRuleWarnSideHit(below, 50.0f));
}

// --- rulesWarnSideValid: accept/reject per condition (§11.2 amendment A2) -

static void test_warn_valid_auto_none_always_legal(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);  // default flags -> AUTO both sides
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, false));
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, true));
    ruleSetWarnMode(r, false, RULE_WARN_NONE);
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, false));
}

static void test_warn_valid_above_accepts_below_limit(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 90.0f;
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, false));
}

static void test_warn_valid_above_rejects_at_or_past_limit(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 100.0f;  // strict equality rejected — zero-width band
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
    r.warnLo = 110.0f;  // past the limit
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
}

static void test_warn_valid_below_accepts_above_limit_rejects_at_or_under(void) {
    Rule r = makeRule(RULE_BELOW, 10.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 15.0f;
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, false));
    r.warnLo = 10.0f;  // strict equality rejected
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
    r.warnLo = 5.0f;   // past the limit
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
}

// A hi-side SET is never legal on a one-sided condition (no hi side to set).
static void test_warn_valid_hi_set_on_above_below_rejected(void) {
    Rule above = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(above, true, RULE_WARN_SET);
    above.warnHi = 90.0f;
    TEST_ASSERT_FALSE(rulesWarnSideValid(above, true));

    Rule below = makeRule(RULE_BELOW, 10.0f);
    ruleSetWarnMode(below, true, RULE_WARN_SET);
    below.warnHi = 15.0f;
    TEST_ASSERT_FALSE(rulesWarnSideValid(below, true));
}

static void test_warn_valid_outside_edge_outside_band_rejected(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 5.0f;    // below limitLo — outside the band
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
    r.warnLo = 10.0f;   // strict equality with limitLo rejected
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));

    ruleSetWarnMode(r, true, RULE_WARN_SET);
    r.warnHi = 110.0f;  // above limitHi — outside the band
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, true));
    r.warnHi = 100.0f;  // strict equality with limitHi rejected
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, true));
}

static void test_warn_valid_outside_both_set_requires_lo_less_than_hi(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 0.0f, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    ruleSetWarnMode(r, true, RULE_WARN_SET);
    r.warnLo = 90.0f;
    r.warnHi = 80.0f;   // lo >= hi -> both sides invalid
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, true));
    r.warnLo = 80.0f;
    r.warnHi = 80.0f;   // equal -> still invalid (strict)
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, false));
    TEST_ASSERT_FALSE(rulesWarnSideValid(r, true));
    r.warnLo = 80.0f;
    r.warnHi = 90.0f;   // valid
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, false));
    TEST_ASSERT_TRUE(rulesWarnSideValid(r, true));
}

// --- rulesNormalizeWarn: degrade-to-AUTO mask + zeroing -----------------

static void test_normalize_warn_degrades_invalid_side_and_zeros(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 150.0f;  // invalid: past the limit
    uint8_t mask = rulesNormalizeWarn(r);
    TEST_ASSERT_EQUAL_UINT8(0x01, mask);
    TEST_ASSERT_EQUAL(RULE_WARN_AUTO, ruleWarnMode(r, false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.warnLo);
}

static void test_normalize_warn_leaves_valid_side_alone(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 90.0f;  // valid
    uint8_t mask = rulesNormalizeWarn(r);
    TEST_ASSERT_EQUAL_UINT8(0, mask);
    TEST_ASSERT_EQUAL(RULE_WARN_SET, ruleWarnMode(r, false));
    TEST_ASSERT_EQUAL_FLOAT(90.0f, r.warnLo);
}

// Both sides invalid: each is evaluated against the ORIGINAL record (not a
// partially-degraded one) — lo degrading first must not change whether hi
// was legal, since the both-SET cross-check reads the other side's value.
static void test_normalize_warn_both_sides_degrade_independently(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 0.0f, 100.0f);
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    ruleSetWarnMode(r, true, RULE_WARN_SET);
    r.warnLo = 90.0f;  // both invalid: lo >= hi AND lo is fine on its own vs the band
    r.warnHi = 80.0f;
    uint8_t mask = rulesNormalizeWarn(r);
    TEST_ASSERT_EQUAL_UINT8(0x03, mask);
    TEST_ASSERT_EQUAL(RULE_WARN_AUTO, ruleWarnMode(r, false));
    TEST_ASSERT_EQUAL(RULE_WARN_AUTO, ruleWarnMode(r, true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.warnLo);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.warnHi);
}

// Non-ALARM severity ignores warn modes entirely (annunciator gate is
// severity == RULE_SEV_ALARM, independent of validity/mode bits).
static void test_warn_non_alarm_severity_ignores_modes(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    r.severity = RULE_SEV_WARNING;
    ruleSetWarnMode(r, false, RULE_WARN_SET);
    r.warnLo = 90.0f;
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, annunAt(r, 95.0f));
}

// --- Action resolution -------------------------------------------------

// Membership aggregates the manager hands the resolver. `linked()` is the
// pre-#343 single-member shape: a faulted source can never be anyActive (NaN
// never asserts), which is what keeps these cases identical to the old
// (ruleActive, sourceTrustworthy) pair.
static ActionDrive manualOnly() { return ActionDrive{0, false, false}; }
static ActionDrive linked(bool ruleActive, bool trustworthy) {
    return ActionDrive{1, trustworthy && ruleActive, !trustworthy};
}
static ActionDrive members(uint8_t n, bool anyActive, bool anyFaulted) {
    return ActionDrive{n, anyActive, anyFaulted};
}

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
    Action a = makeAction(ACT_RELAY, RULES_ID_NONE);
    // rule state is ignored for a manual action
    TEST_ASSERT_TRUE(rulesActionDesired(a, manualOnly(), true, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(a, manualOnly(), false, ACT_OVR_AUTO));
}

static void test_action_follows_rule_when_trustworthy(void) {
    Action a = makeAction(ACT_RELAY, 5);  // linked to rule id 5
    TEST_ASSERT_TRUE(rulesActionDesired(a, linked(true, true), false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(a, linked(false, true), false, ACT_OVR_AUTO));
}

static void test_action_relay_failsafe_on_untrustworthy(void) {
    Action aOn  = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    Action aOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    // source untrustworthy: ignore ruleActive, assume failsafe
    TEST_ASSERT_TRUE(rulesActionDesired(aOn,  linked(false, false), false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(aOff, linked(true,  false), false, ACT_OVR_AUTO));
}

static void test_action_nonrelay_deasserts_on_untrustworthy(void) {
    Action a = makeAction(ACT_LED, 5, ACT_FAILSAFE_ON);  // failsafe ignored for non-RELAY
    TEST_ASSERT_FALSE(rulesActionDesired(a, linked(true, false), false, ACT_OVR_AUTO));
}

// --- Operator override (#329) ------------------------------------------

// An override beats the Rule outright, in both directions.
static void test_action_override_beats_rule(void) {
    Action a = makeAction(ACT_RELAY, 5);
    TEST_ASSERT_TRUE(rulesActionDesired(a, linked(false, true), false, ACT_OVR_FORCE_ON));
    TEST_ASSERT_FALSE(rulesActionDesired(a, linked(true, true), false, ACT_OVR_FORCE_OFF));
}

// THE decision this issue turned on: a forced state also beats the FAILSAFE. If
// the sensor is what died, the operator still needs the fan on — so the failsafe
// must not be able to countermand a deliberate force. Both failsafe polarities.
static void test_action_override_beats_failsafe(void) {
    Action failOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    TEST_ASSERT_TRUE(rulesActionDesired(failOff, linked(false, /*trustworthy*/false), false,
                                          ACT_OVR_FORCE_ON));
    Action failOn = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    TEST_ASSERT_FALSE(rulesActionDesired(failOn, linked(true, /*trustworthy*/false), false,
                                           ACT_OVR_FORCE_OFF));
}

// AUTO is exactly today's behavior — the override tier must be inert by default.
static void test_action_override_auto_is_transparent(void) {
    Action a = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    TEST_ASSERT_TRUE(rulesActionDesired(a, linked(true, true), false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(a, linked(false, true), false, ACT_OVR_AUTO));
    TEST_ASSERT_TRUE(rulesActionDesired(a, linked(false, false), false, ACT_OVR_AUTO));  // failsafe
}

// An override on an unlinked action wins over the manual latch too (the CLI/BLE
// router never sets one there, but the resolution order must be unambiguous).
static void test_action_override_beats_manual_latch(void) {
    Action a = makeAction(ACT_RELAY, RULES_ID_NONE);
    TEST_ASSERT_TRUE(rulesActionDesired(a, manualOnly(), /*manual*/false, ACT_OVR_FORCE_ON));
    TEST_ASSERT_FALSE(rulesActionDesired(a, manualOnly(), /*manual*/true, ACT_OVR_FORCE_OFF));
}

// #294 regression, at the resolution layer. The manager's rulesActionReleaseDrive()
// is what enforces this in the firmware (it clears the manual latch alongside the
// override on every link change), but the reason it MUST is visible right here: an
// action that is rule-driven ignores the latch entirely, so a latch left armed is
// invisible until the link goes away -- and then drives the pin with no command.
static void test_action_stale_manual_latch_would_snap_on_after_unlink(void) {
    Action linkedAction = makeAction(ACT_RELAY, 5);
    // While linked, a stale latch is correctly ignored: rule says off, output off.
    TEST_ASSERT_FALSE(rulesActionDesired(linkedAction, linked(false, true), /*manual*/true, ACT_OVR_AUTO));
    // Unlink it with that latch still set and the pin snaps ON, uncommanded. This
    // is the failure mode; the fix is that no code path may reach here with the
    // latch armed, because every link change releases it.
    Action unlinked = makeAction(ACT_RELAY, RULES_ID_NONE);
    TEST_ASSERT_TRUE(rulesActionDesired(unlinked, manualOnly(), /*manual*/true, ACT_OVR_AUTO));
    // Released (the invariant rulesActionReleaseDrive maintains) -> stays off.
    TEST_ASSERT_FALSE(rulesActionDesired(unlinked, manualOnly(), /*manual*/false, ACT_OVR_AUTO));
}


// --- Multi-rule membership (#343) --------------------------------------

// The issue's motivating case: a cooling fan several sensors can demand. Any one
// active member asserts the output; all quiet -> off.
static void test_action_or_combines_members(void) {
    Action fan = makeAction(ACT_RELAY, 5);
    TEST_ASSERT_TRUE(rulesActionDesired(fan, members(3, /*anyActive*/true, false), false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(fan, members(3, /*anyActive*/false, false), false, ACT_OVR_AUTO));
}

// THE multi-source failsafe decision: a live member's demand outranks a dead
// sibling's unknown. One broken sensor must not cut a fan a healthy sensor is
// legitimately calling for -- in BOTH failsafe polarities.
static void test_action_live_member_outranks_faulted_sibling(void) {
    Action failOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    TEST_ASSERT_TRUE(rulesActionDesired(failOff, members(2, /*anyActive*/true, /*anyFaulted*/true),
                                          false, ACT_OVR_AUTO));
    Action failOn = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    TEST_ASSERT_TRUE(rulesActionDesired(failOn, members(2, true, true), false, ACT_OVR_AUTO));
}

// With no live demand at all, a faulted member still puts a RELAY in its failsafe.
static void test_action_failsafe_applies_when_no_member_demands(void) {
    Action failOn  = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    Action failOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    TEST_ASSERT_TRUE(rulesActionDesired(failOn,  members(2, false, /*anyFaulted*/true), false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(failOff, members(2, false, /*anyFaulted*/true), false, ACT_OVR_AUTO));
}

// A multi-member action still ignores the manual latch, and an override still
// outranks every member (the #329 tier is unchanged by membership).
static void test_action_override_beats_all_members(void) {
    Action fan = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    TEST_ASSERT_FALSE(rulesActionDesired(fan, members(3, true, true), /*manual*/true, ACT_OVR_FORCE_OFF));
    TEST_ASSERT_TRUE(rulesActionDesired(fan, members(3, false, false), /*manual*/false, ACT_OVR_FORCE_ON));
    TEST_ASSERT_FALSE(rulesActionDesired(fan, members(3, false, false), /*manual*/true, ACT_OVR_AUTO));
}

// A dangling member (its Rule record is gone) counts toward memberCount but is
// neither active nor faulted -- the action stays rule-driven and OFF, and must
// NOT fall back to the manual latch (that is the #294 snap-on route).
static void test_action_dangling_member_stays_off_not_manual(void) {
    Action a = makeAction(ACT_RELAY, 5);
    TEST_ASSERT_FALSE(rulesActionDesired(a, members(1, false, false), /*manual*/true, ACT_OVR_AUTO));
}

// --- First verdict: "not sure yet" counts as failed (#669, §11.3) ------------

static void test_unsettled_from_arming_through_warmup(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));                 // INIT
    rulesRuleUpdate(r, ctx, 20.0f, 1001);
    TEST_ASSERT_EQUAL(RULE_WARMUP, ctx.state);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));                 // WARMUP
    r.flags = 0;
    RuleContext off{};
    rulesRuleInit(off, r, 0);
    TEST_ASSERT_FALSE_MESSAGE(rulesRuleUnsettled(off), "a disabled rule is inert, not unsettled");
}

static void test_dead_sensor_never_settles(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    for (uint32_t t = 0; t <= 60000; t += 50) rulesRuleUpdate(r, ctx, NAN, t);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_WARMUP, ctx.state, "WARMUP is only left on a real reading");
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
}

static void test_cold_first_reading_settles(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 20.0f);
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleUnsettled(ctx), "READY alone is not a verdict");
    rulesRuleUpdate(r, ctx, 20.0f, 1650);
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleUnsettled(ctx), "#668: one clear reading is not a verdict");
    rulesRuleUpdate(r, ctx, 20.0f, 1850);                      // debounceMs (200) of clear readings
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

// The hot restart: the first readings assert, and the rule waits out its
// persist. That wait is still unsettled, so a FAILSAFE ON fan isn't dropped.
static void test_hot_first_trip_stays_unsettled_until_active(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 120.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 1650);                     // trip pending
    TEST_ASSERT_TRUE(rulesRuleTripPending(ctx));
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
    rulesRuleUpdate(r, ctx, 120.0f, 1850);                     // 200 ms persist
    TEST_ASSERT_TRUE(rulesRuleIsActive(ctx));
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

// A reading inside the hysteresis band is what a fan that was running leaves
// behind: it doesn't settle the rule; only clearing the ACTIVE test does.
static void test_reading_in_hysteresis_band_does_not_settle(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, /*hyst*/5.0f);
    RuleContext ctx{};
    toReady(r, ctx, 97.0f);
    for (uint32_t t = 1650; t < 9000; t += 50) rulesRuleUpdate(r, ctx, 97.0f, t);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleUnsettled(ctx), "95 < 97 <= 100: not a verdict");
    rulesRuleUpdate(r, ctx, 94.0f, 9000);
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleUnsettled(ctx), "#668: the verdict needs debounceMs of them");
    rulesRuleUpdate(r, ctx, 94.0f, 9200);
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

static void test_noisy_sample_during_first_trip_does_not_settle(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, /*hyst*/5.0f);
    RuleContext ctx{};
    toReady(r, ctx, 101.0f);
    rulesRuleUpdate(r, ctx, 101.0f, 1650);
    rulesRuleUpdate(r, ctx, 99.0f, 1700);                      // one sample under the limit
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
}

static void test_hysteresis_zero_settles_on_first_clear(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, /*hyst*/0.0f);
    RuleContext ctx{};
    toReady(r, ctx, 99.0f);
    rulesRuleUpdate(r, ctx, 99.0f, 1650);
    rulesRuleUpdate(r, ctx, 99.0f, 1850);                      // debounceMs of clear readings (#668)
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

static void test_nan_in_ready_is_no_verdict(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 20.0f);
    rulesRuleUpdate(r, ctx, NAN, 1650);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
}

// Once settled, a later pending trip is ordinary debounce: no failsafe.
static void test_settled_rule_stays_settled_through_later_trips(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 20.0f);
    rulesRuleUpdate(r, ctx, 20.0f, 1650);
    rulesRuleUpdate(r, ctx, 20.0f, 1850);                      // settled (#668: after debounceMs)
    rulesRuleUpdate(r, ctx, 150.0f, 1900);
    TEST_ASSERT_TRUE(rulesRuleTripPending(ctx));
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

static void test_disable_clears_and_rearm_sets_again(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    rulesRuleInit(ctx, r, 0);
    r.flags = 0;
    rulesRuleUpdate(r, ctx, 20.0f, 100);
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
    TEST_ASSERT_EQUAL_UINT8(0, ctx.verdictPending);
    r.flags = RULE_FLAG_ENABLED;
    rulesRuleUpdate(r, ctx, 20.0f, 200);                       // DISABLED case re-arms
    TEST_ASSERT_EQUAL(RULE_INIT, ctx.state);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
}

static void test_action_failsafe_while_unsettled(void) {
    Action aOn  = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_ON);
    Action aOff = makeAction(ACT_RELAY, 5, ACT_FAILSAFE_OFF);
    Action led  = makeAction(ACT_LED, 5, ACT_FAILSAFE_ON);
    ActionDrive d{};
    d.memberCount = 2;
    d.anyUnsettled = true;
    TEST_ASSERT_TRUE(rulesActionDesired(aOn, d, false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(aOff, d, false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE(rulesActionDesired(led, d, false, ACT_OVR_AUTO));
    TEST_ASSERT_FALSE_MESSAGE(rulesActionDesired(aOn, d, false, ACT_OVR_FORCE_OFF), "a force outranks");
    d.anyActive = true;
    TEST_ASSERT_TRUE(rulesActionDesired(aOff, d, false, ACT_OVR_AUTO));   // a live demand outranks
}

// --- #668: every state change waits debounceMs, in both directions -----------
// The tests below drive rulesRuleUpdate() with real samples and time (50 ms
// poll, debounce 200 unless said) and never pin ctx fields.

// READY -> ACTIVE on the default rule (above 100, clears below 98); returns t.
static uint32_t toActive(const Rule& r, RuleContext& ctx) {
    toReady(r, ctx, 50.0f);
    uint32_t t = 1602;
    feed(r, ctx, 120.0f, t, 300);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    return t;
}

static void test_clear_noise_at_the_limit_never_leaves_active(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    for (int i = 0; i < 100; i++) {          // 5 s of asserted / clear alternation
        t += 50; rulesRuleUpdate(r, ctx, (i & 1) ? 50.0f : 120.0f, t);
        TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "noise must not clear the rule");
    }
}

static void test_clear_needs_debounce_of_continuous_clear_readings(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    feed(r, ctx, 50.0f, t, 200);             // timer started at the first sample: 150 ms elapsed
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    feed(r, ctx, 50.0f, t, 50);              // 200 ms
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.clearSince);
}

static void test_clear_restarted_by_an_asserted_sample_or_a_hysteresis_reading(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    feed(r, ctx, 50.0f, t, 150);
    feed(r, ctx, 120.0f, t, 50);             // asserted: restart
    feed(r, ctx, 50.0f, t, 200);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "restarted, 150 ms of the new count");
    feed(r, ctx, 99.0f, t, 50);              // inside the hysteresis band still asserts: restart
    feed(r, ctx, 50.0f, t, 200);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    feed(r, ctx, 50.0f, t, 50);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

static void test_debounce_zero_keeps_the_instant_clear(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f, 0.0f, 2.0f, 500, /*debounce*/0);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    rulesRuleUpdate(r, ctx, 120.0f, 1700);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "instant trip");
    rulesRuleUpdate(r, ctx, 50.0f, 1750);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_READY, ctx.state, "instant clear");
}

// --- warning-tier latch ---

static void test_warn_latch_enters_and_leaves_after_debounce(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);   // warn edge 90
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    uint32_t t = 1602;
    feed(r, ctx, 95.0f, t, 150);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));
    TEST_ASSERT_TRUE_MESSAGE(rulesRuleWarnPending(ctx), "entry pending");
    feed(r, ctx, 95.0f, t, 100);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
    TEST_ASSERT_FALSE(rulesRuleWarnPending(ctx));
    feed(r, ctx, 50.0f, t, 200);             // out of band: 150 ms elapsed
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
    feed(r, ctx, 50.0f, t, 50);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));
}

static void test_warn_latch_no_flicker_from_noise_at_the_edge(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    uint32_t t = 1602;
    for (int i = 0; i < 100; i++) {          // never latches
        t += 50; rulesRuleUpdate(r, ctx, (i & 1) ? 80.0f : 95.0f, t);
        TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));
    }
    feed(r, ctx, 95.0f, t, 300);             // latch it
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
    for (int i = 0; i < 100; i++) {          // never unlatches
        t += 50; rulesRuleUpdate(r, ctx, (i & 1) ? 80.0f : 95.0f, t);
        TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
    }
}

static void test_warn_latch_side_refreshes_at_once(void) {
    Rule r = makeRule(RULE_OUTSIDE_BAND, 10.0f, 100.0f);   // warn edges 11 and 90
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    uint32_t t = 1602;
    feed(r, ctx, 10.5f, t, 300);
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_LO, rulesRuleWarnSide(ctx));
    feed(r, ctx, 95.0f, t, 50);              // jumps to the other band: no debounce, still WARNING
    TEST_ASSERT_EQUAL(RULE_WARN_SIDE_HI, rulesRuleWarnSide(ctx));
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
}

static void test_active_to_ready_through_the_warn_band_shows_warning_at_once(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    TEST_ASSERT_EQUAL(RULE_SEV_ALARM, rulesRuleAnnunciatorSeverity(r, ctx));
    for (int i = 0; i < 5; i++) {            // 95: clears the limit, sits in the warn band
        t += 50; rulesRuleUpdate(r, ctx, 95.0f, t);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx),
                                      "never NORMAL on the way down");
    }
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
}

// --- NaN ---

static void test_nan_gap_restarts_the_clear_timer_and_keeps_state(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    feed(r, ctx, 50.0f, t, 150);             // 100 ms into the clear
    feed(r, ctx, NAN, t, 50);                // gap: restart
    TEST_ASSERT_EQUAL_UINT32(0, ctx.clearSince);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    feed(r, ctx, 50.0f, t, 200);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, ctx.state, "count restarted after the gap");
    feed(r, ctx, 50.0f, t, 50);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
}

static void test_nan_gap_keeps_the_warn_latch_and_restarts_its_timer(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 50.0f);
    uint32_t t = 1602;
    feed(r, ctx, 95.0f, t, 300);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx));
    feed(r, ctx, 50.0f, t, 100);             // leave pending
    feed(r, ctx, NAN, t, 500);               // gap shorter than 2 s
    TEST_ASSERT_EQUAL_UINT32(0, ctx.warnSince);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, ctx), "latch held");
}

static void test_nan_past_the_fault_debounce_goes_ready_at_once_and_drops_the_latch(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    const uint32_t t0 = t + 50;
    while (t < t0 + RULES_FAULT_DEBOUNCE_MS - 50) {
        t += 50; rulesRuleUpdate(r, ctx, NAN, t);
        TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    }
    t += 50; rulesRuleUpdate(r, ctx, NAN, t);   // 2 s of NaN: READY, no extra debounce
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, ctx));

    // A latched WARNING on a dead sensor drops too.
    RuleContext w{};
    toReady(r, w, 50.0f);
    uint32_t u = 1602;
    feed(r, w, 95.0f, u, 300);
    TEST_ASSERT_EQUAL(RULE_SEV_WARNING, rulesRuleAnnunciatorSeverity(r, w));
    feed(r, w, NAN, u, RULES_FAULT_DEBOUNCE_MS + 50);
    TEST_ASSERT_EQUAL(RULE_SEV_NONE, rulesRuleAnnunciatorSeverity(r, w));
}

// #668 decision 4, accepted on purpose: a NaN restarts the clear timer, so a
// flaky sensor (clear, NaN, clear, NaN ...) never accumulates debounceMs of
// continuous clear readings and keeps an ACTIVE rule (and its relay) on. A
// gap of 2 s or more is the only thing that releases it. Don't "fix" this
// without reopening the decision.
static void test_flaky_sensor_holds_an_active_rule(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    uint32_t t = toActive(r, ctx);
    for (int i = 0; i < 400; i++) {          // 20 s >> debounce + 2 s
        t += 50; rulesRuleUpdate(r, ctx, (i & 1) ? NAN : 50.0f, t);
        TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);
    }
}

// --- #669 first verdict is timed ---

static void test_verdict_needs_debounce_of_continuous_clear_readings(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    toReady(r, ctx, 20.0f);
    uint32_t t = 1602;
    feed(r, ctx, 20.0f, t, 150);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
    feed(r, ctx, 99.0f, t, 50);              // in the hysteresis band: restart
    feed(r, ctx, 20.0f, t, 200);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
    feed(r, ctx, 105.0f, t, 50);             // an asserted sample restarts it too
    feed(r, ctx, 20.0f, t, 200);
    TEST_ASSERT_TRUE(rulesRuleUnsettled(ctx));
    feed(r, ctx, 20.0f, t, 50);
    TEST_ASSERT_FALSE(rulesRuleUnsettled(ctx));
}

// --- timer stamps at now == 0 ---

// A stamp of 0 means "not counting", so a timer started on a tick at now == 0
// must not read as "started long ago" and fire at once. The rule is armed
// 2 s before the 32-bit wrap so a real tick lands on 0.
static void test_a_timer_stamped_at_now_zero_does_not_fire(void) {
    Rule r = makeRule(RULE_ABOVE, 100.0f);
    RuleContext ctx{};
    const uint32_t base = (uint32_t)-2000;   // base + 2000 == 0
    rulesRuleInit(ctx, r, base);
    rulesRuleUpdate(r, ctx, 50.0f, base);
    rulesRuleUpdate(r, ctx, 50.0f, base + 1001);
    rulesRuleUpdate(r, ctx, 50.0f, base + 1602);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    rulesRuleUpdate(r, ctx, 120.0f, 0);      // assert timer starts at 0
    TEST_ASSERT_EQUAL_MESSAGE(RULE_READY, ctx.state, "no instant trip");
    rulesRuleUpdate(r, ctx, 120.0f, 100);
    TEST_ASSERT_EQUAL(RULE_READY, ctx.state);
    rulesRuleUpdate(r, ctx, 120.0f, 250);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, ctx.state);

    // The clear timer too: first clear sample on the tick at 0.
    RuleContext c{};
    rulesRuleInit(c, r, base);
    rulesRuleUpdate(r, c, 50.0f, base);
    rulesRuleUpdate(r, c, 50.0f, base + 1001);
    rulesRuleUpdate(r, c, 50.0f, base + 1602);
    rulesRuleUpdate(r, c, 120.0f, base + 1700);
    rulesRuleUpdate(r, c, 120.0f, base + 1950);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, c.state);
    rulesRuleUpdate(r, c, 50.0f, 0);
    TEST_ASSERT_EQUAL_MESSAGE(RULE_ACTIVE, c.state, "no instant clear");
    rulesRuleUpdate(r, c, 50.0f, 100);
    TEST_ASSERT_EQUAL(RULE_ACTIVE, c.state);
    rulesRuleUpdate(r, c, 50.0f, 250);
    TEST_ASSERT_EQUAL(RULE_READY, c.state);
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
    RUN_TEST(test_trip_side_relatches_past_the_other_limit);
    RUN_TEST(test_nan_never_asserts_and_clears_active);
    RUN_TEST(test_active_rule_rides_out_a_short_nan_gap);
    RUN_TEST(test_nan_gap_restarts_a_ready_rules_persist_timer);
    RUN_TEST(test_source_untrusted_has_no_warmup_stretch);
    RUN_TEST(test_nansince_stamps_on_nan_and_clears_on_finite);
    RUN_TEST(test_nansince_clears_on_disable);
    RUN_TEST(test_nansince_now_zero_stamps_one);
    RUN_TEST(test_nansince_stamps_during_warmup);
    RUN_TEST(test_source_faulted_false_in_init);
    RUN_TEST(test_source_faulted_true_in_warmup_past_debounce);
    RUN_TEST(test_source_faulted_false_when_disabled);
    RUN_TEST(test_inside_band_deferred_never_asserts);
    RUN_TEST(test_severity_reflects_active_state);
    RUN_TEST(test_annun_active_alarm_is_alarm);
    RUN_TEST(test_annun_warning_band_below_limit);
    RUN_TEST(test_annun_warning_only_for_alarm_rules);
    RUN_TEST(test_annun_active_info_is_info);
    RUN_TEST(test_annun_no_derived_warning_for_info);
    RUN_TEST(test_info_below_warning_and_alarm);
    RUN_TEST(test_annun_no_warning_during_warmup);
    RUN_TEST(test_annun_outside_band_low_warning);
    RUN_TEST(test_annun_warning_band_negative_above);
    RUN_TEST(test_annun_warning_band_negative_below);
    RUN_TEST(test_annun_outside_band_negative_low_edge);
    RUN_TEST(test_warn_set_lo_on_above);
    RUN_TEST(test_warn_set_lo_on_below);
    RUN_TEST(test_warn_none_never_warns_goes_straight_to_alarm);
    RUN_TEST(test_warn_outside_set_lo_auto_hi);
    RUN_TEST(test_warn_outside_none_lo_set_hi);
    RUN_TEST(test_warn_outside_both_set_asymmetric);
    RUN_TEST(test_warn_side_hit_above_below);
    RUN_TEST(test_warn_valid_auto_none_always_legal);
    RUN_TEST(test_warn_valid_above_accepts_below_limit);
    RUN_TEST(test_warn_valid_above_rejects_at_or_past_limit);
    RUN_TEST(test_warn_valid_below_accepts_above_limit_rejects_at_or_under);
    RUN_TEST(test_warn_valid_hi_set_on_above_below_rejected);
    RUN_TEST(test_warn_valid_outside_edge_outside_band_rejected);
    RUN_TEST(test_warn_valid_outside_both_set_requires_lo_less_than_hi);
    RUN_TEST(test_normalize_warn_degrades_invalid_side_and_zeros);
    RUN_TEST(test_normalize_warn_leaves_valid_side_alone);
    RUN_TEST(test_normalize_warn_both_sides_degrade_independently);
    RUN_TEST(test_warn_non_alarm_severity_ignores_modes);
    RUN_TEST(test_action_manual_follows_manual_state);
    RUN_TEST(test_action_follows_rule_when_trustworthy);
    RUN_TEST(test_action_relay_failsafe_on_untrustworthy);
    RUN_TEST(test_action_nonrelay_deasserts_on_untrustworthy);
    RUN_TEST(test_action_override_beats_rule);
    RUN_TEST(test_action_override_beats_failsafe);
    RUN_TEST(test_action_override_auto_is_transparent);
    RUN_TEST(test_action_override_beats_manual_latch);
    RUN_TEST(test_action_stale_manual_latch_would_snap_on_after_unlink);
    RUN_TEST(test_action_or_combines_members);
    RUN_TEST(test_action_live_member_outranks_faulted_sibling);
    RUN_TEST(test_action_failsafe_applies_when_no_member_demands);
    RUN_TEST(test_action_override_beats_all_members);
    RUN_TEST(test_action_dangling_member_stays_off_not_manual);
    RUN_TEST(test_unsettled_from_arming_through_warmup);
    RUN_TEST(test_dead_sensor_never_settles);
    RUN_TEST(test_cold_first_reading_settles);
    RUN_TEST(test_hot_first_trip_stays_unsettled_until_active);
    RUN_TEST(test_reading_in_hysteresis_band_does_not_settle);
    RUN_TEST(test_noisy_sample_during_first_trip_does_not_settle);
    RUN_TEST(test_hysteresis_zero_settles_on_first_clear);
    RUN_TEST(test_nan_in_ready_is_no_verdict);
    RUN_TEST(test_settled_rule_stays_settled_through_later_trips);
    RUN_TEST(test_disable_clears_and_rearm_sets_again);
    RUN_TEST(test_action_failsafe_while_unsettled);
    RUN_TEST(test_clear_noise_at_the_limit_never_leaves_active);
    RUN_TEST(test_clear_needs_debounce_of_continuous_clear_readings);
    RUN_TEST(test_clear_restarted_by_an_asserted_sample_or_a_hysteresis_reading);
    RUN_TEST(test_debounce_zero_keeps_the_instant_clear);
    RUN_TEST(test_warn_latch_enters_and_leaves_after_debounce);
    RUN_TEST(test_warn_latch_no_flicker_from_noise_at_the_edge);
    RUN_TEST(test_warn_latch_side_refreshes_at_once);
    RUN_TEST(test_active_to_ready_through_the_warn_band_shows_warning_at_once);
    RUN_TEST(test_nan_gap_restarts_the_clear_timer_and_keeps_state);
    RUN_TEST(test_nan_gap_keeps_the_warn_latch_and_restarts_its_timer);
    RUN_TEST(test_nan_past_the_fault_debounce_goes_ready_at_once_and_drops_the_latch);
    RUN_TEST(test_flaky_sensor_holds_an_active_rule);
    RUN_TEST(test_verdict_needs_debounce_of_continuous_clear_readings);
    RUN_TEST(test_a_timer_stamped_at_now_zero_does_not_fire);
    return UNITY_END();
}
