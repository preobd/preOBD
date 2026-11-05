// Native unit test: readCANSensor() (src/sensors/drivers/can/can_read.cpp)
// on cached Mode 01 replies, for the values of multi-sensor PIDs (#535).
//
// Byte A of 0x66/0x67/0x70/0x78/0x79/0x7F says which sensors the ECU has.
// A value whose bit is clear has no reading: the bytes in its place are
// padding or whatever the ECU leaves there, and decoding them could raise
// an alarm (0xAA in 0x67's sensor-2 byte reads 130 C).
#include <unity.h>
#include <Arduino.h>
#include <math.h>
#include <algorithm>

// Board facts the cache sizes itself by (boards/<board>/board.h)
#define CAN_CACHE_SIZE 16
using std::min;

#include "../../src/inputs/input.h"
#include "../../src/sensors/drivers/can/can_frame_cache.cpp"
#include "../../src/sensors/drivers/can/can_read.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"

void setUp(void) { g_test_now = 1000; initCANFrameCache(); }
void tearDown(void) {}

// An imported input reading `pid` at `offset`, 2 bytes, (raw / 10) - 40
static Input canInput(uint16_t id, uint8_t pid, uint8_t offset, uint8_t len = 2,
                      float scale = 0.1f, float off = -40.0f) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.flags.useCustomCalibration = true;
    CANSensorCalibration& c = in.customCalibration.can;
    c.source_can_id = id;
    c.source_pid = pid;
    c.data_offset = offset;
    c.data_length = len;
    c.is_big_endian = true;
    c.scale_factor = scale;
    c.offset = off;
    c.timeout_ms = 2000;
    return in;
}

static float read(Input in) { readCANSensor(&in); return in.value; }

// 0x78 from an ECU with EGT sensors 1 and 2 (A = 0x03): 3 and 4 are absent
static void test_egt_support_bits(void) {
    const uint8_t reply[9] = {0x03, 0x1B, 0x58, 0x1B, 0xBC, 0xAA, 0xAA, 0x00, 0x00};  // 660, 670 C
    updateCANCache(0x7E8, 0x78, reply, sizeof(reply));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 660.0f, read(canInput(0x7E8, 0x78, 1)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 670.0f, read(canInput(0x7E8, 0x78, 3)));
    TEST_ASSERT_TRUE(isnan(read(canInput(0x7E8, 0x78, 5))));
    TEST_ASSERT_TRUE(isnan(read(canInput(0x7E8, 0x78, 7))));
}

// 0x67 from an ECU with coolant sensor 1 only: the padding in C is not a reading
static void test_single_frame_padding(void) {
    const uint8_t reply[5] = {0x01, 0x82, 0xAA, 0xAA, 0xAA};
    updateCANCache(0x7E8, 0x67, reply, sizeof(reply));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, read(canInput(0x7E8, 0x67, 1, 1, 1.0f)));
    TEST_ASSERT_TRUE(isnan(read(canInput(0x7E8, 0x67, 2, 1, 1.0f))));
}

// 0x70: bit 1 is sensor A's actual value, bit 3 sensor B's (0/2 are commanded)
static void test_boost_bits(void) {
    uint8_t reply[10] = {0x05, 0, 0, 0x12, 0xC0, 0, 0, 0x0F, 0x00, 0};   // commanded A + B only
    updateCANCache(0x7E8, 0x70, reply, sizeof(reply));
    TEST_ASSERT_TRUE(isnan(read(canInput(0x7E8, 0x70, 3, 2, 0.03125f, 0))));
    TEST_ASSERT_TRUE(isnan(read(canInput(0x7E8, 0x70, 7, 2, 0.03125f, 0))));
    reply[0] = 0x0A;                                                      // actual A + B
    updateCANCache(0x7E8, 0x70, reply, sizeof(reply));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, read(canInput(0x7E8, 0x70, 3, 2, 0.03125f, 0)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, read(canInput(0x7E8, 0x70, 7, 2, 0.03125f, 0)));
}

// No support byte to check: a single-value PID, a custom JSON offset, a
// broadcast (non-OBD) frame keyed by the same byte
static void test_no_check_where_no_support_byte(void) {
    const uint8_t rpm[2] = {0x0F, 0x7C};
    updateCANCache(0x7E8, 0x0C, rpm, sizeof(rpm));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 991.0f, read(canInput(0x7E8, 0x0C, 0, 2, 0.25f, 0)));
    const uint8_t egt[9] = {0x00, 0x1B, 0x58, 0x1B, 0xBC, 0, 0, 0, 0};
    updateCANCache(0x7E8, 0x78, egt, sizeof(egt));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 88.0f, read(canInput(0x7E8, 0x78, 2, 1, 1.0f, 0)));   // custom offset 2: 0x58
    updateCANCache(0x400, 0x78, egt, sizeof(egt));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 660.0f, read(canInput(0x400, 0x78, 1)));   // broadcast frame
}

// Every multi-sensor value has a support bit, distinct within its PID
static void test_every_sensor_has_a_bit(void) {
    const uint8_t pids[] = {0x66, 0x67, 0x70, 0x78, 0x79, 0x7F};
    for (uint8_t pid : pids) {
        uint8_t seen = 0;
        StandardPIDInfo r;
        memcpy_P(&r, lookupStandardPID(pid), sizeof(r));
        uint8_t b = standardPidSupportBit(pid, r.data_offset);
        TEST_ASSERT_TRUE(b < 8);
        seen |= 1u << b;
        for (uint8_t n = 2; n <= standardPIDSensorCount(pid); n++) {
            StandardPIDSensor v;
            memcpy_P(&v, lookupStandardPIDSensor(pid, n), sizeof(v));
            b = standardPidSupportBit(pid, v.data_offset);
            TEST_ASSERT_TRUE(b < 8);
            TEST_ASSERT_FALSE(seen & (1u << b));
            seen |= 1u << b;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(NO_SUPPORT_BIT, standardPidSupportBit(0x0C, 0));
    TEST_ASSERT_EQUAL_UINT8(NO_SUPPORT_BIT, standardPidSupportBit(0x78, 2));    // between sensors
    TEST_ASSERT_EQUAL_UINT8(NO_SUPPORT_BIT, standardPidSupportBit(0x99, 1));    // not in the table
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_egt_support_bits);
    RUN_TEST(test_single_frame_padding);
    RUN_TEST(test_boost_bits);
    RUN_TEST(test_no_check_where_no_support_byte);
    RUN_TEST(test_every_sensor_has_a_bit);
    return UNITY_END();
}
