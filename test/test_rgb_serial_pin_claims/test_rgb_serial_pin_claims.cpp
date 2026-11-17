// Native unit test: the RGB LED and Serial1's flow-control input touch their
// pins only while they hold them (ARCHITECTURE.md §12.5, #646), and SYSTEM
// PINS lists every registry entry exactly once (#644).
//
// RGB LED: each channel claims its own pin and keeps it if the claim holds,
// even when another channel is refused, because the pins are wiring.ini
// copper wired to the LED (owner, 2026-11-15). The LED is driven only when
// all three held.
//
// Serial1's CTS read: the transport is built with flow control off and reads
// the pin only once serial_manager hands it over (setFlowInPin), so a pin
// another function holds is never read and can't stall a write forever.
//
// The serial claim itself (claim before begin(), the hook, RTS left an input
// on release) is in test_serial_manager, which builds serial_manager.cpp as a
// Mega.
//
// The board, as a board.h + wiring.ini would set it: an RGB LED on 7/8/9.
#define RGB_PIN_R 7
#define RGB_PIN_G 8
#define RGB_PIN_B 9
#define INCLUDE_LED 1

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <string>

#include "../../src/buses/pinctrl/pin_registry.cpp"
#include "../../src/buses/pinctrl/pin_owner.cpp"
#include "../../src/buses/pinctrl/pin_label.cpp"   // SYSTEM PINS prints pin names
#include "../../src/outputs/annunciator/rgb_led.cpp"
#include "../../src/transports/transport_serial.h"

// ---------------------------------------------------------------------------
// Console capture: SYSTEM PINS prints through msg.control
// ---------------------------------------------------------------------------
static std::string g_out;
struct CaptureTransport : public TransportInterface {
    size_t write(uint8_t c) override { g_out += (char)c; return 1; }
    size_t write(const uint8_t* b, size_t n) override { g_out.append((const char*)b, n); return n; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    const char* getName() const override { return "CAPTURE"; }
    uint8_t getCapabilities() const override { return 0; }
    TransportState getState() const override { return TRANSPORT_CONNECTED; }
    bool begin() override { return true; }
    void end() override {}
    void update() override {}
};
static CaptureTransport g_capture;

// Firmware symbols these TUs expect from elsewhere
MessageAPI msg;
LogFilter::LogFilter() {}
TransportInterface* messageSink(MessagePlane p, bool secondary) {
    return (p == PLANE_CONTROL && !secondary) ? &g_capture : nullptr;
}
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }
Input   inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;
#if INCLUDE_RULES_ENGINE
Action  actions[MAX_ACTIONS];
#endif
void watchdogReset() {}
int8_t analogIndexForPin(uint8_t) { return -1; }   // board_pins.cpp: no analog names here

// A stream that swallows writes, for the transport
struct NullStream : public Stream {
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 1; }
};

void setUp(void) {
    initPinRegistry();
    stubPinReset();
    setPinOwnerInputProbe(nullptr);
    setPinOwnerBusProbe(nullptr);
    memset(&rgbState, 0, sizeof(rgbState));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) { inputs[i].pin = 0xFF; inputs[i].applicationIndex = 0xFF; }
    numActiveInputs = 0;
#if INCLUDE_RULES_ENGINE
    for (int i = 0; i < MAX_ACTIONS; i++) actions[i].id = RULES_ID_NONE;
#endif
    g_out.clear();
}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// RGB LED
// ---------------------------------------------------------------------------
static void test_rgb_all_three_held_is_driven(void) {
    initRGBLed();
    TEST_ASSERT_TRUE(rgbLedDriven());
    TEST_ASSERT_TRUE(stubPinModeSet[RGB_PIN_R]);
    TEST_ASSERT_TRUE(stubPinModeSet[RGB_PIN_B]);
}

// Blue's pin is taken: the LED is not driven and touches none of its pins,
// but Red and Green stay claimed (wired to the LED, never handed out).
static void test_rgb_refused_channel_drives_nothing_and_keeps_the_others(void) {
    const uint8_t b = RGB_PIN_B;
    TEST_ASSERT_TRUE(claimPins(&b, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", PIN_DESC_ACTION));

    initRGBLed();
    TEST_ASSERT_FALSE(rgbLedDriven());
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_R));
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_G));
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_B));
    TEST_ASSERT_EQUAL_STRING("RGB LED Red", getPinDescription(RGB_PIN_R));
    TEST_ASSERT_EQUAL_STRING("RGB LED Green", getPinDescription(RGB_PIN_G));
    TEST_ASSERT_EQUAL_PTR(PIN_DESC_ACTION, getPinDescription(RGB_PIN_B));

    // The loop and the public API stay off the pins too
    rgbLedSolid(RGB_RED, PRIORITY_ALARM);
    updateRGBLed();
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_R));
}

