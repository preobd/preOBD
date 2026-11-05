// Native unit test: the watchdog timeout policy (#541).
//
// One owner for "what should the timeout be now": phase (BOOT/CONFIG/RUN)
// plus nested scopes that only ever widen. These pin the rules the old
// call-site logic got wrong at least once: an inner short scope narrowing an
// outer long one (json_config.cpp, #331 review), CONFIG and RUN changes while
// a scope is open, and an unbalanced pop.
#include <unity.h>
#include <string.h>

#include "../../src/core/watchdog_policy.cpp"

void setUp(void) {}
void tearDown(void) {}

static WatchdogPolicy fresh(void) {
    WatchdogPolicy p;
    memset(&p, 0xEE, sizeof(p));
    watchdogPolicyInit(p);
    return p;
}

static void test_zeroed_storage_is_boot_with_no_scopes(void) {
    // core/watchdog.cpp relies on BSS-zero being a valid initial policy.
    WatchdogPolicy p;
    memset(&p, 0, sizeof(p));
    TEST_ASSERT_EQUAL(WD_PHASE_BOOT, p.phase);
    TEST_ASSERT_EQUAL_UINT16(WD_BOOT_TIMEOUT_MS, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("BOOT", watchdogPolicyTag(p));
}

static void test_phase_timeouts(void) {
    WatchdogPolicy p = fresh();
    TEST_ASSERT_EQUAL_UINT16(8000, watchdogPolicyTimeoutMs(p));
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
    watchdogPolicySetPhase(p, WD_PHASE_CONFIG);
    TEST_ASSERT_EQUAL_UINT16(8000, watchdogPolicyTimeoutMs(p));
}

static void test_scope_widens_and_restores(void) {
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    watchdogPolicyPush(p, 20000, "SD");
    TEST_ASSERT_EQUAL_UINT16(20000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("SD", watchdogPolicyTag(p));
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("LOOP", watchdogPolicyTag(p));   // past boot, no scope
}

static void test_scope_never_narrows_the_phase(void) {
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_CONFIG);
    watchdogPolicyPush(p, 1000, "SHORT");
    TEST_ASSERT_EQUAL_UINT16(8000, watchdogPolicyTimeoutMs(p));
}

static void test_inner_short_scope_keeps_outer_wide(void) {
    // The json_config.cpp bug class: a nested narrower scope must not drop
    // the outer 20 s window to 2 s mid-operation.
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    watchdogPolicyPush(p, 20000, "SD");
    watchdogPolicyPush(p, 3000, "JSON");
    TEST_ASSERT_EQUAL_UINT16(20000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("JSON", watchdogPolicyTag(p));   // innermost names the work
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(20000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("SD", watchdogPolicyTag(p));
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
}

static void test_mode_change_inside_a_scope(void) {
    // RUN while an SD scope is open keeps 20 s until the scope ends, then 2 s.
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_CONFIG);
    watchdogPolicyPush(p, 20000, "SD");
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    TEST_ASSERT_EQUAL_UINT16(20000, watchdogPolicyTimeoutMs(p));
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
}

static void test_extra_pop_is_ignored(void) {
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT8(0, p.depth);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
}

static void test_overflow_scopes_still_widen(void) {
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    for (uint8_t i = 0; i < WD_MAX_SCOPES; i++) watchdogPolicyPush(p, 3000, "IN");
    watchdogPolicyPush(p, 30000, "DEEP");   // past the table
    TEST_ASSERT_EQUAL_UINT16(30000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("DEEP", watchdogPolicyTag(p));
    watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(3000, watchdogPolicyTimeoutMs(p));
    TEST_ASSERT_EQUAL_STRING("IN", watchdogPolicyTag(p));
    for (uint8_t i = 0; i < WD_MAX_SCOPES; i++) watchdogPolicyPop(p);
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
}

static void test_overflow_errs_wide_until_last_closes(void) {
    // Overflow scopes are tracked by their maximum only, so a narrower one
    // closing first can't narrow the timeout under the wider one still open.
    WatchdogPolicy p = fresh();
    watchdogPolicySetPhase(p, WD_PHASE_RUN);
    for (uint8_t i = 0; i < WD_MAX_SCOPES; i++) watchdogPolicyPush(p, 1, "IN");
    watchdogPolicyPush(p, 30000, "WIDE");
    watchdogPolicyPush(p, 5000, "NARROW");
    watchdogPolicyPop(p);   // NARROW closes
    TEST_ASSERT_EQUAL_UINT16(30000, watchdogPolicyTimeoutMs(p));
    watchdogPolicyPop(p);   // WIDE closes
    TEST_ASSERT_EQUAL_UINT16(2000, watchdogPolicyTimeoutMs(p));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zeroed_storage_is_boot_with_no_scopes);
    RUN_TEST(test_phase_timeouts);
    RUN_TEST(test_scope_widens_and_restores);
    RUN_TEST(test_scope_never_narrows_the_phase);
    RUN_TEST(test_inner_short_scope_keeps_outer_wide);
    RUN_TEST(test_mode_change_inside_a_scope);
    RUN_TEST(test_extra_pop_is_ignored);
    RUN_TEST(test_overflow_scopes_still_widen);
    RUN_TEST(test_overflow_errs_wide_until_last_closes);
    return UNITY_END();
}
