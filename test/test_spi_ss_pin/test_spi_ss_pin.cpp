// Native unit test: on AVR, SPI's pins include SS (#663).
//
// The AVR core's SPI.begin() drives SS, so the bus claims it with MOSI/MISO/SCK
// and SYSTEM PINS names it. The host build's fallback platform has no SS, so
// this test plays an AVR by defining DEFAULT_SPI0_SS before bus_defaults.h.
#define DEFAULT_SPI0_SS 4
#include <unity.h>
#include <Arduino.h>

#include "../../src/hal/bus_defaults.h"
#include "../../src/buses/pinctrl/bus_pin_roles.cpp"

void setUp(void) {}
void tearDown(void) {}

static void test_spi_bus_pins_include_ss(void) {
    uint8_t p[4] = { 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(4, getSPIBusPins(0, p));
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_SPI0_MOSI, p[0]);
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_SPI0_MISO, p[1]);
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_SPI0_SCK,  p[2]);
    TEST_ASSERT_EQUAL_UINT8(4, p[3]);
}

static void test_ss_has_a_role_label(void) {
    const __FlashStringHelper* role = busPinRole(4);
    TEST_ASSERT_NOT_NULL(role);
    TEST_ASSERT_EQUAL_STRING("SPI0 SS", reinterpret_cast<const char*>(role));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_spi_bus_pins_include_ss);
    RUN_TEST(test_ss_has_a_role_label);
    return UNITY_END();
}
