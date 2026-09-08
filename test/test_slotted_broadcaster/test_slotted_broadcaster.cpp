// Native unit test: SlottedBroadcaster::tick heartbeat/backpressure bookkeeping
// (src/lib/slotted_broadcaster.h). Pure template — no Arduino/transport deps.
//
// Regression targets:
//  1. A transient send() failure during the heartbeat burst must NOT pin
//     heartbeatDue true and turn the periodic re-send into a per-tick flood.
//     That flood (empty Action slots re-emitted every loop) saturated the BLE
//     link and starved the text channel — the bug behind "framed dump never
//     arrives".
//  2. A heartbeat must RESUME after backpressure rather than being abandoned
//     (#350). A 64-byte UART TX buffer accepts three 21-byte frames, so the
//     old single-burst heartbeat delivered slots 0/1/2 and starved every slot
//     past the third forever — the next heartbeat began again at slot 0.
#include <unity.h>
#include <Arduino.h>

#include "../../src/lib/slotted_broadcaster.h"

// --- test send hook ----------------------------------------------------
static int g_emitCount;
static int g_failSlot;   // slot whose send() returns false (backpressure), or -1

// Slots emitted this pass, in order — resumption is about WHICH slots land, and
// a count alone cannot tell "0,1,2 again" from "3,4,5".
static int g_emitted[64];
static int g_emittedN;

static bool testSend(int slot, uint16_t /*seq*/) {
    if (slot == g_failSlot) return false;  // simulate TX-buffer-full
    if (g_emittedN < (int)(sizeof(g_emitted) / sizeof(g_emitted[0]))) {
        g_emitted[g_emittedN++] = slot;
    }
    g_emitCount++;
    return true;
}

// Models the real transport: a fixed budget of frames per pass, refusing the
// rest. `g_budget` is the number of sends that still fit in the TX buffer.
static int g_budget;
static bool budgetSend(int slot, uint16_t seq) {
    if (g_budget <= 0) return false;
    --g_budget;
    return testSend(slot, seq);
}

static void clearEmitted(void) { g_emittedN = 0; }

// Constant per-slot key -> every slot is "unchanged" after its first emit.
static int  packConst(int /*i*/) { return 42; }
static bool eqInt(int a, int b)  { return a == b; }

void setUp(void)    { g_emitCount = 0; g_failSlot = -1; g_emittedN = 0; g_budget = 0; }
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

    // The TX buffer accepts 2 more frames, then refuses. Refusal is
    // transport-wide, not slot-specific (every sendFrame() failure mode is
    // global), so budgetSend is the model that matches the design rationale for
    // stopping the pass. The sweep parks owing slots 2 and 3.
    g_budget = 2;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);   // 0,1 emit (+2) = 6
    TEST_ASSERT_EQUAL_INT(6, g_emitCount);
    TEST_ASSERT_TRUE(bc.sweepPending());

    // The next tick resumes and finishes the sweep — the two owed slots, ONCE.
    g_budget = 100;
    bc.tick(HB + 1, HB, packConst, eqInt, budgetSend); // 2,3 (+2) = 8
    TEST_ASSERT_EQUAL_INT(8, g_emitCount);
    TEST_ASSERT_FALSE(bc.sweepPending());

    // Then QUIET until the next heartbeat. Pre-fix, lastSendMs_ was frozen so
    // heartbeatDue stuck true -> ~4 emits/tick. Fixed: 0.
    const int before = g_emitCount;
    for (uint32_t t = HB + 2; t < 2 * HB - 1; t += 100) {
        g_budget = 100;
        bc.tick(t, HB, packConst, eqInt, budgetSend);
    }
    TEST_ASSERT_EQUAL_INT(before, g_emitCount);      // no flood
}

// After a backpressured heartbeat, the timer still advances, so the NEXT
// heartbeat fires on schedule (not immediately, not never).
static void test_next_heartbeat_fires_on_schedule(void) {
    SlottedBroadcaster<int, 4> bc;
    bc.reset();
    bc.tick(0, HB, packConst, eqInt, testSend);      // cold: 4
    g_budget = 2;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);     // hb parks at 2: +2 = 6
    g_budget = 100;
    bc.tick(HB + 1, HB, packConst, eqInt, budgetSend); // resume finishes it: +2 = 8
    bc.tick(HB + 2, HB, packConst, eqInt, budgetSend); // done, not due yet: +0
    TEST_ASSERT_EQUAL_INT(8, g_emitCount);
    bc.tick(2 * HB, HB, packConst, eqInt, budgetSend); // next heartbeat: +4 = 12
    TEST_ASSERT_EQUAL_INT(12, g_emitCount);
}


