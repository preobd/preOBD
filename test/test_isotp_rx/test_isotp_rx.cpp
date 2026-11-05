// Native unit test: ISO-TP multi-frame Mode 01 reply reassembly
// (src/inputs/isotp_rx.h, #527) and how it holds the poller's request open
// (canPollOnProgress, src/inputs/can_poll.h).
//
// CAN import read single-frame replies only, so 0x70 boost, 0x78/0x79 EGT
// and 0x7F run time, all longer than one frame, could not be imported. The
// frames below are what preOBD's own output sends (output_can.cpp).
#include <unity.h>
#include <Arduino.h>

#include "../../src/inputs/isotp_rx.h"
#include "../../src/inputs/can_poll.h"

void setUp(void) {}
void tearDown(void) {}

static const uint16_t ECU = 0x7E8;

// 0x78 EGT: 41 78 + A..I = 11 bytes. Sensor 1 = 0x0FA0 -> 360.0 degC
static const uint8_t EGT_FF[8] = {0x10, 0x0B, 0x41, 0x78, 0x01, 0x0F, 0xA0, 0x00};
static const uint8_t EGT_CF[8] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0xAA};

static IsoTpRx fresh(void) {
    IsoTpRx s;
    memset(&s, 0xEE, sizeof(s));
    isoTpRxReset(s);
    return s;
}

static void test_flow_control_frame(void) {
    uint8_t f[8];
    memset(f, 0xEE, sizeof(f));
    buildIsoTpFlowControl(f);
    const uint8_t want[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f, 8);
}

static void test_two_frame_reply(void) {
    IsoTpRx s = fresh();
    TEST_ASSERT_EQUAL(ISOTP_RX_STARTED, isoTpRxFrame(s, ECU, EGT_FF, 8, 100));
    TEST_ASSERT_EQUAL(ISOTP_RX_COMPLETE, isoTpRxFrame(s, ECU, EGT_CF, 8, 102));
    TEST_ASSERT_EQUAL_UINT8(11, s.total);
    const uint8_t want[11] = {0x41, 0x78, 0x01, 0x0F, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, s.buf, 11);   // CF padding past the message not taken
    TEST_ASSERT_FALSE(s.active);
}

// 0x7F run time: 41 7F + A..M = 15 bytes, two Consecutive Frames
static void test_three_frame_reply(void) {
    IsoTpRx s = fresh();
    const uint8_t ff[8]  = {0x10, 0x0F, 0x41, 0x7F, 0x07, 0x00, 0x01, 0x51};
    const uint8_t cf1[8] = {0x21, 0x80, 0x00, 0x00, 0x0E, 0x10, 0x00, 0x00};
    const uint8_t cf2[8] = {0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(ISOTP_RX_STARTED, isoTpRxFrame(s, ECU, ff, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_PROGRESS, isoTpRxFrame(s, ECU, cf1, 8, 1));
    TEST_ASSERT_EQUAL(ISOTP_RX_COMPLETE, isoTpRxFrame(s, ECU, cf2, 8, 2));
    TEST_ASSERT_EQUAL_UINT8(15, s.total);
    // B-E total run time = 0x00015180 = 86400 s
    TEST_ASSERT_EQUAL_HEX8(0x00, s.buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x51, s.buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.buf[6]);
}

static void test_out_of_sequence_aborts(void) {
    IsoTpRx s = fresh();
    uint8_t cf[8];
    memcpy(cf, EGT_CF, 8);
    cf[0] = 0x22;   // skipped 0x21
    TEST_ASSERT_EQUAL(ISOTP_RX_STARTED, isoTpRxFrame(s, ECU, EGT_FF, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_ABORTED, isoTpRxFrame(s, ECU, cf, 8, 1));
    TEST_ASSERT_FALSE(s.active);
    // The right frame arriving late belongs to nothing now
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, EGT_CF, 8, 2));
}

static void test_timeout_aborts(void) {
    IsoTpRx s = fresh();
    TEST_ASSERT_EQUAL(ISOTP_RX_STARTED, isoTpRxFrame(s, ECU, EGT_FF, 8, 1000));
    TEST_ASSERT_EQUAL(ISOTP_RX_ABORTED,
                      isoTpRxFrame(s, ECU, EGT_CF, 8, 1000 + ISOTP_RX_TIMEOUT_MS + 1));
    // At the limit is still in time
    s = fresh();
    isoTpRxFrame(s, ECU, EGT_FF, 8, 1000);
    TEST_ASSERT_EQUAL(ISOTP_RX_COMPLETE,
                      isoTpRxFrame(s, ECU, EGT_CF, 8, 1000 + ISOTP_RX_TIMEOUT_MS));
}

static void test_short_consecutive_frame_aborts(void) {
    IsoTpRx s = fresh();
    isoTpRxFrame(s, ECU, EGT_FF, 8, 0);
    // 5 bytes still due, the frame carries 3
    TEST_ASSERT_EQUAL(ISOTP_RX_ABORTED, isoTpRxFrame(s, ECU, EGT_CF, 4, 1));
    // A DLC-trimmed last frame carrying exactly what is due completes
    s = fresh();
    isoTpRxFrame(s, ECU, EGT_FF, 8, 0);
    TEST_ASSERT_EQUAL(ISOTP_RX_COMPLETE, isoTpRxFrame(s, ECU, EGT_CF, 6, 1));
}

static void test_first_frames_not_taken(void) {
    IsoTpRx s = fresh();
    const uint8_t vin[8]   = {0x10, 0x14, 0x49, 0x02, 0x01, 0x31, 0x47, 0x31};   // Mode 09 VIN
    const uint8_t big[8]   = {0x10, 0x10, 0x41, 0x99, 0x00, 0x00, 0x00, 0x00};   // 16 > ISOTP_RX_MAX
    const uint8_t small[8] = {0x10, 0x07, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00};   // fits a single frame
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, vin, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, big, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, small, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, EGT_FF, 7, 0));    // a First Frame fills 8
    TEST_ASSERT_FALSE(s.active);
    // Single frames, Flow Control and NRCs are not this machine's
    const uint8_t sf[8] = {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00};
    const uint8_t fc[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t nrc[8] = {0x03, 0x7F, 0x01, 0x31, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, sf, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, fc, 8, 0));
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, nrc, 8, 0));
}

