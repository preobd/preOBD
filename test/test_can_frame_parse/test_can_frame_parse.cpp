// Native unit test: CAN input frame identification (src/inputs/can_frame_parse.h).
//
// Pins which cache key a received frame lands under. Regression for #473:
// only 2-byte PID responses (length byte 0x04) were recognised, so a 1-byte
// reply such as coolant temp (03 41 05 7B) was cached under 0x03 and an
// input imported with SET CAN 0x05 never saw data.
#include <unity.h>
#include <Arduino.h>

#include "../../src/inputs/can_frame_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void expectParse(const uint8_t* frame, uint8_t len,
                        uint8_t wantId, uint8_t wantOffset) {
    uint8_t id = 0xEE, off = 0xEE;
    TEST_ASSERT_TRUE(parseCANInputFrame(frame, len, id, off));
    TEST_ASSERT_EQUAL_HEX8(wantId, id);
    TEST_ASSERT_EQUAL_UINT8(wantOffset, off);
}

// 1-byte PID: coolant temperature 0x05 (0x7B - 40 = 83 C)
static void test_one_byte_pid(void) {
    const uint8_t f[8] = {0x03, 0x41, 0x05, 0x7B, 0x00, 0x00, 0x00, 0x00};
    expectParse(f, 8, 0x05, 3);
    TEST_ASSERT_EQUAL_HEX8(0x7B, f[3]);
}

// 2-byte PID: engine RPM 0x0C (the only shape recognised before #473)
static void test_two_byte_pid(void) {
    const uint8_t f[8] = {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00};
    expectParse(f, 8, 0x0C, 3);
}

// 3-byte PID: length byte 0x05
static void test_three_byte_pid(void) {
    const uint8_t f[8] = {0x05, 0x41, 0x6B, 0x01, 0x02, 0x03, 0x00, 0x00};
    expectParse(f, 8, 0x6B, 3);
}

// 4-byte PID: supported-PIDs bitmap 0x00
static void test_four_byte_pid(void) {
    const uint8_t f[8] = {0x06, 0x41, 0x00, 0xBE, 0x3F, 0xA8, 0x13, 0x00};
    expectParse(f, 8, 0x00, 3);
}

// 5-byte PID fills the single frame: length byte 0x07
static void test_five_byte_pid(void) {
    const uint8_t f[8] = {0x07, 0x41, 0x4F, 0x01, 0x02, 0x03, 0x04, 0x05};
    expectParse(f, 8, 0x4F, 3);
}

// Unpadded DLC: frame is only as long as the response
static void test_unpadded_one_byte_pid(void) {
    const uint8_t f[4] = {0x03, 0x41, 0x0D, 0x3C};
    expectParse(f, 4, 0x0D, 3);
}

// Mode 01 response without the length byte (some ECUs)
static void test_no_length_prefix(void) {
    const uint8_t f[3] = {0x41, 0x05, 0x7B};
    expectParse(f, 3, 0x05, 2);
}

// Length byte out of range: not a single-frame response -> custom path
static void test_length_out_of_range_is_custom(void) {
    const uint8_t f1[8] = {0x02, 0x41, 0x05, 0x7B, 0, 0, 0, 0};
    expectParse(f1, 8, 0x02, 0);
    const uint8_t f2[8] = {0x08, 0x41, 0x05, 0x7B, 0, 0, 0, 0};
    expectParse(f2, 8, 0x08, 0);
}

// Custom protocol: first byte is the identifier, payload from byte 0
static void test_custom_protocol(void) {
    const uint8_t f[8] = {0x10, 0x22, 0x33, 0x44, 0, 0, 0, 0};
    expectParse(f, 8, 0x10, 0);
}

// Header with no data byte after it is rejected
static void test_header_only_rejected(void) {
    uint8_t id, off;
    const uint8_t f[3] = {0x03, 0x41, 0x05};
    TEST_ASSERT_FALSE(parseCANInputFrame(f, 3, id, off));
}

static void test_empty_rejected(void) {
    uint8_t id, off;
    const uint8_t f[1] = {0x00};
    TEST_ASSERT_FALSE(parseCANInputFrame(f, 0, id, off));
}

// Two-byte 0x41 frame is too short for PID extraction -> custom key 0x41
static void test_short_obd_is_custom(void) {
    const uint8_t f[2] = {0x41, 0x05};
    expectParse(f, 2, 0x41, 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_one_byte_pid);
    RUN_TEST(test_two_byte_pid);
    RUN_TEST(test_three_byte_pid);
    RUN_TEST(test_four_byte_pid);
    RUN_TEST(test_five_byte_pid);
    RUN_TEST(test_unpadded_one_byte_pid);
    RUN_TEST(test_no_length_prefix);
    RUN_TEST(test_length_out_of_range_is_custom);
    RUN_TEST(test_custom_protocol);
    RUN_TEST(test_header_only_rejected);
    RUN_TEST(test_empty_rejected);
    RUN_TEST(test_short_obd_is_custom);
    return UNITY_END();
}