// --- #350: the heartbeat must resume where backpressure parked it ------

// THE #350 GUARD. 8 slots, a transport that accepts 3 frames per pass (64-byte
// buffer / 21-byte frame). Pre-fix, every heartbeat delivered 0,1,2 and slots
// 3..7 were never seen again. Fixed: successive ticks continue the sweep.
static void test_heartbeat_resumes_after_backpressure(void) {
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);                                    // cached: nothing owed yet

    // Heartbeat #1 opens the sweep; only 3 frames fit.
    g_budget = 3;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(3, g_emittedN);
    TEST_ASSERT_EQUAL_INT(0, g_emitted[0]);
    TEST_ASSERT_EQUAL_INT(1, g_emitted[1]);
    TEST_ASSERT_EQUAL_INT(2, g_emitted[2]);
    TEST_ASSERT_TRUE(bc.sweepPending());             // still owes 3..7

    // Next loop iteration, buffer drained: the sweep CONTINUES at slot 3 rather
    // than re-sending 0,1,2. This is the whole bug.
    clearEmitted();
    g_budget = 3;
    bc.tick(HB + 1, HB, packConst, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(3, g_emittedN);
    TEST_ASSERT_EQUAL_INT(3, g_emitted[0]);
    TEST_ASSERT_EQUAL_INT(4, g_emitted[1]);
    TEST_ASSERT_EQUAL_INT(5, g_emitted[2]);

    clearEmitted();
    g_budget = 3;
    bc.tick(HB + 2, HB, packConst, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(2, g_emittedN);            // 6 and 7, then done
    TEST_ASSERT_EQUAL_INT(6, g_emitted[0]);
    TEST_ASSERT_EQUAL_INT(7, g_emitted[1]);
    TEST_ASSERT_FALSE(bc.sweepPending());            // sweep complete

    // Every slot delivered exactly once across the whole heartbeat.
    TEST_ASSERT_EQUAL_INT(8, g_emitCount);
}

// A completed sweep goes quiet: resumption must not become a per-tick re-send.
static void test_completed_sweep_stops_emitting(void) {
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);
    g_budget = 3;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);
    for (uint32_t t = HB + 1; t < 2 * HB - 1; t += 10) {
        g_budget = 100;
        bc.tick(t, HB, packConst, eqInt, budgetSend);
    }
    TEST_ASSERT_EQUAL_INT(8, g_emitCount);           // the 8 owed, nothing more
    TEST_ASSERT_FALSE(bc.sweepPending());
}

// A slot the heartbeat predicate excludes is skipped by the sweep but must
// still emit the instant it changes — the #337 contract, re-checked under
// resumption because `owed` and `changed` are now separate gates.
static int  g_packVal[8];
static int  packArr(int i)   { return g_packVal[i]; }
static bool hbEvenOnly(int i) { return (i % 2) == 0; }

static void test_excluded_slot_still_emits_on_change(void) {
    for (int i = 0; i < 8; ++i) g_packVal[i] = 42;
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);

    g_budget = 2;                                    // force a parked sweep
    bc.tick(HB, HB, packConst, eqInt, budgetSend, hbEvenOnly);
    TEST_ASSERT_EQUAL_INT(0, g_emitted[0]);
    TEST_ASSERT_EQUAL_INT(2, g_emitted[1]);          // odd slots excluded
    TEST_ASSERT_TRUE(bc.sweepPending());

    // Slot 7 (heartbeat-excluded, and BEHIND the parked cursor's remaining
    // work) changes: it must go out on this very tick.
    clearEmitted();
    g_packVal[7] = 99;
    g_budget = 100;
    bc.tick(HB + 1, HB, packArr, eqInt, budgetSend, hbEvenOnly);
    bool saw7 = false;
    for (int i = 0; i < g_emittedN; ++i) if (g_emitted[i] == 7) saw7 = true;
    TEST_ASSERT_TRUE(saw7);
    TEST_ASSERT_FALSE(bc.sweepPending());
}

