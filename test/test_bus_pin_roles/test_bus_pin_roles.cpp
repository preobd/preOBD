// Native unit test: busPinRole() (#304).
//
// SYSTEM DIAG PINS annotates a swept pin with its hardware bus role (I2C
// SDA/SCL, SPI MOSI/MISO/SCK, CAN TX/RX) when the pin isn't otherwise
// explained — e.g. a Teensy's A4/A5 read "pulled high" (the I2C pull-ups)
// with no context when Wire never started. busPinRole() is the single
// source of truth for that mapping, built entirely from the getDefault*()
// helpers already in bus_defaults.h (no second {pin -> role} table — see the
// header comment above busPinRole() for why that would duplicate them).
//
// This host build has no real board macro defined (test/stubs/profile_native.h
// declares none), so bus_defaults.h falls into its "UNKNOWN PLATFORM"
// fallback: I2C0 SDA/SCL = 18/19, SPI0 MOSI/MISO/SCK = 11/12/13, zero CAN
// buses. That fallback is exactly what's under test here — busPinRole() must
// use whatever this platform's defaults are, not hardcode Teensy pins.
#include <unity.h>
#include <Arduino.h>

#include "../../src/hal/bus_defaults.h"
#include "../../src/pinctrl/bus_pin_roles.cpp"

// F() on this host stub is just reinterpret_cast<const __FlashStringHelper*>
// of the string literal pointer (test/stubs/Arduino.h) — safe to cast back
// for a plain strcmp.
static const char* asStr(const __FlashStringHelper* f) {
    return reinterpret_cast<const char*>(f);
}

static void test_known_i2c_pin_returns_role(void) {
    const __FlashStringHelper* role = busPinRole(DEFAULT_I2C0_SDA);
    TEST_ASSERT_NOT_NULL(role);
    TEST_ASSERT_EQUAL_STRING("I2C0 SDA", asStr(role));

    role = busPinRole(DEFAULT_I2C0_SCL);
    TEST_ASSERT_NOT_NULL(role);
    TEST_ASSERT_EQUAL_STRING("I2C0 SCL", asStr(role));
}

static void test_known_spi_pin_returns_role(void) {
    const __FlashStringHelper* role = busPinRole(DEFAULT_SPI0_MOSI);
    TEST_ASSERT_NOT_NULL(role);
    TEST_ASSERT_EQUAL_STRING("SPI0 MOSI", asStr(role));

    role = busPinRole(DEFAULT_SPI0_SCK);
    TEST_ASSERT_NOT_NULL(role);
    TEST_ASSERT_EQUAL_STRING("SPI0 SCK", asStr(role));
}

static void test_unrelated_pin_returns_null(void) {
    // Not any of this platform's I2C0/SPI0 default pins.
    TEST_ASSERT_NULL(busPinRole(5));
}

static void test_out_of_range_pin_returns_null(void) {
    // 0xFF is the getDefault*() "bus not present" sentinel itself; busPinRole()
    // must reject it up front rather than let it coincidentally match.
    TEST_ASSERT_NULL(busPinRole(0xFF));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_known_i2c_pin_returns_role);
    RUN_TEST(test_known_spi_pin_returns_role);
    RUN_TEST(test_unrelated_pin_returns_null);
    RUN_TEST(test_out_of_range_pin_returns_null);
    return UNITY_END();
}
