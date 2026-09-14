// Native unit test: registerSystemPins()'s Tier-1 *_WIRED_BUS reservation is
// gated on NEED, not just on the declaration being in effect (#394).
//
// #379 taught registerSystemPins() to reserve a *_WIRED_BUS declaration's
// pins at Tier 1 whenever no explicit `BUS I2C/SPI <n>` selection overrides
// it (wiredBusIsInEffect(), test/test_bus_wiring). But that reservation was
// unconditional in the "in effect" case: acquireRequiredBuses() (bus_manager
// .cpp) only ever STARTS a bus something votes for — a stored input's sensor
// requirement or the display (#358) — so a declared-but-unneeded bus never
// runs, yet its pins were reserved anyway, silently dropping any configured
// input sitting on that copper. This suite is the regression test for the
// fix: registerSystemPins() now takes the SAME busNeeds vote
// acquireRequiredBuses() uses (main.cpp computes it once, before either call)
// and only reserves a declared bus's pins when the declaration is BOTH in
// effect AND needed.
//
// This is a SEPARATE suite from test_bus_wiring/test_bus_pin_defense rather
// than an extension of either: those suites' own comments rely on
// I2C_WIRED_BUS/SPI_WIRED_BUS being UNDEFINED for their build (so
// BUS_WIRED_I2C_ID stays 0xFF and registerSystemPins()'s reservation blocks
// don't even compile in) — see test_bus_wiring.cpp's header comment. Testing
// the reservation itself requires the opposite: a real declared bus, defined
// as a translation-unit-local macro BELOW, before any header that reads it is
// included — the same effect as a board's `-D I2C_WIRED_BUS=0`, scoped to
// just this test binary.
#define I2C_WIRED_BUS 0   // "Wire" on the UNKNOWN-PLATFORM native host: SDA=18, SCL=19

#include <unity.h>
#include <Arduino.h>

#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/drivers/bus_manager.cpp"
#include "../../src/storage/system_config.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols these TUs expect from elsewhere.
// ---------------------------------------------------------------------------
MessageRouter router;
MessageAPI msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

// system_config.cpp defines the global `systemConfig` itself (system_config.h
// declares `extern SystemConfig systemConfig;` and system_config.cpp is the
// one TU that instantiates it in the real firmware) — do NOT redeclare it
// here the way the other bus suites do; a second definition would be a link
// error against the one #include "../../src/storage/system_config.cpp" below already provides.

static const uint8_t I2C0_SDA = 18;
static const uint8_t I2C0_SCL = 19;

void setUp(void) {
    initPinRegistry();
    setPinOwnerInputProbe(nullptr);
    setPinOwnerBusProbe(nullptr);
    systemConfig.buses.active_i2c = 0xFF;
    systemConfig.buses.i2c_clock = 400;
    systemConfig.buses.active_spi = 0xFF;
    systemConfig.buses.spi_clock = 4000000;
}
void tearDown(void) {}

// ===========================================================================
// 1. Declared but NOT needed: pins stay free, a stored input on them survives
// ===========================================================================
//
// THE MUTANT TARGET (first test below): drop the `busNeeds & BUS_NEED_I2C`
// half of the condition in system_config.cpp's Tier-1 block (i.e. go back to
// reserving whenever wiredBusIsInEffect() alone is true) and this must fail —
// registerSystemPins(BUS_NEED_NONE) would reserve 18/19 anyway.
static void test_declared_but_unneeded_bus_leaves_its_pins_free(void) {
    registerSystemPins(BUS_NEED_NONE);

    TEST_ASSERT_TRUE(isPinAvailable(I2C0_SDA));
    TEST_ASSERT_TRUE(isPinAvailable(I2C0_SCL));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// The concrete case from the issue: A4/A5 (== SDA/SCL here) hold configured
// analog inputs, no I2C sensor and no display — the input must be able to
// claim its pin after boot's Tier-1 pass, exactly as loadInputConfig() will.
static void test_a_stored_input_on_the_unneeded_buss_pins_survives(void) {
    registerSystemPins(BUS_NEED_NONE);

    const uint8_t p = I2C0_SDA;
    TEST_ASSERT_TRUE(claimPins(&p, 1, PIN_RESERVED, PIN_RANK_INPUT, "TEST", "FPR (A4)"));
    TEST_ASSERT_EQUAL_STRING("FPR (A4)", getPinDescription(I2C0_SDA));
}

// ===========================================================================
// 2. Declared AND needed: the #384 property — still reserved ahead of serial
// ===========================================================================
//
// MUTANT TARGET: gate the reservation on `busNeeds` alone, dropping
// wiredBusIsInEffect() (i.e. reserve whenever needed, regardless of an
// explicit selection) — covered separately by section 3 below, which this
// test does not exercise (no explicit selection is set here).
static void test_declared_and_needed_bus_still_reserves_ahead_of_serial(void) {
    registerSystemPins(BUS_NEED_I2C);

    // Reserved already, by Tier 1 — the pin registry shows it before any
    // serial port has had a chance to run initConfiguredSerialPorts().
    TEST_ASSERT_FALSE(isPinAvailable(I2C0_SDA));
    TEST_ASSERT_EQUAL_STRING(getI2CBusName(0), getPinDescription(I2C0_SDA));

    // The #384 property itself: a serial port claiming the same physical pin
    // AFTER registerSystemPins() (simulating initConfiguredSerialPorts() at
    // main.cpp:275, which runs after it) is refused — Tier 1 (BUILTIN) beats
    // Tier 2 (SERIAL).
    TEST_ASSERT_FALSE(claimPins(&I2C0_SDA, 1, PIN_RESERVED, PIN_RANK_SERIAL, "TEST", "Serial1"));
    TEST_ASSERT_EQUAL_STRING(getI2CBusName(0), getPinDescription(I2C0_SDA));
}

// ===========================================================================
// 3. An explicit `BUS I2C <n>` selection still overrides the declaration
//    (#377 behaviour) — must not regress now that a second condition exists.
// ===========================================================================
//
// MUTANT TARGET: drop wiredBusIsInEffect() from the condition entirely,
// leaving only `busNeeds & BUS_NEED_I2C` — this must fail, because bus 0's
// pins (18/19) would get reserved even though the explicit selection named a
// DIFFERENT bus (only one I2C bus exists on this host, so "a different bus"
// is modeled the same way test_bus_wiring.cpp models it: a configured bus id
// that isn't 0xFF, which alone makes wiredBusIsInEffect() false regardless of
// which id — see that predicate's own tests for why).
static void test_explicit_selection_still_overrides_the_declaration_when_needed(void) {
    systemConfig.buses.active_i2c = 0;   // an explicit `BUS I2C 0` selection is stored

    registerSystemPins(BUS_NEED_I2C);    // needed AND declared — but NOT "in effect"

    // registerSystemPins()'s Tier-1 pass must not have touched 18/19 at all:
    // the explicit selection is what's in effect, defended instead by
    // storedBusHoldsPin() / rank-BUS at initConfiguredBuses() (#379's other
    // half) — not by this function.
    TEST_ASSERT_TRUE(isPinAvailable(I2C0_SDA));
    TEST_ASSERT_TRUE(isPinAvailable(I2C0_SCL));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_declared_but_unneeded_bus_leaves_its_pins_free);
    RUN_TEST(test_a_stored_input_on_the_unneeded_buss_pins_survives);
    RUN_TEST(test_declared_and_needed_bus_still_reserves_ahead_of_serial);
    RUN_TEST(test_explicit_selection_still_overrides_the_declaration_when_needed);
    return UNITY_END();
}