// A sweep still parked when the next heartbeat comes due restarts from slot 0
// rather than inheriting the stale cursor — otherwise a chronically busy link
// could pin the sweep mid-table and re-starve the head of it.
static void test_new_heartbeat_restarts_parked_sweep(void) {
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);
    g_budget = 3;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);    // 0,1,2 then parked
    TEST_ASSERT_TRUE(bc.sweepPending());

    clearEmitted();
    g_budget = 3;
    bc.tick(2 * HB, HB, packConst, eqInt, budgetSend);  // next heartbeat due
    TEST_ASSERT_EQUAL_INT(0, g_emitted[0]);             // back to the top
    TEST_ASSERT_EQUAL_INT(1, g_emitted[1]);
    TEST_ASSERT_EQUAL_INT(2, g_emitted[2]);
}


// --- the cursor must never move backwards -----------------------------

// A refusal taken at a CHANGED slot below the cursor must not rewind the sweep.
// Rewinding re-sends slots the sweep already served (double-spending a freed
// slot's clear-grace window, which is charged per accepted frame) and, with low
// slots changing every pass, livelocks: the tail is never reached.
static void test_cursor_never_rewinds(void) {
    for (int i = 0; i < 8; ++i) g_packVal[i] = 42;
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);

    g_budget = 3;
    bc.tick(HB, HB, packArr, eqInt, budgetSend);     // 0,1,2 then parks at 3
    TEST_ASSERT_EQUAL_INT(3, g_emittedN);
    TEST_ASSERT_TRUE(bc.sweepPending());

    // Slot 1 changes and the link is closed: the pass breaks at slot 1, which
    // is BELOW the parked cursor. Nothing may be emitted, and the cursor must
    // stay at 3.
    clearEmitted();
    g_packVal[1] = 99;
    g_budget = 0;
    bc.tick(HB + 1, HB, packArr, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(0, g_emittedN);
    TEST_ASSERT_FALSE(bc.sweepDraining());           // no progress -> not draining

    // Link reopens: the sweep resumes at 3 — NOT at 1 — and finishes, then
    // slot 1's change goes out behind it. Pre-fix the cursor had rewound and
    // slots 1,2 were re-sent as heartbeat frames inside this same sweep.
    clearEmitted();
    g_budget = 100;
    bc.tick(HB + 2, HB, packArr, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(6, g_emittedN);            // 3,4,5,6,7 then 1
    TEST_ASSERT_EQUAL_INT(3, g_emitted[0]);          // sweep first
    TEST_ASSERT_EQUAL_INT(7, g_emitted[4]);
    TEST_ASSERT_EQUAL_INT(1, g_emitted[5]);          // then the changed slot
    TEST_ASSERT_FALSE(bc.sweepPending());
}

// The livelock itself: three low slots changing every pass against a 3-frame
// budget. Pre-fix the cursor rewound to the changed slot every pass and slots
// 3..7 were served ZERO times while the sweep never completed.
static void test_busy_low_slots_do_not_livelock_the_sweep(void) {
    for (int i = 0; i < 8; ++i) g_packVal[i] = 42;
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);

    int churn = 0;
    for (int pass = 0; pass < 40; ++pass) {
        for (int i = 0; i < 3; ++i) g_packVal[i] = ++churn;  // low slots churn
        g_budget = 3;
        bc.tick(HB + pass, HB, packArr, eqInt, budgetSend);
        if (!bc.sweepPending()) break;                       // sweep finished
    }
    TEST_ASSERT_FALSE(bc.sweepPending());            // it completed
    bool sawTail = false;
    for (int i = 0; i < g_emittedN; ++i) if (g_emitted[i] >= 3) sawTail = true;
    TEST_ASSERT_TRUE(sawTail);                       // tail actually served
}

// --- a stalled sweep must not claim to be draining --------------------

