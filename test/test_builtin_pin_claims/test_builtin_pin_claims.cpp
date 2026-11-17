// Native unit test: a builtin drives its pin only while its boot claim holds
// (ARCHITECTURE.md §12.5, #646).
//
// registerSystemPins() claims the buzzer, user button, MCP2515 CS and the
// test-mode pin once, at boot, and records which claims held
// (builtinPinHeld()). Every driver asks that before it touches its pin. A
// stored input wins its pin at boot (§12.4), so an input on a builtin's pin
// is the refusal under test: the builtin must leave that pin alone.
//
// The buzzer's drive sites (output_alarm.cpp) and TEST OUTPUT (command_table
// .cpp) don't build on the host, and the native build has no CAN bus for
// initCANBus()'s CS check; those are covered on the bench (PR body).
//
// The board is defined here, before any header that reads it, as a
// board.h + wiring.ini would: a buzzer, a user button and an SPI CAN CS.
#define BUZZER_PIN          5
#define USER_BUTTON_PIN     4
#define INCLUDE_USER_BUTTON 1
#define CAN_CS_0            9

#include <unity.h>
#include <Arduino.h>

#include "../../src/buses/pinctrl/pin_registry.cpp"
#include "../../src/buses/pinctrl/pin_owner.cpp"
#include "../../src/buses/bus_manager.cpp"
#include "../../src/config/system_config.cpp"
#include "../../src/outputs/table/output_table.cpp"   // the stored outputs (#612)
#include "../../src/console/user_button.cpp"

// Firmware symbols these TUs expect from elsewhere (as test_bus_wired_need_gate).
MessageAPI msg;
LogFilter::LogFilter() {}
TransportInterface* messageSink(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// A configured input on one pin, as the input manager's probe answers it
static uint8_t g_inputPin = 0xFF;
static bool inputProbe(uint8_t pin) { return pin == g_inputPin; }

void setUp(void) {
    initPinRegistry();
    stubPinReset();
    g_inputPin = 0xFF;
    setPinOwnerInputProbe(inputProbe);
    setPinOwnerBusProbe(nullptr);
    systemConfig.buses.active_i2c = 0xFF;
    systemConfig.buses.active_spi = 0xFF;
    builtinHeldFlags = 0;   // file-static in system_config.cpp, set only at boot
    g_test_now = 1000;
}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// The held bits
// ---------------------------------------------------------------------------
static void test_clean_boot_holds_every_builtin(void) {
    registerSystemPins(BUS_NEED_NONE);
    TEST_ASSERT_TRUE(builtinPinHeld(BPK_BUZZER));
    TEST_ASSERT_TRUE(builtinPinHeld(BPK_USER_BUTTON));
    TEST_ASSERT_TRUE(builtinPinHeld(BPK_CAN_CS));
}

static void test_input_on_buzzer_pin_refuses_only_the_buzzer(void) {
    g_inputPin = BUZZER_PIN;
    registerSystemPins(BUS_NEED_NONE);
    TEST_ASSERT_FALSE(builtinPinHeld(BPK_BUZZER));
    TEST_ASSERT_TRUE(builtinPinHeld(BPK_USER_BUTTON));
    TEST_ASSERT_TRUE(builtinPinHeld(BPK_CAN_CS));
    TEST_ASSERT_FALSE(stubPinTouched(BUZZER_PIN));
}

// ---------------------------------------------------------------------------
// MCP2515 CS: deselected at boot when held, HIGH latched before OUTPUT
// ---------------------------------------------------------------------------
static void test_held_can_cs_is_deselected_level_first(void) {
    registerSystemPins(BUS_NEED_NONE);
    // The CS's own events, in order: INPUT, latch HIGH, OUTPUT, HIGH
    StubPinEvent ev[8]; int n = 0;
    for (int i = 0; i < stubPinLogN && n < 8; i++)
        if (stubPinLog[i].pin == CAN_CS_0) ev[n++] = stubPinLog[i];
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_CHAR('M', ev[0].kind); TEST_ASSERT_EQUAL_UINT8(INPUT,  ev[0].value);
    TEST_ASSERT_EQUAL_CHAR('W', ev[1].kind); TEST_ASSERT_EQUAL_UINT8(HIGH,   ev[1].value);
    TEST_ASSERT_EQUAL_CHAR('M', ev[2].kind); TEST_ASSERT_EQUAL_UINT8(OUTPUT, ev[2].value);
    TEST_ASSERT_EQUAL_CHAR('W', ev[3].kind); TEST_ASSERT_EQUAL_UINT8(HIGH,   ev[3].value);
}

static void test_refused_can_cs_is_never_touched(void) {
    g_inputPin = CAN_CS_0;
    registerSystemPins(BUS_NEED_NONE);
    TEST_ASSERT_FALSE(builtinPinHeld(BPK_CAN_CS));
    TEST_ASSERT_FALSE(stubPinTouched(CAN_CS_0));
}

// ---------------------------------------------------------------------------
// User button
// ---------------------------------------------------------------------------
static void test_held_button_reads_and_detects_a_press(void) {
    registerSystemPins(BUS_NEED_NONE);
    stubPinInput[USER_BUTTON_PIN] = HIGH;   // released
    initUserButton();
    TEST_ASSERT_EQUAL_UINT8(INPUT_PULLUP, stubPinMode[USER_BUTTON_PIN]);

    stubPinInput[USER_BUTTON_PIN] = LOW;    // pressed
    TEST_ASSERT_TRUE(userButtonDownNow());
    updateUserButton(); g_test_now += 100;
    updateUserButton(); g_test_now += 100;  // debounced: press starts
    stubPinInput[USER_BUTTON_PIN] = HIGH;
    updateUserButton(); g_test_now += 100;
    TEST_ASSERT_EQUAL_INT(BUTTON_SHORT_PRESS, updateUserButton());
}

// A sensor that sits LOW on a refused button's pin: the boot check must not
// see CONFIG, the loop must not see presses (they'd silence alarms), and the
// pin gets no pull-up (it would skew a resistive sender).
static void test_refused_button_never_touches_or_reads_its_pin(void) {
    g_inputPin = USER_BUTTON_PIN;
    registerSystemPins(BUS_NEED_NONE);
    TEST_ASSERT_FALSE(builtinPinHeld(BPK_USER_BUTTON));

    stubPinInput[USER_BUTTON_PIN] = LOW;
    initUserButton();
    TEST_ASSERT_FALSE(userButtonDownNow());
    for (int i = 0; i < 40; i++) {           // 4 s of a sensor toggling
        stubPinInput[USER_BUTTON_PIN] = (i & 4) ? HIGH : LOW;
        TEST_ASSERT_EQUAL_INT(BUTTON_NONE, updateUserButton());
        g_test_now += 100;
    }
    TEST_ASSERT_FALSE(stubPinTouched(USER_BUTTON_PIN));
    TEST_ASSERT_EQUAL_UINT16(0, stubPinReads[USER_BUTTON_PIN]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clean_boot_holds_every_builtin);
    RUN_TEST(test_input_on_buzzer_pin_refuses_only_the_buzzer);
    RUN_TEST(test_held_can_cs_is_deselected_level_first);
    RUN_TEST(test_refused_can_cs_is_never_touched);
    RUN_TEST(test_held_button_reads_and_detects_a_press);
    RUN_TEST(test_refused_button_never_touches_or_reads_its_pin);
    return UNITY_END();
}
