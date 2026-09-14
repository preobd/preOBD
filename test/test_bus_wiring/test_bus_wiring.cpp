// Native unit test: the bus WIRING decision predicate (#377).
//
// A bus may be brought up ONLY if the stored CONFIG explicitly names it (a
// user statement) or WIRING declares it (a build-time user statement).
// Anything else — a compiled-in default standing in for a choice the user
// never made — is INFERENCE, and that is the bug this closes: DEFAULT_I2C_BUS
// is what silently claimed Wire (18/19 on Teensy 4.x) out from under
// configured inputs on A4/A5.
//
// Two things are under test:
//
//  1. resolveWiredBusId() (src/pinctrl/bus_wiring.h) — the decision itself, in
//     isolation. It is a pure function of two bytes, so every property the
//     issue cares about is exercised directly with synthetic values,
//     independent of what this host platform's bus_defaults.h happens to
//     report (NUM_I2C_BUSES==1 here — the "UNKNOWN PLATFORM" fallback branch,
//     since no real board macro is defined on the host).
//
//  2. acquireI2C() / acquireSPI() (src/drivers/bus_manager.cpp), compiled for
//     real and exercised end to end: with neither a CONFIG selection nor a
//     WIRING declaration, they must REFUSE (return nullptr) rather than
//     falling back to a platform default. This is the regression test for the
//     exact bug #377 closes — see the mutant drill in the PR description.
//
// I2C_WIRED_BUS/SPI_WIRED_BUS are not defined for this build (profile_native.h
// declares no wiring), so BUS_WIRED_I2C_ID/BUS_WIRED_SPI_ID are always 0xFF
// here — acquireI2C()/acquireSPI() can only be exercised through their
// explicit-CONFIG-selection path and their refuse-both path; the
// declaration-authorizes and declaration-never-vetoes properties are the
// resolveWiredBusId() tests' job.
#include <unity.h>
#include <Arduino.h>

#include "../../src/pinctrl/bus_wiring.h"
#include "../../src/pinctrl/pin_registry.cpp"
#include "../../src/pinctrl/pin_owner.cpp"
#include "../../src/drivers/bus_manager.cpp"

// ---------------------------------------------------------------------------
// Firmware symbols bus_manager.cpp expects from elsewhere.
// ---------------------------------------------------------------------------
MessageRouter router;
MessageAPI msg;
SystemConfig systemConfig;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
size_t MessageStream::warn(const char*, const char*, ...) { return 0; }
size_t MessageStream::info(const char*, const char*, ...) { return 0; }
size_t MessageStream::debug(const char*, const char*, ...) { return 0; }

void setUp(void) {
    initPinRegistry();
    setPinOwnerInputProbe(nullptr);
    systemConfig.buses.active_i2c = 0xFF;
    systemConfig.buses.i2c_clock = 400;
    systemConfig.buses.active_spi = 0xFF;
    systemConfig.buses.spi_clock = 4000000;
}
void tearDown(void) {}

// ===========================================================================
// 1. resolveWiredBusId() — the decision predicate, in isolation
// ===========================================================================

// An explicit CONFIG selection always wins, whether or not anything is wired.
static void test_explicit_selection_wins_over_no_declaration(void) {
    TEST_ASSERT_EQUAL(0, resolveWiredBusId(/*configured=*/0, /*declared=*/0xFF));
}

static void test_explicit_selection_wins_over_a_declaration(void) {
    // Configured bus 0, WIRING declares bus 1: the user's live choice wins.
    TEST_ASSERT_EQUAL(0, resolveWiredBusId(/*configured=*/0, /*declared=*/1));
}

// A build-time WIRING declaration authorizes bring-up when nothing else has
// chosen — this is the whole point of #377: permission without a guess.
static void test_declaration_authorizes_when_nothing_is_configured(void) {
    TEST_ASSERT_EQUAL(1, resolveWiredBusId(/*configured=*/0xFF, /*declared=*/1));
    TEST_ASSERT_EQUAL(2, resolveWiredBusId(/*configured=*/0xFF, /*declared=*/2));
}

// Neither a selection nor a declaration: refuse. This is the fallback #377
// deletes — there is no DEFAULT_*_BUS standing in for a choice never made.
static void test_neither_selected_nor_declared_refuses(void) {
    TEST_ASSERT_EQUAL(0xFF, resolveWiredBusId(/*configured=*/0xFF, /*declared=*/0xFF));
}

// A declaration GRANTS PERMISSION; it must never VETO. Declaring bus 1 wired
// must not forbid explicitly selecting bus 0 — experimentation stays open.
static void test_declaration_never_vetoes_a_different_explicit_choice(void) {
    TEST_ASSERT_EQUAL(0, resolveWiredBusId(/*configured=*/0, /*declared=*/1));
    TEST_ASSERT_EQUAL(2, resolveWiredBusId(/*configured=*/2, /*declared=*/1));
}

