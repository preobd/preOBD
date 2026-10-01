// Native unit test: OBD-II Mode 01 request pacing (src/inputs/can_poll.h, #476).
//
// CAN input used to be passive only, so an imported PID read NaN unless
// another tester polled the ECU. Pins the request frame, the ECU addressing
// rule, and the response-driven pacing / dead-bus backoff.
#include <unity.h>
#include <Arduino.h>

#include "../../src/inputs/can_poll.h"
#include "../../src/inputs/can_frame_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_request_frame(void) {
    uint8_t f[8];
    for (uint8_t i = 0; i < 8; i++) f[i] = 0xEE;
    buildMode01Request(0x05, f);
    const uint8_t want[8] = {0x02, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f, 8);
}

static void test_addressing(void) {
    TEST_ASSERT_EQUAL_HEX16(0x7E0, obdRequestIdFor(0x7E8));
    TEST_ASSERT_EQUAL_HEX16(0x7E7, obdRequestIdFor(0x7EF));
    TEST_ASSERT_TRUE(isObdResponseId(0x7E8));
    TEST_ASSERT_TRUE(isObdResponseId(0x7EF));
    TEST_ASSERT_FALSE(isObdResponseId(0x7E7));   // a request ID
    TEST_ASSERT_FALSE(isObdResponseId(0x7DF));   // functional request
    TEST_ASSERT_FALSE(isObdResponseId(0x7F0));
    TEST_ASSERT_FALSE(isObdResponseId(0x400));   // custom broadcast
}

// First request goes out after the minimum gap from reset
static void test_first_request_after_gap(void) {
    CanPollState s;
    canPollReset(s, 1000);
    TEST_ASSERT_FALSE(canPollReady(s, 1000 + CAN_POLL_MIN_GAP_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, 1000 + CAN_POLL_MIN_GAP_MS));
}

// An answer lets the next request go out a short gap after the ANSWER, not
// the timeout — an ECU slower than the gap must not get back-to-back requests
static void test_answer_releases_early(void) {
    CanPollState s;
    canPollReset(s, 0);
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    TEST_ASSERT_FALSE(canPollReady(s, 115));          // waiting for the answer
    canPollOnResponse(s, 0x7E8, 0x0C, 115);           // answered after 15 ms
    TEST_ASSERT_FALSE(canPollReady(s, 115 + CAN_POLL_MIN_GAP_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, 115 + CAN_POLL_MIN_GAP_MS));
    TEST_ASSERT_EQUAL_UINT8(0, s.missStreak);
}

// A different PID's answer does not release the outstanding request
static void test_other_answer_does_not_release(void) {
    CanPollState s;
    canPollReset(s, 0);
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    canPollOnResponse(s, 0x7E8, 0x05, 110);
    TEST_ASSERT_TRUE(s.pending);
    TEST_ASSERT_FALSE(canPollReady(s, 100 + CAN_POLL_RESPONSE_TIMEOUT_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, 100 + CAN_POLL_RESPONSE_TIMEOUT_MS));
}

// No answer: the request times out and counts as a miss
static void test_timeout_counts_miss(void) {
    CanPollState s;
    canPollReset(s, 0);
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    TEST_ASSERT_TRUE(canPollReady(s, 100 + CAN_POLL_RESPONSE_TIMEOUT_MS));
    TEST_ASSERT_FALSE(s.pending);
    TEST_ASSERT_EQUAL_UINT8(1, s.missStreak);
}