static void test_other_ecu_frames_ignored(void) {
    IsoTpRx s = fresh();
    isoTpRxFrame(s, ECU, EGT_FF, 8, 0);
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, 0x7E9, EGT_CF, 8, 1));
    TEST_ASSERT_EQUAL(ISOTP_RX_COMPLETE, isoTpRxFrame(s, ECU, EGT_CF, 8, 2));
}

// A new First Frame from the same ECU starts over; a non-Mode-01 one from it
// ends the reply in progress rather than letting its frames be taken
static void test_first_frame_restarts(void) {
    IsoTpRx s = fresh();
    const uint8_t boostFF[8] = {0x10, 0x0C, 0x41, 0x70, 0x02, 0x00, 0x00, 0x0C};
    isoTpRxFrame(s, ECU, EGT_FF, 8, 0);
    TEST_ASSERT_EQUAL(ISOTP_RX_STARTED, isoTpRxFrame(s, ECU, boostFF, 8, 1));
    TEST_ASSERT_EQUAL_HEX8(0x70, s.buf[1]);
    TEST_ASSERT_EQUAL_UINT8(12, s.total);

    const uint8_t vin[8] = {0x10, 0x14, 0x49, 0x02, 0x01, 0x31, 0x47, 0x31};
    TEST_ASSERT_EQUAL(ISOTP_RX_IGNORED, isoTpRxFrame(s, ECU, vin, 8, 2));
    TEST_ASSERT_FALSE(s.active);
}

// A multi-frame answer keeps the request open past CAN_POLL_RESPONSE_TIMEOUT_MS
// counted from the request, and its last frame answers it
static void test_poll_held_open_by_progress(void) {
    CanPollState p;
    canPollReset(p, 0);
    canPollOnSent(p, ECU, 0x78, true, 100);
    TEST_ASSERT_FALSE(canPollOnProgress(p, 0x7E9, 0x78, 140));         // another ECU
    TEST_ASSERT_FALSE(canPollOnProgress(p, ECU, 0x70, 140));           // another tester's reply
    TEST_ASSERT_TRUE(p.pending);
    TEST_ASSERT_TRUE(canPollOnProgress(p, ECU, 0x78, 140));            // First Frame
    TEST_ASSERT_FALSE(canPollReady(p, 100 + CAN_POLL_RESPONSE_TIMEOUT_MS + 5));
    TEST_ASSERT_TRUE(p.pending);                                        // not a miss
    TEST_ASSERT_TRUE(canPollOnResponse(p, ECU, 0x78, 160));            // reassembled reply
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_EQUAL_UINT8(0, p.missStreak);
    // Nothing outstanding: progress extends nothing
    TEST_ASSERT_FALSE(canPollOnProgress(p, ECU, 0x78, 170));
}

// A reply that stalls still times out, counted from its last frame
static void test_poll_stalled_reply_times_out(void) {
    CanPollState p;
    canPollReset(p, 0);
    canPollOnSent(p, ECU, 0x7F, true, 100);
    canPollOnProgress(p, ECU, 0x7F, 120);
    canPollReady(p, 120 + CAN_POLL_RESPONSE_TIMEOUT_MS);
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_EQUAL_UINT8(1, p.missStreak);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_flow_control_frame);
    RUN_TEST(test_two_frame_reply);
    RUN_TEST(test_three_frame_reply);
    RUN_TEST(test_out_of_sequence_aborts);
    RUN_TEST(test_timeout_aborts);
    RUN_TEST(test_short_consecutive_frame_aborts);
    RUN_TEST(test_first_frames_not_taken);
    RUN_TEST(test_other_ecu_frames_ignored);
    RUN_TEST(test_first_frame_restarts);
    RUN_TEST(test_poll_held_open_by_progress);
    RUN_TEST(test_poll_stalled_reply_times_out);
    return UNITY_END();
}
