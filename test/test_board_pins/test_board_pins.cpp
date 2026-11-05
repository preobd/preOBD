// Native unit test: pinctrl/board_pins (#570) against a fake core.
//
// board_pins.cpp reads every fact from the Arduino core. This test plays the
// core: a board with digital pins 0..41, analog names that are NOT a
// contiguous A0 + n run (like Teensy 4.1's A14..A17 at 38..41), one ADC-only
// pin past the digital range (like Teensy 3.6's A10 = 64), and a seventh
// analog input board.h caps away (like ESP32's ADC2).
#include <unity.h>
#include <Arduino.h>

#undef A0
#define A0 14
#define A1 15
#define A2 16
#define A3 38
#define A4 39
#define A5 64
#define A6 70
#define NUM_DIGITAL_PINS 42
#define NUM_ANALOG_INPUTS 7
#define BOARD_ANALOG_USABLE 6
#define ARDUINO_TEENSY41

#include "../../src/pinctrl/board_pins.cpp"

void setUp(void) {}
void tearDown(void) {}

static void test_analog_names_map_both_ways_through_the_core_constants(void) {
    TEST_ASSERT_EQUAL_UINT8(6, analogPinCount());
    const uint8_t pins[6] = {14, 15, 16, 38, 39, 64};
    for (uint8_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT8(pins[i], analogPinForIndex(i));
        TEST_ASSERT_EQUAL_INT8(i, analogIndexForPin(pins[i]));
    }
    TEST_ASSERT_EQUAL_INT8(-1, analogIndexForPin(17));   // A0 + 3 is not A3
}

// board.h's cap wins over the core's count: A6 is not offered.
static void test_board_analog_usable_caps_the_core_count(void) {
    TEST_ASSERT_EQUAL_UINT8(0xFF, analogPinForIndex(6));
    TEST_ASSERT_EQUAL_INT8(-1, analogIndexForPin(70));
}

static void test_a_pin_exists_if_the_core_has_it_or_it_is_an_analog_name(void) {
    TEST_ASSERT_TRUE(boardHasPin(0));
    TEST_ASSERT_TRUE(boardHasPin(41));
    TEST_ASSERT_FALSE(boardHasPin(42));
    TEST_ASSERT_TRUE_MESSAGE(boardHasPin(64), "an ADC-only pin past the digital range exists");
    TEST_ASSERT_FALSE_MESSAGE(boardHasPin(70), "a capped-away analog pin does not");
    TEST_ASSERT_FALSE(boardHasPin(0xFF));
}

static void test_the_board_name_comes_from_the_board_define(void) {
    TEST_ASSERT_EQUAL_STRING("TEENSY41", boardName());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_analog_names_map_both_ways_through_the_core_constants);
    RUN_TEST(test_board_analog_usable_caps_the_core_count);
    RUN_TEST(test_a_pin_exists_if_the_core_has_it_or_it_is_an_analog_name);
    RUN_TEST(test_the_board_name_comes_from_the_board_define);
    return UNITY_END();
}