// Dead bus: consecutive misses back off to one request per CAN_POLL_BACKOFF_MS,
// and the first answer restores full rate
static void test_backoff_and_recovery(void) {
    CanPollState s;
    canPollReset(s, 0);
    uint32_t t = 0;
    for (uint8_t i = 0; i < CAN_POLL_BACKOFF_AFTER; i++) {
        t += CAN_POLL_RESPONSE_TIMEOUT_MS;
        TEST_ASSERT_TRUE(canPollReady(s, t));
        canPollOnSent(s, 0x7E8, 0x0C, true, t);
    }
    // The last request's timeout pushes the streak to the threshold
    t += CAN_POLL_RESPONSE_TIMEOUT_MS;
    TEST_ASSERT_TRUE(canPollReady(s, t));
    TEST_ASSERT_EQUAL_UINT8(CAN_POLL_BACKOFF_AFTER, s.missStreak);
    canPollOnSent(s, 0x7E8, 0x0C, true, t);

    // Backed off: the next request waits CAN_POLL_BACKOFF_MS, not the timeout
    TEST_ASSERT_FALSE(canPollReady(s, t + CAN_POLL_RESPONSE_TIMEOUT_MS));
    TEST_ASSERT_FALSE(canPollReady(s, t + CAN_POLL_BACKOFF_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, t + CAN_POLL_BACKOFF_MS));

    uint32_t sent = t + CAN_POLL_BACKOFF_MS;
    canPollOnSent(s, 0x7E8, 0x0C, true, sent);
    TEST_ASSERT_FALSE(canPollReady(s, sent + CAN_POLL_BACKOFF_MS - 1));
    canPollOnResponse(s, 0x7E8, 0x0C, sent + 12);
    TEST_ASSERT_EQUAL_UINT8(0, s.missStreak);
    TEST_ASSERT_TRUE(canPollReady(s, sent + 12 + CAN_POLL_MIN_GAP_MS));
}

// A refused write (TX full / bus-off) counts as a miss and leaves nothing pending
static void test_failed_write_counts_miss(void) {
    CanPollState s;
    canPollReset(s, 0);
    canPollOnSent(s, 0x7E8, 0x0C, false, 100);
    TEST_ASSERT_FALSE(s.pending);
    TEST_ASSERT_EQUAL_UINT8(1, s.missStreak);
    TEST_ASSERT_TRUE(canPollReady(s, 100 + CAN_POLL_MIN_GAP_MS));
}

// millis() wrap: elapsed is computed unsigned, so pacing survives rollover
static void test_millis_wrap(void) {
    CanPollState s;
    canPollReset(s, 0);
    canPollOnSent(s, 0x7E8, 0x0C, true, 0xFFFFFFF0u);
    TEST_ASSERT_FALSE(canPollReady(s, 0x00000010u));  // 32 ms elapsed
    TEST_ASSERT_TRUE(canPollReady(s, 0xFFFFFFF0u + CAN_POLL_RESPONSE_TIMEOUT_MS));
}

// #507: "PID not supported" (03 7F 01 31) from the polled ECU releases the
// request a gap after the NRC, not after the timeout, and is no miss
static void test_negative_response_releases(void) {
    CanPollState s;
    canPollReset(s, 0);
    s.missStreak = 3;
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    TEST_ASSERT_TRUE(canPollOnNegative(s, 0x7E8, 108));
    TEST_ASSERT_FALSE(s.pending);
    TEST_ASSERT_EQUAL_UINT8(0, s.missStreak);
    TEST_ASSERT_FALSE(canPollReady(s, 108 + CAN_POLL_MIN_GAP_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, 108 + CAN_POLL_MIN_GAP_MS));
    TEST_ASSERT_EQUAL_UINT8(0, s.missStreak);
}

// An NRC from a different ECU doesn't release the request (it isn't ours),
// but still proves an ECU is there and ends a backoff
static void test_negative_from_other_ecu(void) {
    CanPollState s;
    canPollReset(s, 0);
    s.missStreak = CAN_POLL_BACKOFF_AFTER;
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    TEST_ASSERT_FALSE(canPollOnNegative(s, 0x7E9, 105));
    TEST_ASSERT_TRUE(s.pending);
    TEST_ASSERT_EQUAL_UINT8(0, s.missStreak);
    TEST_ASSERT_FALSE(canPollOnNegative(s, 0x7DF, 106));   // not a response ID
}

