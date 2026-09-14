// Native unit test: a refused BUS SERIAL <n> ENABLE must leave systemConfig
// byte-identical (#390).
//
// enableSerialPort() used to write systemConfig.serial (enabled_mask +
// baudrate_index) BEFORE calling initSerialPort(), which can refuse — most
// concretely on a pin conflict. A refusal left the config mutated anyway; a
// later unrelated SAVE would persist a port the firmware itself had just
// refused to bring up, and at the next boot initConfiguredSerialPorts()
// (main.cpp) would claim the pins for real, ahead of buses and Action
// targets, silently stealing them (the #390 bench repro: two live relay
// Actions on Serial2's RX/TX pins were disabled by a command that had
// already been refused).
//
// NUM_SERIAL_PORTS resolves to 0 on the native/host build by default
// (bus_defaults.h's "unknown platform" fallback overrides whatever
// profile_native.h supplies), which would refuse every port_id before
// initSerialPort() ever reaches the pin-conflict check — a real refusal, but
// the wrong one to exercise here. This TU instead compiles serial_manager.cpp
// as if it were an Arduino Mega (__AVR_ATmega2560__): that gives
// NUM_SERIAL_PORTS=3 and real RX/TX pin numbers, and drives the actual
// pinsConflict()/claimPins() path inside initSerialPort() — the one the fix
// is about. HardwareSerial isn't reachable on the host, so Serial1/2/3 below
// are minimal stand-ins with just enough surface (.begin()) for that switch
// to link.
#define __AVR_ATmega2560__ 1

#include <unity.h>
#include <Arduino.h>
#include <string.h>

struct FakeHardwareSerial : public Stream {
    void begin(unsigned long) {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};
FakeHardwareSerial Serial1;
FakeHardwareSerial Serial2;
FakeHardwareSerial Serial3;

#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/drivers/serial_manager.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols the included TUs expect from elsewhere.
// ---------------------------------------------------------------------------
MessageAPI msg;
SystemConfig systemConfig;
MessageRouter router;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
void setUp(void) {
    initPinRegistry();
    memset(&systemConfig, 0, sizeof(systemConfig));
}
void tearDown(void) {}

// Serial2 on the Mega profile is RX=17/TX=16 (DEFAULT_SERIAL2_RX/TX above).
// Claim TX (16) for something else first, exactly like the bench repro's
// Action output on the pin Serial2 wants — enableSerialPort() must refuse
// AND leave systemConfig untouched.
static void test_refused_enable_is_byte_identical(void) {
    uint8_t claimed = 16;
    TEST_ASSERT_TRUE(claimPins(&claimed, 1, PIN_RESERVED, PIN_RANK_ACTION,
                                "TEST", "Action output"));

    SystemConfig before;
    memcpy(&before, &systemConfig, sizeof(SystemConfig));

    TEST_ASSERT_FALSE(enableSerialPort(2, BAUD_115200));

    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &systemConfig, sizeof(SystemConfig)));
}

// Sharper form: seed the config the way an operator's prior session would
// have left it (a different port already enabled at a non-default baud), so
// a careless fix that clears-then-fails-to-restore, rather than genuinely
// deferring the write, would still be caught.
static void test_refused_enable_preserves_prior_config(void) {
    uint8_t claimed = 16;
    TEST_ASSERT_TRUE(claimPins(&claimed, 1, PIN_RESERVED, PIN_RANK_ACTION,
                                "TEST", "Action output"));

    systemConfig.serial.enabled_mask = 0x01;              // Serial1 already on
    systemConfig.serial.baudrate_index[0] = BAUD_57600;
    SystemConfig before;
    memcpy(&before, &systemConfig, sizeof(SystemConfig));

    TEST_ASSERT_FALSE(enableSerialPort(2, BAUD_115200));

    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &systemConfig, sizeof(SystemConfig)));
}

// Control: with no conflict, Serial2 enables normally and the config DOES
// change — confirms the refusal above is actually about the conflict, not
// some other reason initSerialPort() might bail early.
static void test_unrefused_enable_does_change_config(void) {
    TEST_ASSERT_TRUE(enableSerialPort(2, BAUD_115200));
    TEST_ASSERT_TRUE(systemConfig.serial.enabled_mask & (1 << 1));
    TEST_ASSERT_EQUAL_UINT8(BAUD_115200, systemConfig.serial.baudrate_index[1]);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_refused_enable_is_byte_identical);
    RUN_TEST(test_refused_enable_preserves_prior_config);
    RUN_TEST(test_unrefused_enable_does_change_config);
    return UNITY_END();
}
