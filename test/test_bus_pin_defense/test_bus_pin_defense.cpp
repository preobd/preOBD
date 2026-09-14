// Native unit test: a stored-but-not-yet-started BUS selection defends its
// pins (#379) — pinsConflict()'s third source (pin_owner.h/.cpp).
//
// `BUS I2C <n>` / `BUS SPI <n>` store a selection and defer bring-up to
// reboot (cmd_bus.cpp). Between the command and the reboot, the selected
// pins were in nobody's name: the pin registry only reflects an
// ALREADY-STARTED bus, and the input probe doesn't cover buses at all. A
// later command claiming those same physical pins for something else (e.g.
// `BUS SERIAL <n> ENABLE`) was accepted, and the collision surfaced only at
// the next reboot when initConfiguredBuses() and initConfiguredSerialPorts()
// both tried to claim the same copper.
//
// storedBusHoldsPin() (src/drivers/bus_manager.cpp) is the probe that closes this;
// it is registered with pin_owner.cpp via setPinOwnerBusProbe() and consulted
// by pinsConflict() only when the pin registry itself has nothing to say
// (an ALREADY-STARTED bus is still judged by the ordinary registry path, with
// its usual rank-based wording — untouched by this change).
//
// The wording matters as much as the boolean: a stored selection is a FRESH
// conflict the user is creating right now by typing the second command, not
// an existing claim gone stale, so the refusal must never say "Stale
// configuration" even when the new claimant numerically outranks a bus
// selection (SERIAL=2 vs BUS=3) — see pin_owner.cpp's dedicated branch.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/core/transport_interface.h"
#include "../../src/pinctrl/bus_wiring.h"
#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/drivers/bus_manager.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols bus_manager.cpp expects from elsewhere.
// ---------------------------------------------------------------------------
SystemConfig systemConfig;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// A capturing CONTROL transport so tests can assert on the exact wording a
// user sees (test_pin_reserve.cpp's pattern) — the boolean alone can't tell
// "refused, correctly worded" from "refused, but tells the user the wrong
// story" (the "Stale" regression this test suite exists to catch).
class CaptureTransport : public TransportInterface {
public:
    char buf[512];
    size_t len = 0;
    size_t write(uint8_t c) override {
        if (len < sizeof(buf) - 1) buf[len++] = (char)c;
        buf[len] = '\0';
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
        for (size_t i = 0; i < n; i++) write(b[i]);
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    const char* getName() const override { return "CAP"; }
    uint8_t getCapabilities() const override { return CAP_WRITE; }
    TransportState getState() const override { return TRANSPORT_CONNECTED; }
    bool begin() override { return true; }
    void end() override {}
    void update() override {}
    void reset() { len = 0; buf[0] = '\0'; }
};
static CaptureTransport g_capture;
MessageAPI msg;
MessageRouter router;
TransportInterface* MessageRouter::getTransport(MessagePlane plane, bool primary) {
    return (plane == PLANE_CONTROL && primary) ? &g_capture : nullptr;
}

void setUp(void) {
    initPinRegistry();
    setPinOwnerInputProbe(nullptr);
    setPinOwnerBusProbe(storedBusHoldsPin);
    g_capture.reset();
    // #396: this TU #includes pin_owner.cpp directly, so its boot-diagnostics
    // buffer statics are visible here too. Default to "not buffering" so the
    // existing g_capture wording assertions below (written pre-#396) keep
    // seeing output immediately, same as test_pin_reserve.cpp.
    bootDiagCount = 0;
    bootDiagOverflow = 0;
    bootDiagBuffering = false;
    systemConfig.buses.active_i2c = 0xFF;
    systemConfig.buses.i2c_clock = 400;
    systemConfig.buses.active_spi = 0xFF;
    systemConfig.buses.spi_clock = 4000000;
#if NUM_CAN_BUSES > 0
    systemConfig.buses.output_can_bus = 0xFF;
    systemConfig.buses.can_output_enabled = 0;
    systemConfig.buses.input_can_bus = 0xFF;
    systemConfig.buses.can_input_mode = CAN_INPUT_OFF;
#endif
}
void tearDown(void) { setPinOwnerBusProbe(nullptr); }

// ===========================================================================
// 1. storedBusHoldsPin() in isolation
// ===========================================================================

static void test_no_stored_selection_holds_nothing(void) {
    const char* desc = nullptr;
    TEST_ASSERT_FALSE(storedBusHoldsPin(getDefaultI2CSDA(0), nullptr, &desc));
}

static void test_a_stored_i2c_selection_holds_its_pins(void) {
    systemConfig.buses.active_i2c = 0;
    const char* desc = nullptr;
    TEST_ASSERT_TRUE(storedBusHoldsPin(getDefaultI2CSDA(0), nullptr, &desc));
    TEST_ASSERT_EQUAL_STRING(getI2CBusName(0), desc);
    TEST_ASSERT_TRUE(storedBusHoldsPin(getDefaultI2CSCL(0), nullptr, &desc));
}

static void test_a_stored_spi_selection_holds_its_pins(void) {
    systemConfig.buses.active_spi = 0;
    const char* desc = nullptr;
    TEST_ASSERT_TRUE(storedBusHoldsPin(getDefaultSPIMOSI(0), nullptr, &desc));
    TEST_ASSERT_EQUAL_STRING(getSPIBusName(0), desc);
}

// An unrelated pin is not "held" just because SOME bus is selected.
static void test_unrelated_pin_is_not_held(void) {
    systemConfig.buses.active_i2c = 0;
    const char* desc = nullptr;
    TEST_ASSERT_FALSE(storedBusHoldsPin(200, nullptr, &desc));
}

// Self-exclusion: the bus's OWN bring-up must not see itself as a conflict —
// this is what lets initI2CBus() call claimPins() for the very bus that is
// stored as selected without refusing against its own pending selection.
static void test_self_exclusion_by_owner_description(void) {
    systemConfig.buses.active_i2c = 0;
    const char* desc = nullptr;
    TEST_ASSERT_FALSE(storedBusHoldsPin(getDefaultI2CSDA(0), getI2CBusName(0), &desc));
    // A DIFFERENT owner asking about the same pin still sees the conflict.
    TEST_ASSERT_TRUE(storedBusHoldsPin(getDefaultI2CSDA(0), "Serial4", &desc));
}

// 0xFF is never "held" by anything.
static void test_absent_pin_is_never_held(void) {
    systemConfig.buses.active_i2c = 0;
    const char* desc = nullptr;
    TEST_ASSERT_FALSE(storedBusHoldsPin(0xFF, nullptr, &desc));
}

// ===========================================================================
// 2. Boot self-conflict: initI2CBus() must still be able to start the very
//    bus that is stored as selected (the scenario test 5 above guards in
//    isolation, exercised here through claimPins() end to end).
// ===========================================================================

static void test_the_selected_bus_can_still_start_itself(void) {
    systemConfig.buses.active_i2c = 0;
    TEST_ASSERT_TRUE(initI2CBus(0, 400));
    TEST_ASSERT_EQUAL_STRING(getI2CBusName(0), getPinDescription(getDefaultI2CSDA(0)));
}

// ===========================================================================
// 3. #379's own reproduction: the two orderings, with exact wording
// ===========================================================================

// Order A: `BUS I2C 0` (selection stored, not started) THEN a serial port
// tries to claim an overlapping pin. Serial (rank 2) numerically outranks a
// bus selection (rank 3) — by the ladder alone the claimant "should win" —
// but this is a FRESH conflict the user is creating right now, not an
// established claim gone stale, so the refusal must read as "claimed by
// ...; free it first or choose another", never "Stale configuration".
static void test_order_A_i2c_selected_then_serial_is_refused_not_stale(void) {
    systemConfig.buses.active_i2c = 0;   // `BUS I2C 0` already run

    const uint8_t serialPin = getDefaultI2CSDA(0);   // overlapping physical pin
    TEST_ASSERT_TRUE(pinsConflict(&serialPin, 1, PIN_RANK_SERIAL, "Serial1"));

    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "is claimed by"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, getI2CBusName(0)));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "free it first or choose another"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "Stale"));
    // And nothing was actually reserved — a refused claim must not half-claim.
    TEST_ASSERT_TRUE(isPinAvailable(serialPin));
}