// Framed mode off => sendFrame() refuses unconditionally. sweeping_ latches
// true (the next heartbeat is what clears it), so a caller keying its rate
// gate on sweepPending() alone would run at loop rate for the rest of the boot.
// sweepDraining() is false throughout, because the cursor never advances.
static void test_durably_refusing_transport_never_claims_draining(void) {
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);
    for (int pass = 0; pass < 50; ++pass) {
        g_budget = 0;                                // transport always refuses
        bc.tick(HB + pass, HB, packConst, eqInt, budgetSend);
        TEST_ASSERT_FALSE(bc.sweepDraining());
    }
    TEST_ASSERT_EQUAL_INT(0, g_emitCount);
}

// The healthy case still gets the fast path: a parked sweep that moved the
// cursor IS draining, and stops being so the moment it completes.
static void test_progressing_sweep_reports_draining(void) {
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);
    g_budget = 3;
    bc.tick(HB, HB, packConst, eqInt, budgetSend);
    TEST_ASSERT_TRUE(bc.sweepDraining());
    g_budget = 100;
    bc.tick(HB + 1, HB, packConst, eqInt, budgetSend);
    TEST_ASSERT_FALSE(bc.sweepDraining());           // completed
    TEST_ASSERT_FALSE(bc.sweepPending());
}

// --- occupancy + the post-vacate grace window (#337/#349/#352) ---------
//
// These used to be copy-pasted into every subsystem (identical arrays, arm loop
// and spend, differing only in the occupancy predicate), and each of the four
// bugs above was fixed one subsystem at a time. The template owns them now, so
// they are tested once, here, at the level the guarantee lives at.

// Named so the grace-window constant can be used inside a Unity macro (the
// template's comma would otherwise split the macro's arguments).
typedef SlottedBroadcaster<int, 8> BC8;

static bool g_occ[8];
static bool occArr(int i) { return g_occ[i]; }

// Key reflects occupancy: an empty slot packs to 0 (the "tile is gone" frame),
// an occupied one to a nonzero value that changes when g_packVal does.
static int packOcc(int i) { return g_occ[i] ? (100 + g_packVal[i]) : 0; }

static int emitCountFor(int slot) {
    int n = 0;
    for (int i = 0; i < g_emittedN; ++i) if (g_emitted[i] == slot) ++n;
    return n;
}

static void occSetUp(void) {
    memset(g_occ, 0, sizeof(g_occ));
    for (int i = 0; i < 8; ++i) g_packVal[i] = 0;
    g_budget = 100;
}

// ARM-ON-EDGE + GRACE EXPIRY. A slot that goes empty keeps taking part in the
// heartbeat for exactly kGraceFrames accepted frames (the edge-triggered change
// push plus kGraceHeartbeats repeats), then goes quiet for good. Without the
// window a single lost clear leaves a stale tile forever; without the expiry we
// are back to sweeping every empty slot every heartbeat (#337/#365).
static void test_vacated_slot_beats_for_the_grace_window_then_stops(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);                                    // empty key cached everywhere

    g_occ[0] = true;
    bc.tick(1, HB, packOcc, eqInt, budgetSend, occArr);   // slot 0 appears
    TEST_ASSERT_EQUAL_INT(1, emitCountFor(0));

    clearEmitted();
    g_occ[0] = false;                               // …and is freed
    bc.tick(2, HB, packOcc, eqInt, budgetSend, occArr);
    TEST_ASSERT_EQUAL_INT(1, emitCountFor(0));      // the clearing frame

    // Three heartbeat repeats, then silence — and never any other slot.
    for (int hb = 1; hb <= 6; ++hb) {
        g_budget = 100;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    TEST_ASSERT_EQUAL_INT(BC8::kGraceFrames, emitCountFor(0));
    for (int i = 1; i < 8; ++i) TEST_ASSERT_EQUAL_INT(0, emitCountFor(i));
}