// wiredBusIsInEffect() (#379 Tier 1): registerSystemPins() must reserve the
// WIRED bus's pins as a build-time physical fact ONLY in the exact case where
// resolveWiredBusId() above would fall through to the declaration — i.e.
// nothing has explicitly selected a different bus. Getting this backwards
// (reserving unconditionally) would block copper an explicit `BUS I2C <n>`
// selection is using for a DIFFERENT bus, for a declaration that only ever
// granted permission, never a veto.
static void test_wired_bus_is_in_effect_with_no_explicit_selection(void) {
    TEST_ASSERT_TRUE(wiredBusIsInEffect(/*configured=*/0xFF, /*declared=*/1));
}

static void test_wired_bus_is_not_in_effect_with_an_explicit_selection(void) {
    // Selecting the SAME bus the declaration names does not change the
    // answer: the selection is what's in effect, the declaration is moot
    // either way, and initConfiguredBuses() (rank BUS, not rank BUILTIN) is
    // where that claim belongs — see registerSystemPins()'s comment.
    TEST_ASSERT_FALSE(wiredBusIsInEffect(/*configured=*/1, /*declared=*/1));
    TEST_ASSERT_FALSE(wiredBusIsInEffect(/*configured=*/0, /*declared=*/1));
}

static void test_wired_bus_is_not_in_effect_with_no_declaration(void) {
    TEST_ASSERT_FALSE(wiredBusIsInEffect(/*configured=*/0xFF, /*declared=*/0xFF));
}

// ===========================================================================
// 2. acquireI2C() / acquireSPI() — the same rule, end to end
// ===========================================================================
//
// Ordering matters within each bus type: acquireI2C()/acquireSPI() cache the
// live bus in a file-scope static once brought up, with no reset hook (there
// is nothing to un-begin — TwoWire has no portable end(), see bus_manager.cpp).
// The refuse case therefore MUST run before the select case for the same bus,
// so it observes the bus still down. I2C and SPI have independent statics, so
// the two bus types don't interfere with each other.

// THE MUTANT TARGET: reintroduce a DEFAULT_I2C_BUS fallback inside
// acquireI2C() (`if (bus_id == 0xFF) bus_id = DEFAULT_I2C_BUS;` in place of
// the refusal) and this must fail — acquireI2C() would return &Wire instead
// of nullptr.
static void test_acquireI2C_refuses_with_no_selection_and_no_declaration(void) {
    TEST_ASSERT_NULL(getActiveI2C());
    TEST_ASSERT_NULL(acquireI2C());
    TEST_ASSERT_NULL(getActiveI2C());
}

static void test_acquireI2C_honors_an_explicit_selection(void) {
    systemConfig.buses.active_i2c = 0;
    TEST_ASSERT_NOT_NULL(acquireI2C());
    TEST_ASSERT_EQUAL_PTR(getActiveI2C(), acquireI2C());
}

// SPI counterpart of the mutant target.
static void test_acquireSPI_refuses_with_no_selection_and_no_declaration(void) {
    TEST_ASSERT_NULL(getActiveSPI());
    TEST_ASSERT_NULL(acquireSPI());
    TEST_ASSERT_NULL(getActiveSPI());
}

static void test_acquireSPI_honors_an_explicit_selection(void) {
    systemConfig.buses.active_spi = 0;
    TEST_ASSERT_NOT_NULL(acquireSPI());
    TEST_ASSERT_EQUAL_PTR(getActiveSPI(), acquireSPI());
}

// ===========================================================================
// 3. The reporting EPISODE rule
// ===========================================================================
//
// The no-bus guidance is de-duplicated within one episode so boot (which can
// reach the same refusal from acquireRequiredBuses(), main.cpp's diagnosis and
// initLCD()) prints it once rather than once per stored sensor. But the latch
// must not outlive boot: the CLI is the other place this guidance has to land.
// A user configuring an I2C input at runtime with no bus available must be told
// what to run — and on a board whose boot already printed once, a boot-lifetime
// latch would silence exactly the answer the user is acting on.
//
// MUTANT TARGET: empty out rearmBusDiagnostics() (restoring a boot-lifetime
// latch) and this must fail on the third assertion.
static void test_no_bus_guidance_is_rearmed_after_boot(void) {
    rearmBusDiagnostics();
    TEST_ASSERT_TRUE(reportNoI2CBus());   // first time in this episode: speaks
    TEST_ASSERT_FALSE(reportNoI2CBus());  // same episode: stays quiet

    rearmBusDiagnostics();                // end of setup()
    TEST_ASSERT_TRUE(reportNoI2CBus());   // the CLI must be able to say it again
}