// Order B: the serial port claims the pin FIRST (and so is a real, registered
// holder by the time I2C is selected) — the ordinary registry path handles
// this, unaffected by the new probe. Serial (rank 2) legitimately outranks
// the bus selection (rank 3) here too, so the wording is the SAME
// non-stale, "has precedence" flavor — for a different, pre-existing reason.
static void test_order_B_serial_first_then_i2c_selection_is_refused_not_stale(void) {
    const uint8_t pin = getDefaultI2CSDA(0);
    TEST_ASSERT_TRUE(claimPins(&pin, 1, PIN_RESERVED, PIN_RANK_SERIAL, "TEST", "Serial1"));
    g_capture.reset();

    const uint8_t i2cPins[2] = { getDefaultI2CSDA(0), getDefaultI2CSCL(0) };
    TEST_ASSERT_TRUE(pinsConflict(i2cPins, 2, PIN_RANK_BUS, getI2CBusName(0)));

    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "precedence"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "Stale"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "is claimed by"));  // the OTHER branch's wording
}

// The reverse rank relationship still isn't "stale" either: a stored SPI
// selection (rank 3) blocking a later Action claim (rank 5, weaker) is the
// ordinary "holder has precedence" case, same as order A/B — the "Stale"
// wording is reserved for the pin_owner.cpp registry path entirely; the
// pending-selection branch never emits it, by construction.
static void test_pending_selection_never_says_stale_regardless_of_rank(void) {
    systemConfig.buses.active_spi = 0;
    const uint8_t pin = getDefaultSPIMOSI(0);
    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_ACTION, "Action output"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "Stale"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_stored_selection_holds_nothing);
    RUN_TEST(test_a_stored_i2c_selection_holds_its_pins);
    RUN_TEST(test_a_stored_spi_selection_holds_its_pins);
    RUN_TEST(test_unrelated_pin_is_not_held);
    RUN_TEST(test_self_exclusion_by_owner_description);
    RUN_TEST(test_absent_pin_is_never_held);

    RUN_TEST(test_the_selected_bus_can_still_start_itself);

    RUN_TEST(test_order_A_i2c_selected_then_serial_is_refused_not_stale);
    RUN_TEST(test_order_B_serial_first_then_i2c_selection_is_refused_not_stale);
    RUN_TEST(test_pending_selection_never_says_stale_regardless_of_rank);
    return UNITY_END();
}
