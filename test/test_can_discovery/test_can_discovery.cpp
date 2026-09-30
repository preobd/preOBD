// Native unit test: OBD-II supported-PID discovery (src/inputs/can_discovery.h, #511).
//
// SCAN CAN only listens, so on a bus where nothing asks the ECU it finds
// nothing. SCAN PIDS walks the Mode 01 supported-PID bitmaps. Pins the bit
// order, the continuation walk, multi-ECU collection, and the retry/give-up
// on a silent bus.
#include <unity.h>
#include <Arduino.h>

#include "../../src/sensors/drivers/can/can_discovery.h"

void setUp(void) {}
void tearDown(void) {}

// Drive the bitmap phase: expect a request for `wantBase` at `now`, send it.
static void sendBlock(CanDiscState& s, uint32_t now, uint8_t wantBase) {
    uint8_t base = 0xAA;
    TEST_ASSERT_TRUE(canDiscDue(s, now, base));
    TEST_ASSERT_EQUAL_HEX8(wantBase, base);
    canDiscOnSent(s, now);
}

static void test_bitmap_pids(void) {
    TEST_ASSERT_TRUE(canDiscIsBitmapPid(0x00));
    TEST_ASSERT_TRUE(canDiscIsBitmapPid(0x20));
    TEST_ASSERT_TRUE(canDiscIsBitmapPid(0xE0));
    TEST_ASSERT_FALSE(canDiscIsBitmapPid(0x0C));
    TEST_ASSERT_FALSE(canDiscIsBitmapPid(0x21));
}

// Bit 7 of the first byte is PID base+1; the real-world 0x00 answer
// BE 3F A8 13 lists 01 03-07 0B-11 13 15 1C 1F, plus 20 (the next bitmap,
// which is not a sensor PID).
static void test_bit_order(void) {
    CanDiscState s;
    canDiscStart(s);
    sendBlock(s, 0, 0x00);
    const uint8_t bm[4] = {0xBE, 0x3F, 0xA8, 0x13};
    TEST_ASSERT_TRUE(canDiscOnAnswer(s, 0x7E8, 0x00, bm, 4));
    const uint8_t yes[] = {0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D,
                           0x0E, 0x0F, 0x10, 0x11, 0x13, 0x15, 0x1C, 0x1F};
    const uint8_t no[]  = {0x02, 0x08, 0x09, 0x0A, 0x12, 0x14, 0x16, 0x1B, 0x1D, 0x1E, 0x20, 0x21};
    for (uint8_t p : yes) TEST_ASSERT_TRUE_MESSAGE(canDiscSupports(s, 0x7E8, p), "expected supported");
    for (uint8_t p : no)  TEST_ASSERT_FALSE_MESSAGE(canDiscSupports(s, 0x7E8, p), "expected unsupported");
    TEST_ASSERT_FALSE(canDiscSupports(s, 0x7E9, 0x0C));
}

// Continuation bit set -> next block; clear -> sample phase
static void test_continuation_walk(void) {
    CanDiscState s;
    canDiscStart(s);
    sendBlock(s, 0, 0x00);
    const uint8_t b0[4] = {0x00, 0x10, 0x00, 0x01};   // 0x0C + more
    canDiscOnAnswer(s, 0x7E8, 0x00, b0, 4);
    uint8_t base;
    TEST_ASSERT_FALSE(canDiscDue(s, CAN_DISC_WINDOW_MS - 1, base));  // window still open
    sendBlock(s, CAN_DISC_WINDOW_MS, 0x20);
    const uint8_t b1[4] = {0x80, 0x00, 0x00, 0x00};   // 0x21, no more
    TEST_ASSERT_TRUE(canDiscOnAnswer(s, 0x7E8, 0x20, b1, 4));
    TEST_ASSERT_FALSE(canDiscDue(s, 2 * CAN_DISC_WINDOW_MS, base));
    TEST_ASSERT_EQUAL(CAN_DISC_DONE, s.phase);
    TEST_ASSERT_TRUE(canDiscSupports(s, 0x7E8, 0x0C));
    TEST_ASSERT_TRUE(canDiscSupports(s, 0x7E8, 0x21));
}