static void test_no_bus_guidance_episode_is_per_bus_type(void) {
    rearmBusDiagnostics();
    TEST_ASSERT_TRUE(reportNoI2CBus());
    // Reporting I2C must not consume SPI's turn — a board can be missing both.
    TEST_ASSERT_TRUE(reportNoSPIBus());
    TEST_ASSERT_FALSE(reportNoSPIBus());
}

// ===========================================================================
// 4. The two refusals must not be confused for each other
// ===========================================================================
//
// acquire*() returns nullptr for two opposite reasons: "I refused to guess" and
// "you named a bus and it could not start". Saying the first when the second
// happened tells a user their build-time declaration does not exist — the exact
// misdirection #377 exists to remove. It is reachable: DEFAULT_I2C1_SDA/SCL are
// 17/16 on Teensy 4.x and DEFAULT_SERIAL4_RX/TX are 16/17, so declaring
// I2C_WIRED_BUS=1 with Serial4 enabled makes claimPins() refuse.
//
// MUTANT TARGET: report the no-bus guidance on the bus-failed path (or diagnose
// from `!getActiveI2C()` outside acquire*(), which cannot tell them apart) and
// the last assertion fails — the no-bus message would have been consumed.
static void test_a_named_bus_that_cannot_start_is_not_reported_as_undeclared(void) {
    rearmBusDiagnostics();

    // Give SDA/SCL to another owner so the bus is named but cannot claim them.
    registerPin_unchecked(getDefaultI2CSDA(0), PIN_RESERVED, "Serial4", PIN_RANK_SERIAL);
    registerPin_unchecked(getDefaultI2CSCL(0), PIN_RESERVED, "Serial4", PIN_RANK_SERIAL);
    systemConfig.buses.active_i2c = 0;          // explicitly NAMED, not guessed

    TEST_ASSERT_NULL(acquireI2C());             // still refuses to start it
    TEST_ASSERT_FALSE(reportI2CBusUnavailable(0));  // ...having said THIS instead

    // The decisive one: the no-bus guidance must be untouched. If acquireI2C()
    // had reported "no I2C bus declared or selected", the episode latch would be
    // spent and this would come back false.
    TEST_ASSERT_TRUE(reportNoI2CBus());
}

// A named bus that cannot start must NOT be silently swapped for bus 0.
// initConfiguredBuses() used to do exactly that, which on Teensy 4.x means
// claiming 18/19 for a bus nobody selected — the #377 inference in its worst
// form, since it can evict configured inputs on A4/A5.
//
// MUTANT TARGET: restore `if (active_i2c != 0) initI2CBus(0, ...)` and the last
// assertion fails — bus 0 would be up.
static void test_a_failed_selection_does_not_fall_back_to_bus_zero(void) {
    rearmBusDiagnostics();
    // Select a bus this platform does not have (host build reports
    // NUM_I2C_BUSES == 1), so bring-up fails for a reason bus 0 does NOT share
    // — bus 0's pins are deliberately left FREE here, so the old fallback would
    // succeed and be plainly visible as a live bus.
    systemConfig.buses.active_i2c = 2;

    initConfiguredBuses();

    // The decisive one: no bus at all, rather than the unrequested bus 0.
    TEST_ASSERT_NULL(getActiveI2C());
    // ...and the fault was reported as a fault, not as a missing declaration.
    TEST_ASSERT_FALSE(reportI2CBusUnavailable(2));
    TEST_ASSERT_TRUE(reportNoI2CBus());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_explicit_selection_wins_over_no_declaration);
    RUN_TEST(test_explicit_selection_wins_over_a_declaration);
    RUN_TEST(test_declaration_authorizes_when_nothing_is_configured);
    RUN_TEST(test_neither_selected_nor_declared_refuses);
    RUN_TEST(test_declaration_never_vetoes_a_different_explicit_choice);

    RUN_TEST(test_wired_bus_is_in_effect_with_no_explicit_selection);
    RUN_TEST(test_wired_bus_is_not_in_effect_with_an_explicit_selection);
    RUN_TEST(test_wired_bus_is_not_in_effect_with_no_declaration);

    RUN_TEST(test_acquireI2C_refuses_with_no_selection_and_no_declaration);
    // Before the "honors" test below: that one leaves the bus UP in a static
    // with no reset hook, and this case needs it still down (see the ordering
    // note above).
    RUN_TEST(test_a_named_bus_that_cannot_start_is_not_reported_as_undeclared);
    RUN_TEST(test_a_failed_selection_does_not_fall_back_to_bus_zero);
    RUN_TEST(test_acquireI2C_honors_an_explicit_selection);
    RUN_TEST(test_acquireSPI_refuses_with_no_selection_and_no_declaration);
    RUN_TEST(test_acquireSPI_honors_an_explicit_selection);

    RUN_TEST(test_no_bus_guidance_is_rearmed_after_boot);
    RUN_TEST(test_no_bus_guidance_episode_is_per_bus_type);
    return UNITY_END();
}
