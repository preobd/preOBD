// Native unit test: HALL_SPEED's ISR edge-rejection threshold (#577).
//
// A fixed 500 µs debounce capped the input at 2 kHz -- 38.6 km/h with the
// preset calibration -- and above it the ISR silently kept every 2nd/3rd
// edge. The threshold now follows the calibration: half the pulse interval at
// max_speed_kph, floor 20 µs. These pin the values and the invariant that a
// rate above max speed still reads as out of range rather than a lower,
// plausible speed.
#include <unity.h>
#include <math.h>

#include "../../src/sensors/drivers/speed/hall_speed_debounce.h"

void setUp(void) {}
void tearDown(void) {}

// Same formula readHallSpeed() uses.
static float speedKph(float interval_us, uint8_t ppr, uint16_t circ, float ratio, float mult) {
    return (1000000.0f / interval_us) / ppr * (circ / 1000.0f) / ratio * 3.6f * mult;
}

// Simulate the ISR on a square wave of period_us: which interval does it
// latch once steady? (Same comparison as speedPulseISR: accept >= threshold.)
static uint32_t latchedInterval(uint32_t period_us, uint32_t debounce_us) {
    uint32_t t = 0, last = 0, interval = 0;
    for (int i = 0; i < 1000; i++) {
        t += period_us;
        if (t - last >= debounce_us) { interval = t - last; last = t; }
    }
    return interval;
}

static void test_preset_calibration(void) {
    // 100 ppr, 2000 mm, 3.73, x1, 300 km/h: interval at max = 64.3 µs.
    TEST_ASSERT_EQUAL_UINT32(32, hallSpeedDebounceUs(100, 2000, 3.73f, 1.0f, 300));
}

static void test_preset_follows_issue_rates(void) {
    // The measured failures: 2 kHz read 19.3, 4 kHz read ~26 (#577).
    uint32_t d = hallSpeedDebounceUs(100, 2000, 3.73f, 1.0f, 300);
    TEST_ASSERT_EQUAL_UINT32(500, latchedInterval(500, d));
    TEST_ASSERT_EQUAL_UINT32(250, latchedInterval(250, d));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 38.6f, speedKph(latchedInterval(500, d), 100, 2000, 3.73f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 77.2f, speedKph(latchedInterval(250, d), 100, 2000, 3.73f, 1.0f));
}

static void test_custom_bench_calibration(void) {
    // SET <pin> CAL SPEED 1 1000 1.0 1.0 2000 300: interval at max = 12 ms.
    TEST_ASSERT_EQUAL_UINT32(6000, hallSpeedDebounceUs(1, 1000, 1.0f, 1.0f, 300));
}

static void test_calibration_mult_scales_threshold(void) {
    TEST_ASSERT_EQUAL_UINT32(12000, hallSpeedDebounceUs(1, 1000, 1.0f, 2.0f, 300));
}

static void test_noise_floor(void) {
    // Very high pulse rate at max speed: interval/2 < 20 µs -> floor.
    TEST_ASSERT_EQUAL_UINT32(HALL_SPEED_DEBOUNCE_FLOOR_US,
                             hallSpeedDebounceUs(250, 1000, 10.0f, 1.0f, 300));
}

static void test_invalid_calibration_uses_floor(void) {
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(0, 2000, 3.73f, 1.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 0, 3.73f, 1.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 2000, 0.0f, 1.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 2000, -1.0f, 1.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 2000, 3.73f, 0.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 2000, 3.73f, NAN, 300));
    TEST_ASSERT_EQUAL_UINT32(20, hallSpeedDebounceUs(100, 2000, 3.73f, 1.0f, 0));
}

static void test_upper_clamp(void) {
    TEST_ASSERT_EQUAL_UINT32(HALL_SPEED_DEBOUNCE_MAX_US,
                             hallSpeedDebounceUs(1, 65535, 0.01f, 1.0f, 1));
}

// Every rate up to max speed is followed exactly; every rate above it reads
// faster than max speed (-> readHallSpeed's NAN), never a lower in-range value.
static void check_invariant(uint8_t ppr, uint16_t circ, float ratio, uint16_t max_kph) {
    uint32_t d = hallSpeedDebounceUs(ppr, circ, ratio, 1.0f, max_kph);
    float min_interval = 3600.0f * circ / (ppr * ratio * max_kph);
    for (uint32_t period = 1; period < 4 * (uint32_t)min_interval + 10; period++) {
        uint32_t got = latchedInterval(period, d);
        if ((float)period >= min_interval) {
            TEST_ASSERT_EQUAL_UINT32(period, got);
        } else if (d > HALL_SPEED_DEBOUNCE_FLOOR_US) {
            TEST_ASSERT_TRUE(speedKph(got, ppr, circ, ratio, 1.0f) > max_kph);
        }
    }
}

static void test_above_max_speed_never_reads_in_range(void) {
    check_invariant(100, 2000, 3.73f, 300);
    check_invariant(1, 1000, 1.0f, 300);
    check_invariant(4, 1900, 3.9f, 200);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_preset_calibration);
    RUN_TEST(test_preset_follows_issue_rates);
    RUN_TEST(test_custom_bench_calibration);
    RUN_TEST(test_calibration_mult_scales_threshold);
    RUN_TEST(test_noise_floor);
    RUN_TEST(test_invalid_calibration_uses_floor);
    RUN_TEST(test_upper_clamp);
    RUN_TEST(test_above_max_speed_never_reads_in_range);
    return UNITY_END();
}