// A stale answer for another block is not taken as this block's bitmap
static void test_wrong_block_ignored(void) {
    CanDiscState s;
    canDiscStart(s);
    sendBlock(s, 0, 0x00);
    const uint8_t bm[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(canDiscOnAnswer(s, 0x7E8, 0x20, bm, 4));
    TEST_ASSERT_FALSE(canDiscOnAnswer(s, 0x7E8, 0x0C, bm, 4));   // a sensor answer
    TEST_ASSERT_FALSE(canDiscOnAnswer(s, 0x7E8, 0x00, bm, 3));   // too short
    TEST_ASSERT_FALSE(canDiscOnAnswer(s, 0x7DF, 0x00, bm, 4));   // not an ECU
    TEST_ASSERT_EQUAL_HEX8(0, s.ecuMask);
}

// Two ECUs answer the same functional request; either one's continuation
// bit keeps the walk going, and each keeps its own set
static void test_multi_ecu(void) {
    CanDiscState s;
    canDiscStart(s);
    sendBlock(s, 0, 0x00);
    const uint8_t engine[4] = {0x08, 0x00, 0x00, 0x00};  // 0x05
    const uint8_t trans[4]  = {0x00, 0x00, 0x00, 0x01};  // 0x20 only (more)
    TEST_ASSERT_TRUE(canDiscOnAnswer(s, 0x7E8, 0x00, engine, 4));
    TEST_ASSERT_TRUE(canDiscOnAnswer(s, 0x7E9, 0x00, trans, 4));
    TEST_ASSERT_EQUAL_HEX8(0x03, s.ecuMask);
    sendBlock(s, CAN_DISC_WINDOW_MS, 0x20);
    const uint8_t trans2[4] = {0x00, 0x00, 0x00, 0x80};  // 0x39
    canDiscOnAnswer(s, 0x7E9, 0x20, trans2, 4);
    uint8_t base;
    TEST_ASSERT_FALSE(canDiscDue(s, 2 * CAN_DISC_WINDOW_MS, base));

    TEST_ASSERT_EQUAL(CAN_DISC_DONE, s.phase);
    TEST_ASSERT_TRUE(canDiscSupports(s, 0x7E8, 0x05));
    TEST_ASSERT_FALSE(canDiscSupports(s, 0x7E8, 0x39));
    TEST_ASSERT_TRUE(canDiscSupports(s, 0x7E9, 0x39));
    TEST_ASSERT_FALSE(canDiscSupports(s, 0x7E9, 0x20));   // bitmap PID: not a sensor
    TEST_ASSERT_EQUAL_UINT8(1, canDiscCountEcu(s, 0));
    TEST_ASSERT_EQUAL_UINT8(1, canDiscCountEcu(s, 1));
    TEST_ASSERT_EQUAL_UINT8(2, canDiscEcuCount(s));
}

// Nothing answers PID 0x00: CAN_DISC_TRIES requests, then done with nothing
static void test_silent_bus_gives_up(void) {
    CanDiscState s;
    canDiscStart(s);
    uint32_t t = 0;
    for (uint8_t i = 0; i < CAN_DISC_TRIES; i++) {
        sendBlock(s, t, 0x00);
        t += CAN_DISC_WINDOW_MS;
    }
    uint8_t base;
    TEST_ASSERT_FALSE(canDiscDue(s, t, base));
    TEST_ASSERT_EQUAL(CAN_DISC_DONE, s.phase);
    TEST_ASSERT_FALSE(canDiscActive(s));
    TEST_ASSERT_EQUAL_UINT8(0, canDiscEcuCount(s));
}

// The 0xE0 block's continuation bit would point past PID 0xFF: stop there
static void test_last_block_stops(void) {
    CanDiscState s;
    canDiscStart(s);
    const uint8_t all[4] = {0x00, 0x00, 0x00, 0x01};
    uint32_t t = 0;
    for (uint16_t b = 0; b <= CAN_DISC_LAST_BASE; b += 0x20) {
        sendBlock(s, t, (uint8_t)b);
        TEST_ASSERT_TRUE(canDiscOnAnswer(s, 0x7E8, (uint8_t)b, all, 4));
        t += CAN_DISC_WINDOW_MS;
    }
    uint8_t base;
    TEST_ASSERT_FALSE(canDiscDue(s, t, base));
    TEST_ASSERT_EQUAL(CAN_DISC_DONE, s.phase);
    TEST_ASSERT_EQUAL_HEX8(CAN_DISC_LAST_BASE, s.base);
    TEST_ASSERT_EQUAL_UINT8(0, canDiscCountEcu(s, 0));   // only bitmap PIDs listed
}

// A block's answer outside its window (none open) is ignored
static void test_answer_before_request_ignored(void) {
    CanDiscState s;
    canDiscStart(s);
    const uint8_t bm[4] = {0xFF, 0xFF, 0xFF, 0xFE};
    TEST_ASSERT_FALSE(canDiscOnAnswer(s, 0x7E8, 0x00, bm, 4));
}

static void test_millis_wrap(void) {
    CanDiscState s;
    canDiscStart(s);
    sendBlock(s, 0xFFFFFFF0u, 0x00);
    uint8_t base;
    TEST_ASSERT_FALSE(canDiscDue(s, 0x00000010u, base));   // 32 ms elapsed
    TEST_ASSERT_TRUE(canDiscDue(s, 0xFFFFFFF0u + CAN_DISC_WINDOW_MS, base));  // retry
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bitmap_pids);
    RUN_TEST(test_bit_order);
    RUN_TEST(test_continuation_walk);
    RUN_TEST(test_wrong_block_ignored);
    RUN_TEST(test_multi_ecu);
    RUN_TEST(test_silent_bus_gives_up);
    RUN_TEST(test_last_block_stops);
    RUN_TEST(test_answer_before_request_ignored);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
