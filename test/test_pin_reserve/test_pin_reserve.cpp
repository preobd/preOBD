// Native unit test: group pin reservation (src/lib/pin_registry.cpp).
//
// The invariant every bus rests on is "bus up implies its pins are reserved"
// (#351, #359). registerPin() is first-writer-wins, so honoring that invariant
// means a bus must claim ALL of its pins or none — a half-claim leaves the
// caller believing it owns copper another function is already driving, which is
// exactly the failure #359 documents (Teensy 4.x I2C1 17/16 vs Serial4 16/17).
//
// reservePins()/releasePins() are that all-or-nothing primitive. bus_manager's
// use of them is a straight-line `if (!claim) return false;` before the
// peripheral is started; the peripheral half needs Wire/SPI/FlexCAN and is not
// reachable on the host, so what is tested here is the decision logic itself.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/pin_registry.cpp"

// ---- messaging: only the "registry full" path prints -----------------------
MessageAPI    msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
MessageRouter router;
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::error(const char* tag, const char* fmt, ...) {
    (void)tag; (void)fmt; return 0;
}

static const char* OWNER_A = "Wire1";
static const char* OWNER_B = "Serial4";

void setUp(void)    { initPinRegistry(); }
void tearDown(void) {}

// --- the happy path: a whole group becomes one owner's ----------------------
static void test_group_claim_reserves_every_pin(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins(pins, 2, OWNER_A));
    TEST_ASSERT_EQUAL(PIN_RESERVED, getPinUsage(18));
    TEST_ASSERT_EQUAL(PIN_RESERVED, getPinUsage(19));
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(18));
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(19));
}

// --- the #359 case: someone already holds one of the pins ------------------
static void test_group_claim_refused_when_a_pin_is_taken(void) {
    TEST_ASSERT_TRUE(registerPin(16, PIN_RESERVED, OWNER_B));

    const uint8_t pins[2] = { 17, 16 };   // I2C1 SCL/SDA over Serial4 RX/TX
    TEST_ASSERT_FALSE(reservePins(pins, 2, OWNER_A));
}

// --- and the refusal must not leave half a bus behind ----------------------
// This is the rollback: pin 17 was free and WAS claimed before pin 16 failed.
// Leaving it reserved would strand a pin against a bus that never started.
static void test_refused_claim_leaves_no_partial_reservation(void) {
    TEST_ASSERT_TRUE(registerPin(16, PIN_RESERVED, OWNER_B));

    const uint8_t pins[2] = { 17, 16 };
    TEST_ASSERT_FALSE(reservePins(pins, 2, OWNER_A));

    TEST_ASSERT_TRUE(isPinAvailable(17));
    TEST_ASSERT_EQUAL(PIN_UNUSED, getPinUsage(17));
}

// --- a refusal must never disturb the incumbent ----------------------------
static void test_refused_claim_leaves_the_incumbent_owner_intact(void) {
    TEST_ASSERT_TRUE(registerPin(16, PIN_RESERVED, OWNER_B));

    const uint8_t pins[2] = { 17, 16 };
    TEST_ASSERT_FALSE(reservePins(pins, 2, OWNER_A));

    TEST_ASSERT_EQUAL_STRING(OWNER_B, getPinDescription(16));
    TEST_ASSERT_EQUAL(1, getPinRegistrySize());
}

// --- re-claiming a bus that is already up is not a conflict ----------------
// initConfiguredBuses()'s fallback path and acquireI2C() can both reach
// initI2CBus() for a bus already running; that must succeed, not refuse.
static void test_reclaim_by_the_same_owner_succeeds(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins(pins, 2, OWNER_A));
    TEST_ASSERT_TRUE(reservePins(pins, 2, OWNER_A));
    TEST_ASSERT_EQUAL(2, getPinRegistrySize());
}

// --- 0xFF means "this bus has no such pin on this platform" ----------------
// SPI-attached CAN controllers have no fixed TX/RX; the claim must not choke.
static void test_absent_pins_are_skipped(void) {
    const uint8_t pins[2] = { 0xFF, 0xFF };
    TEST_ASSERT_TRUE(reservePins(pins, 2, "CAN1"));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// --- a full registry is a refusal, not a partial claim ---------------------
static void test_registry_full_refuses_the_whole_group(void) {
    for (uint8_t i = 0; i < MAX_PIN_REGISTRY; i++) {
        TEST_ASSERT_TRUE(registerPin(i, PIN_INPUT, "filler"));
    }
    // Free exactly one slot: a two-pin group cannot fit.
    unregisterPin(0);

    const uint8_t pins[2] = { 100, 101 };
    TEST_ASSERT_FALSE(reservePins(pins, 2, OWNER_A));
    TEST_ASSERT_TRUE(isPinAvailable(100));
    TEST_ASSERT_TRUE(isPinAvailable(101));
}

// --- release is the mirror image, and is owner-scoped ----------------------
static void test_release_frees_only_the_named_owners_pins(void) {
    TEST_ASSERT_TRUE(registerPin(16, PIN_RESERVED, OWNER_B));
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins(pins, 2, OWNER_A));

    const uint8_t overlap[3] = { 18, 19, 16 };
    releasePins(overlap, 3, OWNER_A);

    TEST_ASSERT_TRUE(isPinAvailable(18));
    TEST_ASSERT_TRUE(isPinAvailable(19));
    TEST_ASSERT_EQUAL_STRING(OWNER_B, getPinDescription(16));   // not ours to free
}

// --- an input pin is not "reserved", so it must not be mistaken for ours ---
static void test_same_named_non_reserved_pin_is_still_a_conflict(void) {
    TEST_ASSERT_TRUE(registerPin(18, PIN_INPUT, OWNER_A));

    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_FALSE(reservePins(pins, 2, OWNER_A));
    TEST_ASSERT_EQUAL(PIN_INPUT, getPinUsage(18));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_group_claim_reserves_every_pin);
    RUN_TEST(test_group_claim_refused_when_a_pin_is_taken);
    RUN_TEST(test_refused_claim_leaves_no_partial_reservation);
    RUN_TEST(test_refused_claim_leaves_the_incumbent_owner_intact);
    RUN_TEST(test_reclaim_by_the_same_owner_succeeds);
    RUN_TEST(test_absent_pins_are_skipped);
    RUN_TEST(test_registry_full_refuses_the_whole_group);
    RUN_TEST(test_release_frees_only_the_named_owners_pins);
    RUN_TEST(test_same_named_non_reserved_pin_is_still_a_conflict);
    return UNITY_END();
}
