// Native unit test: OBD-II Mode 01 request pacing (src/inputs/can_poll.h, #476).
//
// CAN input used to be passive only, so an imported PID read NaN unless
// another tester polled the ECU. Pins the request frame, the ECU addressing
// rule, and the response-driven pacing / dead-bus backoff.
#include <unity.h>
#include <Arduino.h>

#include "../../src/inputs/can_poll.h"

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
    return UNITY_END();
}