// The #646 case: the registry has no room for Blue. Same result.
static void test_rgb_registry_full_drives_nothing(void) {
    // Fill the table, leaving room for exactly two entries (Red and Green)
    uint8_t pin = 100;
    while (getPinRegistrySize() < MAX_PIN_REGISTRY - 2) {
        TEST_ASSERT_TRUE(claimPins(&pin, 1, PIN_RESERVED, PIN_RANK_BUS, "TEST", "filler"));
        pin++;
    }
    initRGBLed();
    TEST_ASSERT_FALSE(rgbLedDriven());
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_R));
    TEST_ASSERT_FALSE(stubPinTouched(RGB_PIN_B));
    TEST_ASSERT_NULL(getPinDescription(RGB_PIN_B));
}

// ---------------------------------------------------------------------------
// Serial1's CTS read
// ---------------------------------------------------------------------------
static void test_transport_reads_cts_only_once_handed_the_pin(void) {
    NullStream s;
    SerialTransport t(&s, "SERIAL1", 115200);
    const uint8_t data[40] = {0};
    t.write(data, sizeof(data));
    t.write('x');
    TEST_ASSERT_EQUAL_UINT16(0, stubPinReads[2]);

    t.setFlowInPin(2);   // stubPinInput[2] = LOW: clear to send
    t.write(data, sizeof(data));
    TEST_ASSERT_TRUE(stubPinReads[2] > 0);

    const uint16_t reads = stubPinReads[2];
    t.setFlowInPin(-1);  // released: never read again
    t.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT16(reads, stubPinReads[2]);
}

// ---------------------------------------------------------------------------
// SYSTEM PINS (#644)
// ---------------------------------------------------------------------------
static int countListed() {
    int n = 0;
    for (size_t at = g_out.find("  Pin "); at != std::string::npos; at = g_out.find("  Pin ", at + 1)) n++;
    return n;
}

static void test_system_pins_lists_every_registry_entry_once(void) {
    initRGBLed();
    const uint8_t bus[2] = { 18, 19 };
    TEST_ASSERT_TRUE(claimPins(bus, 2, PIN_RESERVED, PIN_RANK_BUS, "TEST", "Wire"));
    const uint8_t btn = 4;
    TEST_ASSERT_TRUE(claimPins(&btn, 1, PIN_BUTTON, PIN_RANK_BUILTIN, "TEST", "User Button"));
    const uint8_t odd = 30;   // a type no section names
    TEST_ASSERT_TRUE(claimPins(&odd, 1, PIN_INPUT, PIN_RANK_BUILTIN, "TEST", "Something new"));
#if INCLUDE_RULES_ENGINE
    // One RELAY action holding its pin, one refused (no claim)
    actions[0].id = 1; actions[0].type = ACT_RELAY; actions[0].target = 22;
    actions[1].id = 2; actions[1].type = ACT_RELAY; actions[1].target = 23;
    const uint8_t relay = 22;
    TEST_ASSERT_TRUE(claimPins(&relay, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", PIN_DESC_ACTION));
#endif

    printPinStatus();

    TEST_ASSERT_TRUE(g_out.find("RGB LED Red") != std::string::npos);
    TEST_ASSERT_TRUE(g_out.find("RGB LED Blue") != std::string::npos);
    TEST_ASSERT_TRUE(g_out.find("Other Pins:") != std::string::npos);
    TEST_ASSERT_TRUE(g_out.find("Something new") != std::string::npos);
#if INCLUDE_RULES_ENGINE
    TEST_ASSERT_TRUE(g_out.find("Action #1\n") != std::string::npos ||
                     g_out.find("Action #1\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(g_out.find("Action #2 (not driven)") != std::string::npos);
    // Every registry entry once, plus the one refused relay
    TEST_ASSERT_EQUAL_INT(getPinRegistrySize() + 1, countListed());
#else
    TEST_ASSERT_EQUAL_INT(getPinRegistrySize(), countListed());
#endif
}


// INFO <pin>: a RELAY whose claim was refused is "not driven", not "Available"
static void test_single_pin_view_names_a_refused_relay(void) {
#if INCLUDE_RULES_ENGINE
    actions[0].id = 3; actions[0].type = ACT_RELAY; actions[0].target = 24;
    printPinStatus(24);
    TEST_ASSERT_TRUE(g_out.find("Action #3 (not driven)") != std::string::npos);
    TEST_ASSERT_TRUE(g_out.find("Available") == std::string::npos);
#endif
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_rgb_all_three_held_is_driven);
    RUN_TEST(test_rgb_refused_channel_drives_nothing_and_keeps_the_others);
    RUN_TEST(test_rgb_registry_full_drives_nothing);
    RUN_TEST(test_transport_reads_cts_only_once_handed_the_pin);
    RUN_TEST(test_system_pins_lists_every_registry_entry_once);
    RUN_TEST(test_single_pin_view_names_a_refused_relay);
    return UNITY_END();
}
