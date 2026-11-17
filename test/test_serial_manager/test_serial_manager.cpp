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
// nRF flow-control pins, as a wiring.ini would set them, so Serial1's claim
// takes CTS/RTS with RX/TX and the flow-pin hook runs (#646). The Mega has no
// nRF; these are free pins on it, and only the claim and the hook are tested.
#define HOST_NRF_CTS_PIN 2
#define HOST_NRF_RTS_PIN 3

#include <unity.h>
#include <Arduino.h>
#include <string.h>

struct FakeHardwareSerial : public Stream {
    int begins = 0;
    void begin(unsigned long) { begins++; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};
FakeHardwareSerial Serial1;
FakeHardwareSerial Serial2;
FakeHardwareSerial Serial3;

#include "../../src/buses/pinctrl/pin_registry.cpp"
#include "../../src/buses/pinctrl/pin_owner.cpp"
#include "../../src/buses/serial_manager.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols the included TUs expect from elsewhere.
// ---------------------------------------------------------------------------
MessageAPI msg;
SystemConfig systemConfig;
LogFilter::LogFilter() {}
TransportInterface* messageSink(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
static int16_t g_flowPin = 0x7FFF;   // last value the hook was given
static int     g_flowCalls = 0;
static void flowHook(int16_t pin) { g_flowPin = pin; g_flowCalls++; }

void setUp(void) {
    initPinRegistry();
    memset(&systemConfig, 0, sizeof(systemConfig));
    stubPinReset();
    Serial1.begins = Serial2.begins = 0;
    g_flowPin = 0x7FFF;
    g_flowCalls = 0;
    setSerialFlowPinHook(flowHook);
    // disableSerialPort() releases only what the port held: start from "off"
    disableSerialPort(1);
    disableSerialPort(2);
    g_flowCalls = 0;
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

// #648 review B1: disabling a port that never came up must not delete what
// else holds its default pins (an action's relay pin, which then could never
// be switched off).
static void test_disabling_an_idle_port_leaves_other_holders(void) {
    uint8_t claimed = 16;   // Serial2's TX on this profile
    TEST_ASSERT_TRUE(claimPins(&claimed, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", "Action output"));
    TEST_ASSERT_TRUE(disableSerialPort(2));
    TEST_ASSERT_EQUAL_STRING("Action output", getPinDescription(16));
}

// ...and a port that was up releases only its own pins
static void test_disabling_an_active_port_releases_only_its_own(void) {
    TEST_ASSERT_TRUE(enableSerialPort(2, BAUD_115200));
    TEST_ASSERT_NOT_NULL(getPinDescription(16));
    TEST_ASSERT_TRUE(disableSerialPort(2));
    TEST_ASSERT_NULL(getPinDescription(16));
    TEST_ASSERT_NULL(getPinDescription(17));
}


// #646: Serial1 claims RX/TX and the flow-control pins BEFORE begin(). A
// refused claim begins nothing, holds nothing, and never turns CTS reading on.
static void test_refused_serial1_claim_begins_nothing(void) {
    uint8_t cts = HOST_NRF_CTS_PIN;
    TEST_ASSERT_TRUE(claimPins(&cts, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", "Action output"));
    const uint8_t before = getPinRegistrySize();

    TEST_ASSERT_FALSE(initSerialPort(1, 115200));
    TEST_ASSERT_EQUAL_INT(0, Serial1.begins);
    TEST_ASSERT_EQUAL_UINT8(before, getPinRegistrySize());
    TEST_ASSERT_NULL(getPinDescription(getDefaultSerialRX(1)));
    TEST_ASSERT_NULL(getPinDescription(HOST_NRF_RTS_PIN));
    TEST_ASSERT_EQUAL_INT(0, g_flowCalls);
    TEST_ASSERT_FALSE(isSerialPortActive(1));
}

// A held claim begins the port and hands the transport the CTS pin; DISABLE
// takes it back (-1) and leaves RTS an input before releasing it.
static void test_serial1_flow_pin_follows_the_claim(void) {
    TEST_ASSERT_TRUE(initSerialPort(1, 115200));
    TEST_ASSERT_EQUAL_INT(1, Serial1.begins);
    TEST_ASSERT_EQUAL_INT16(HOST_NRF_CTS_PIN, g_flowPin);
    TEST_ASSERT_EQUAL_STRING("Serial1", getPinDescription(HOST_NRF_CTS_PIN));

    TEST_ASSERT_TRUE(disableSerialPort(1));
    TEST_ASSERT_EQUAL_INT16(-1, g_flowPin);
    TEST_ASSERT_TRUE(stubPinModeSet[HOST_NRF_RTS_PIN]);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[HOST_NRF_RTS_PIN]);
    TEST_ASSERT_NULL(getPinDescription(HOST_NRF_CTS_PIN));
    TEST_ASSERT_NULL(getPinDescription(HOST_NRF_RTS_PIN));
}

// ENABLE on a port that is already up adds no second set of entries
static void test_repeat_enable_adds_no_entries(void) {
    TEST_ASSERT_TRUE(enableSerialPort(1, BAUD_115200));
    const uint8_t once = getPinRegistrySize();
    TEST_ASSERT_TRUE(enableSerialPort(1, BAUD_115200));
    TEST_ASSERT_EQUAL_UINT8(once, getPinRegistrySize());
}

// Option A (#646): a port that carries commands can't be disabled from under
// the console; serialPortCarriesCommands() is what BUS SERIAL DISABLE asks.
static void test_a_console_or_framed_port_carries_commands(void) {
    TEST_ASSERT_TRUE(enableSerialPort(1, BAUD_115200));
    TEST_ASSERT_FALSE(serialPortCarriesCommands(1));
    systemConfig.console.secondary = TRANSPORT_SERIAL1;
    TEST_ASSERT_TRUE(serialPortCarriesCommands(1));
    systemConfig.console.secondary = TRANSPORT_USB_SERIAL;
    systemConfig.serial.framed_enabled_mask = 0x01;
    TEST_ASSERT_TRUE(serialPortCarriesCommands(1));
    TEST_ASSERT_TRUE(disableSerialPort(1));
    TEST_ASSERT_FALSE(serialPortCarriesCommands(1));   // not running: nothing to cut off
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_refused_enable_is_byte_identical);
    RUN_TEST(test_refused_enable_preserves_prior_config);
    RUN_TEST(test_unrefused_enable_does_change_config);
    RUN_TEST(test_disabling_an_idle_port_leaves_other_holders);
    RUN_TEST(test_disabling_an_active_port_releases_only_its_own);
    RUN_TEST(test_refused_serial1_claim_begins_nothing);
    RUN_TEST(test_serial1_flow_pin_follows_the_claim);
    RUN_TEST(test_repeat_enable_adds_no_entries);
    RUN_TEST(test_a_console_or_framed_port_carries_commands);
    return UNITY_END();
}
