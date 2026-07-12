// Native unit test: SlottedBroadcaster::tick heartbeat/backpressure bookkeeping
// (src/lib/slotted_broadcaster.h). Pure template — no Arduino/transport deps.
//
// Regression target: a transient send() failure during the heartbeat burst must
// NOT pin heartbeatDue true and turn the periodic re-send into a per-tick flood.
// That flood (empty Action slots re-emitted every loop) saturated the BLE link
// and starved the text channel — the bug behind "framed dump never arrives".
#include <unity.h>
#include <Arduino.h>

#include "../../src/lib/slotted_broadcaster.h"

// --- test send hook ----------------------------------------------------
static int g_emitCount;
static int g_failSlot;   // slot whose send() returns false (backpressure), or -1

static bool testSend(int slot, uint16_t /*seq*/) {
    if (slot == g_failSlot) return false;  // simulate TX-buffer-full
    g_emitCount++;
    return true;
}

// Constant per-slot key -> every slot is "unchanged" after its first emit.
static int  packConst(int /*i*/) { return 42; }
static bool eqInt(int a, int b)  { return a == b; }

void setUp(void)    { g_emitCount = 0; g_failSlot = -1; }
void tearDown(void) {}

static const uint32_t HB = 5000;

// --- tests -------------------------------------------------------------

static void test_cold_start_emits_all(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0, HB, packConst, eqInt, testSend);
    TEST_ASSERT_EQUAL_INT(4, g_emitCount);   // all slots on cold start
}

static void test_unchanged_quiet_between_heartbeats(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0,    HB, packConst, eqInt, testSend);   // cold: 4
    bc.tick(100,  HB, packConst, eqInt, testSend);   // unchanged, no heartbeat
    bc.tick(4999, HB, packConst, eqInt, testSend);   // still < HB
    TEST_ASSERT_EQUAL_INT(4, g_emitCount);
}

static void test_heartbeat_resends_all(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0,  HB, packConst, eqInt, testSend);     // cold: 4
    bc.tick(HB, HB, packConst, eqInt, testSend);     // heartbeat: +4
    TEST_ASSERT_EQUAL_INT(8, g_emitCount);
}

// THE REGRESSION GUARD: a failed send at a heartbeat must not flood.
static void test_backpressure_at_heartbeat_does_not_flood(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0, HB, packConst, eqInt, testSend);      // cold: 4
    TEST_ASSERT_EQUAL_INT(4, g_emitCount);

    g_failSlot = 2;                                  // slot 2 backpressures
    bc.tick(HB, HB, packConst, eqInt, testSend);     // heartbeat: 0,1,3 emit (+3)=7
    TEST_ASSERT_EQUAL_INT(7, g_emitCount);

    // Ticks before the next heartbeat must stay QUIET. Pre-fix, lastSendMs_ was
    // frozen so heartbeatDue stuck true -> ~4 emits/tick. Fixed: 0.
    g_failSlot = -1;
    const int before = g_emitCount;
    for (uint32_t t = HB + 1; t < 2 * HB - 1; t += 100) {
        bc.tick(t, HB, packConst, eqInt, testSend);
    }
    TEST_ASSERT_EQUAL_INT(before, g_emitCount);      // no flood
}

// After a backpressured heartbeat, the timer still advances, so the NEXT
// heartbeat fires on schedule (not immediately, not never).
static void test_next_heartbeat_fires_on_schedule(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0, HB, packConst, eqInt, testSend);      // cold: 4
    g_failSlot = 2;
    bc.tick(HB, HB, packConst, eqInt, testSend);     // hb w/ 1 fail: +3 = 7
    g_failSlot = -1;
    bc.tick(HB + 1, HB, packConst, eqInt, testSend); // not due yet: +0
    TEST_ASSERT_EQUAL_INT(7, g_emitCount);
    bc.tick(2 * HB, HB, packConst, eqInt, testSend); // next heartbeat: +4 = 11
    TEST_ASSERT_EQUAL_INT(11, g_emitCount);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cold_start_emits_all);
    RUN_TEST(test_unchanged_quiet_between_heartbeats);
    RUN_TEST(test_heartbeat_resends_all);
    RUN_TEST(test_backpressure_at_heartbeat_does_not_flood);
    RUN_TEST(test_next_heartbeat_fires_on_schedule);
    return UNITY_END();
}