// Which frames are Mode 01 negative responses
static void test_negative_frame_shape(void) {
    const uint8_t nrc31[8] = {0x03, 0x7F, 0x01, 0x31, 0, 0, 0, 0};
    const uint8_t nrc12[8] = {0x03, 0x7F, 0x01, 0x12, 0, 0, 0, 0};
    const uint8_t pending[8] = {0x03, 0x7F, 0x01, 0x78, 0, 0, 0, 0};  // still coming
    const uint8_t mode09[8] = {0x03, 0x7F, 0x09, 0x31, 0, 0, 0, 0};   // not our mode
    const uint8_t value[8] = {0x03, 0x41, 0x05, 0x7B, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(isMode01NegativeResponse(nrc31, 8));
    TEST_ASSERT_TRUE(isMode01NegativeResponse(nrc12, 4));
    TEST_ASSERT_FALSE(isMode01NegativeResponse(nrc31, 3));
    TEST_ASSERT_FALSE(isMode01NegativeResponse(pending, 8));
    TEST_ASSERT_FALSE(isMode01NegativeResponse(mode09, 8));
    TEST_ASSERT_FALSE(isMode01NegativeResponse(value, 8));
}

// Drive one PID the way pollCANInputs() does: every request opens a round.
// The ECU answers in 5 ms, so without the floor it would go every 15 ms.
static void test_round_floor_single_pid(void) {
    CanPollState s;
    canPollReset(s, 0);
    uint32_t sends[4];
    uint8_t n = 0;
    for (uint32_t t = 0; t < 200 && n < 4; t++) {
        if (s.pending && t == s.lastTxMs + 5) canPollOnResponse(s, 0x7E8, 0x0C, t);
        if (!canPollReady(s, t)) continue;
        if (!canPollStartRound(s, t)) continue;
        canPollOnSent(s, 0x7E8, 0x0C, true, t);
        sends[n++] = t;
    }
    TEST_ASSERT_EQUAL_UINT8(4, n);
    TEST_ASSERT_EQUAL_UINT32(CAN_POLL_MIN_GAP_MS, sends[0]);   // first round not held
    for (uint8_t i = 1; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32(CAN_POLL_MIN_PID_INTERVAL_MS, sends[i] - sends[i - 1]);
    }
}

// A held round blocks canPollReady() itself, and is released by the floor
static void test_round_hold_blocks_ready(void) {
    CanPollState s;
    canPollReset(s, 0);
    TEST_ASSERT_TRUE(canPollStartRound(s, 100));
    canPollOnSent(s, 0x7E8, 0x0C, true, 100);
    canPollOnResponse(s, 0x7E8, 0x0C, 105);
    TEST_ASSERT_TRUE(canPollReady(s, 115));
    TEST_ASSERT_FALSE(canPollStartRound(s, 115));
    TEST_ASSERT_TRUE(s.roundHeld);
    TEST_ASSERT_FALSE(canPollReady(s, 100 + CAN_POLL_MIN_PID_INTERVAL_MS - 1));
    TEST_ASSERT_TRUE(canPollReady(s, 100 + CAN_POLL_MIN_PID_INTERVAL_MS));
    TEST_ASSERT_TRUE(canPollStartRound(s, 100 + CAN_POLL_MIN_PID_INTERVAL_MS));
    TEST_ASSERT_FALSE(s.roundHeld);
}

// A round longer than the floor (many PIDs, slow ECU) is never held
static void test_long_round_not_held(void) {
    CanPollState s;
    canPollReset(s, 0);
    TEST_ASSERT_TRUE(canPollStartRound(s, 100));
    TEST_ASSERT_TRUE(canPollStartRound(s, 100 + CAN_POLL_MIN_PID_INTERVAL_MS + 30));
    TEST_ASSERT_FALSE(s.roundHeld);
}

// The floor survives millis() rollover
static void test_round_floor_millis_wrap(void) {
    CanPollState s;
    canPollReset(s, 0xFFFFFF00u);
    TEST_ASSERT_TRUE(canPollStartRound(s, 0xFFFFFFF0u));
    TEST_ASSERT_FALSE(canPollStartRound(s, 0x00000010u));   // 32 ms later
    TEST_ASSERT_TRUE(canPollStartRound(s, 0xFFFFFFF0u + CAN_POLL_MIN_PID_INTERVAL_MS));
}

// Which searches open a round: the head of the table, or a wrap past its end
static void test_opens_round(void) {
    TEST_ASSERT_TRUE(canPollOpensRound(0, 0, 8));    // cursor at the head
    TEST_ASSERT_TRUE(canPollOpensRound(0, 5, 8));    // ... target further on
    TEST_ASSERT_FALSE(canPollOpensRound(3, 0, 8));   // mid-table, same pass
    TEST_ASSERT_FALSE(canPollOpensRound(3, 4, 8));   // slot 7, last slot
    TEST_ASSERT_TRUE(canPollOpensRound(3, 5, 8));    // wrapped to slot 0
    TEST_ASSERT_TRUE(canPollOpensRound(7, 1, 8));    // one past the last slot
    TEST_ASSERT_TRUE(canPollOpensRound(39, 39, 40)); // Teensy table, full wrap
}

// The pollCANInputs() loop over a table: PIDs in slots 1, 4 and 7 (the last)
// answered in 2 ms. Only the first request of each round is held; the
// others follow at the gap, and each PID repeats every 50 ms.
static void test_round_floor_multi_pid_table(void) {
    const uint8_t SIZE = 8;
    const bool target[SIZE] = {false, true, false, false, true, false, false, true};
    CanPollState s;
    canPollReset(s, 0);
    uint8_t cursor = 0;
    uint32_t sentAt[16];
    uint8_t sentSlot[16];
    uint8_t n = 0;
    for (uint32_t t = 0; t < 400 && n < 9; t++) {
        if (s.pending && t == s.lastTxMs + 2) canPollOnResponse(s, 0x7E8, s.pendingPid, t);
        if (!canPollReady(s, t)) continue;
        for (uint8_t k = 0; k < SIZE; k++) {
            uint8_t slot = (uint8_t)((cursor + k) % SIZE);
            if (!target[slot]) continue;
            if (canPollOpensRound(cursor, k, SIZE) && !canPollStartRound(s, t)) break;
            cursor = (uint8_t)((slot + 1) % SIZE);
            canPollOnSent(s, 0x7E8, slot, true, t);
            sentAt[n] = t;
            sentSlot[n] = slot;
            n++;
            break;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(9, n);
    for (uint8_t i = 0; i < 9; i++) {
        TEST_ASSERT_EQUAL_UINT8(i % 3 == 0 ? 1 : i % 3 == 1 ? 4 : 7, sentSlot[i]);
        if (i >= 3) TEST_ASSERT_EQUAL_UINT32(CAN_POLL_MIN_PID_INTERVAL_MS, sentAt[i] - sentAt[i - 3]);
        if (i % 3) TEST_ASSERT_EQUAL_UINT32(2 + CAN_POLL_MIN_GAP_MS, sentAt[i] - sentAt[i - 1]);
    }
}

// canPollReset (run every loop during SCAN PIDS) drops a held round
static void test_reset_clears_round_hold(void) {
    CanPollState s;
    canPollReset(s, 0);
    TEST_ASSERT_TRUE(canPollStartRound(s, 100));
    TEST_ASSERT_FALSE(canPollStartRound(s, 110));
    TEST_ASSERT_TRUE(s.roundHeld);
    canPollReset(s, 120);
    TEST_ASSERT_FALSE(s.roundHeld);
    TEST_ASSERT_TRUE(canPollReady(s, 120 + CAN_POLL_MIN_GAP_MS));
    TEST_ASSERT_TRUE(canPollStartRound(s, 120 + CAN_POLL_MIN_GAP_MS));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_request_frame);
    RUN_TEST(test_addressing);
    RUN_TEST(test_first_request_after_gap);
    RUN_TEST(test_answer_releases_early);
    RUN_TEST(test_other_answer_does_not_release);
    RUN_TEST(test_timeout_counts_miss);
    RUN_TEST(test_backoff_and_recovery);
    RUN_TEST(test_failed_write_counts_miss);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_negative_response_releases);
    RUN_TEST(test_negative_from_other_ecu);
    RUN_TEST(test_negative_frame_shape);
    RUN_TEST(test_round_floor_single_pid);
    RUN_TEST(test_round_hold_blocks_ready);
    RUN_TEST(test_long_round_not_held);
    RUN_TEST(test_round_floor_millis_wrap);
    RUN_TEST(test_opens_round);
    RUN_TEST(test_round_floor_multi_pid_table);
    RUN_TEST(test_reset_clears_round_hold);
    return UNITY_END();
}