// #349 AT TEMPLATE LEVEL. The window is spent per frame the transport ACCEPTED,
// never per heartbeat edge. The subsystem could only infer acceptance — the
// template is the one thing that sees send()'s return value. Counting edges
// burned the whole window on refused sends: on a link busy enough to starve the
// heartbeat, the window expired having delivered nothing at all, which is
// exactly the self-healing it exists to provide (bench-confirmed on teensy40).
static void test_grace_window_is_spent_per_accepted_frame_only(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);

    g_occ[0] = true;
    bc.tick(1, HB, packOcc, eqInt, budgetSend, occArr);
    clearEmitted();

    // Freed onto a closed link: the clearing frame and two whole heartbeats are
    // refused. Nothing was delivered, so nothing may be charged.
    g_occ[0] = false;
    g_budget = 0;
    bc.tick(2, HB, packOcc, eqInt, budgetSend, occArr);
    for (int hb = 1; hb <= 2; ++hb) {
        g_budget = 0;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    TEST_ASSERT_EQUAL_INT(0, emitCountFor(0));

    // The link reopens: the full window is still owed.
    g_budget = 100;
    bc.tick(2 * HB + 1, HB, packOcc, eqInt, budgetSend, occArr);   // parked sweep drains
    for (int hb = 3; hb <= 8; ++hb) {
        g_budget = 100;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    TEST_ASSERT_EQUAL_INT(BC8::kGraceFrames, emitCountFor(0));
}

// #352 AT TEMPLATE LEVEL. The broadcasters are brought up before the config
// load, so an occupancy snapshot taken at init() reads a BSS-zeroed table —
// every slot's id/pin is 0, which no occupancy predicate can tell from a real
// record at 0. prime()/reset() therefore seed "nothing was occupied", which is
// correct by construction: the first tick records the truth and can only arm on
// a LATER vacate. Snapshotting instead made the first tick see N - n phantom
// occupied->vacated edges and drip a "tile is gone" frame out of every slot
// that had never held anything.
//
// This is also why prime()/reset() take a Key BY VALUE rather than a
// "read the table" callback: the signature itself makes the #352 mistake
// impossible to reintroduce at this layer. The per-broadcaster boot-order
// fixtures (boot_order_fixture.h; test_broadcaster.cpp,
// test_action_broadcaster.cpp, test_live_value_broadcaster.cpp) are what
// verify each concrete init() upholds its half of the contract — this test
// verifies the template's (#369).
static void test_seed_empty_never_clears_never_occupied_slots(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);                       // …at boot, before the table is loaded

    g_occ[0] = true;                   // the config load lands afterwards
    g_occ[1] = true;

    for (int hb = 1; hb <= 12; ++hb) { // ~a minute of heartbeats
        g_budget = 100;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    for (int i = 2; i < 8; ++i)
        TEST_ASSERT_EQUAL_INT(0, emitCountFor(i));   // never occupied: silent
    TEST_ASSERT_GREATER_THAN_INT(0, emitCountFor(0));
    TEST_ASSERT_GREATER_THAN_INT(0, emitCountFor(1));
}

// reset() carries the same guarantee as prime(): it must not leave occupancy
// asserted from whatever the object held before.
static void test_reset_seeds_occupancy_empty(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);
    for (int i = 0; i < 8; ++i) g_occ[i] = true;
    bc.tick(1, HB, packOcc, eqInt, budgetSend, occArr);   // everything occupied

    bc.reset();
    memset(g_occ, 0, sizeof(g_occ));                     // …and the table empties
    clearEmitted();
    // reset() means "nothing cached", so the first tick emits every slot once —
    // but no slot may be inside a grace window, so the next heartbeats are quiet.
    bc.tick(2, HB, packOcc, eqInt, budgetSend, occArr);
    clearEmitted();
    for (int hb = 1; hb <= 6; ++hb) {
        g_budget = 100;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    TEST_ASSERT_EQUAL_INT(0, g_emittedN);
}

// The heartbeat covers occupied slots only, and change detection is untouched
// by that: an empty, heartbeat-excluded slot still emits the instant it is
// filled (#337 — excluding a slot must never make a transition invisible).
static void test_heartbeat_is_occupied_only_but_change_always_emits(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);
    g_occ[3] = true;
    bc.tick(1, HB, packOcc, eqInt, budgetSend, occArr);

    clearEmitted();
    for (int hb = 1; hb <= 3; ++hb) {
        g_budget = 100;
        bc.tick(hb * HB, HB, packOcc, eqInt, budgetSend, occArr);
    }
    TEST_ASSERT_EQUAL_INT(3, emitCountFor(3));           // the occupied one beats
    TEST_ASSERT_EQUAL_INT(3, g_emittedN);                // …and only it

    clearEmitted();
    g_occ[6] = true;                                     // a fresh record appears
    bc.tick(3 * HB + 1, HB, packOcc, eqInt, budgetSend, occArr);
    TEST_ASSERT_EQUAL_INT(1, emitCountFor(6));
}

// AlarmBroadcaster's shape: no occupancy predicate, heartbeatMs 0. Every slot
// counts as permanently occupied, so the grace window never arms and the
// broadcaster stays purely edge-triggered — identical to its behaviour before
// the occupancy machinery moved into the template.
static void test_no_occupancy_predicate_is_edge_only(void) {
    for (int i = 0; i < 8; ++i) g_packVal[i] = 42;
    SlottedBroadcaster<int, 8> bc;
    bc.prime(42);
    g_budget = 100;
    for (uint32_t t = 1; t < 4 * HB; t += 137) {
        g_budget = 100;
        bc.tick(t, /*heartbeatMs=*/0, packArr, eqInt, budgetSend);
    }
    TEST_ASSERT_EQUAL_INT(0, g_emitCount);               // quiet without a transition

    g_packVal[5] = 7;                                    // one transition
    g_budget = 100;
    bc.tick(4 * HB, 0, packArr, eqInt, budgetSend);
    TEST_ASSERT_EQUAL_INT(1, g_emitCount);
    for (uint32_t t = 4 * HB + 1; t < 8 * HB; t += 137) {
        g_budget = 100;
        bc.tick(t, 0, packArr, eqInt, budgetSend);
    }
    TEST_ASSERT_EQUAL_INT(1, g_emitCount);               // …and no repeats
}

// invalidateActive(): a reconnecting client rebuilds the tiles it can have —
// the occupied slots plus any still owed a clear. A slot that has never held
// anything has no tile to rebuild and must stay silent (#337/#352).
static void test_invalidate_active_covers_occupied_and_grace_only(void) {
    occSetUp();
    SlottedBroadcaster<int, 8> bc;
    bc.prime(0);
    g_occ[0] = true;
    g_occ[1] = true;
    bc.tick(1, HB, packOcc, eqInt, budgetSend, occArr);
    g_occ[1] = false;                                    // slot 1 freed…
    bc.tick(2, HB, packOcc, eqInt, budgetSend, occArr);  // …clear pushed, grace open

    clearEmitted();
    bc.invalidateActive(occArr);                         // a central reconnects
    g_budget = 100;
    bc.tick(3, HB, packOcc, eqInt, budgetSend, occArr);
    TEST_ASSERT_EQUAL_INT(1, emitCountFor(0));           // occupied: re-sent
    TEST_ASSERT_EQUAL_INT(1, emitCountFor(1));           // inside its grace window
    TEST_ASSERT_EQUAL_INT(2, g_emittedN);                // nothing else
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cold_start_emits_all);
    RUN_TEST(test_unchanged_quiet_between_heartbeats);
    RUN_TEST(test_heartbeat_resends_all);
    RUN_TEST(test_backpressure_at_heartbeat_does_not_flood);
    RUN_TEST(test_next_heartbeat_fires_on_schedule);
    RUN_TEST(test_heartbeat_resumes_after_backpressure);
    RUN_TEST(test_completed_sweep_stops_emitting);
    RUN_TEST(test_excluded_slot_still_emits_on_change);
    RUN_TEST(test_new_heartbeat_restarts_parked_sweep);
    RUN_TEST(test_cursor_never_rewinds);
    RUN_TEST(test_busy_low_slots_do_not_livelock_the_sweep);
    RUN_TEST(test_durably_refusing_transport_never_claims_draining);
    RUN_TEST(test_progressing_sweep_reports_draining);
    RUN_TEST(test_vacated_slot_beats_for_the_grace_window_then_stops);
    RUN_TEST(test_grace_window_is_spent_per_accepted_frame_only);
    RUN_TEST(test_seed_empty_never_clears_never_occupied_slots);
    RUN_TEST(test_reset_seeds_occupancy_empty);
    RUN_TEST(test_heartbeat_is_occupied_only_but_change_always_emits);
    RUN_TEST(test_no_occupancy_predicate_is_edge_only);
    RUN_TEST(test_invalidate_active_covers_occupied_and_grace_only);
    return UNITY_END();
}
