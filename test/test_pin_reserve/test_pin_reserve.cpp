// Native unit test: group pin reservation (src/lib/pin_registry.cpp) and the
// ranked ownership predicate (src/lib/pin_owner.cpp).
//
// The invariant every claimant rests on is "up implies its pins are reserved"
// (#351, #359). registerPin_unchecked() is first-writer-wins, so honoring that
// invariant means a claimant must claim ALL of its pins or none — a half-claim
// leaves the caller believing it owns copper another function is already
// driving, which is exactly the failure #359 documents (Teensy 4.x I2C1 17/16
// vs Serial4 16/17).
//
// reservePins_unchecked()/releasePins() are that all-or-nothing primitive.
// Callers reach it only through pin_owner.cpp's claimPins() — see #384's
// generalization, which also adds PinRank (pin_owner.h): an explicit,
// testable precedence ladder in place of the accident of setup() call order.
// There is no runtime eviction (no portable TwoWire::end()), so a claim always
// refuses on conflict; rank only decides which of two refusal messages is
// printed — see the "outranks"/"stale" tests below.
//
// bus_manager's use of claimPins() is a straight-line `if (!claim) return
// false;` before the peripheral is started; the peripheral half needs
// Wire/SPI/FlexCAN and is not reachable on the host, so what is tested here is
// the decision logic itself.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/transport_interface.h"
#include "../../src/lib/pin_registry.cpp"
#include "../../src/lib/pin_owner.cpp"

// ---- messaging -------------------------------------------------------------
// A tiny capturing transport lets the rank tests assert on the WORDING of a
// refusal (stale-config vs. higher-precedence), not just its boolean outcome.
MessageAPI    msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
MessageRouter router;
size_t MessageStream::error(const char* tag, const char* fmt, ...) {
    (void)tag; (void)fmt; return 0;
}

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

TransportInterface* MessageRouter::getTransport(MessagePlane plane, bool primary) {
    return (plane == PLANE_CONTROL && primary) ? &g_capture : nullptr;
}

static const char* OWNER_A = "Wire1";
static const char* OWNER_B = "Serial4";

void setUp(void)    {
    initPinRegistry();
    g_capture.reset();
    // pinOwnerBootComplete() is one-way in the firmware (boot happens once);
    // reset it per test so wording tests can pick their phase deliberately.
    bootReconciling = true;
    // Same for the #396 boot-diagnostics buffer: flushPinOwnerBootDiagnostics()
    // is one-way in the firmware too (router.begin() runs once). Reset the
    // module statics directly per test, mirroring bootReconciling above —
    // this test TU #includes pin_owner.cpp, so they're visible here. Default
    // to "not buffering" (post-flush/steady-state) so every existing test
    // that asserts on g_capture immediately keeps working unmodified; the
    // #396 tests below opt INTO the buffering window explicitly.
    bootDiagCount = 0;
    bootDiagOverflow = 0;
    bootDiagBuffering = false;
}
void tearDown(void) {}

// --- the happy path: a whole group becomes one owner's ----------------------
static void test_group_claim_reserves_every_pin(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
    TEST_ASSERT_EQUAL(PIN_RESERVED, getPinUsage(18));
    TEST_ASSERT_EQUAL(PIN_RESERVED, getPinUsage(19));
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(18));
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(19));
}

// --- the #359 case: someone already holds one of the pins ------------------
static void test_group_claim_refused_when_a_pin_is_taken(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(16, PIN_RESERVED, OWNER_B, PIN_RANK_SERIAL));

    const uint8_t pins[2] = { 17, 16 };   // I2C1 SCL/SDA over Serial4 RX/TX
    TEST_ASSERT_FALSE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
}

// --- and the refusal must not leave half a bus behind ----------------------
// This is the rollback: pin 17 was free and WAS claimed before pin 16 failed.
// Leaving it reserved would strand a pin against a bus that never started.
static void test_refused_claim_leaves_no_partial_reservation(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(16, PIN_RESERVED, OWNER_B, PIN_RANK_SERIAL));

    const uint8_t pins[2] = { 17, 16 };
    TEST_ASSERT_FALSE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));

    TEST_ASSERT_TRUE(isPinAvailable(17));
    TEST_ASSERT_EQUAL(PIN_UNUSED, getPinUsage(17));
}

// --- a refusal must never disturb the incumbent ----------------------------
static void test_refused_claim_leaves_the_incumbent_owner_intact(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(16, PIN_RESERVED, OWNER_B, PIN_RANK_SERIAL));

    const uint8_t pins[2] = { 17, 16 };
    TEST_ASSERT_FALSE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));

    TEST_ASSERT_EQUAL_STRING(OWNER_B, getPinDescription(16));
    TEST_ASSERT_EQUAL(1, getPinRegistrySize());
}

// --- re-claiming a bus that is already up is not a conflict ----------------
// initConfiguredBuses()'s fallback path and acquireI2C() can both reach
// initI2CBus() for a bus already running; that must succeed, not refuse.
static void test_reclaim_by_the_same_owner_succeeds(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
    TEST_ASSERT_TRUE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
    TEST_ASSERT_EQUAL(2, getPinRegistrySize());
}

// --- 0xFF means "this bus has no such pin on this platform" ----------------
// SPI-attached CAN controllers have no fixed TX/RX; the claim must not choke.
static void test_absent_pins_are_skipped(void) {
    const uint8_t pins[2] = { 0xFF, 0xFF };
    TEST_ASSERT_TRUE(reservePins_unchecked(pins, 2, PIN_RESERVED, "CAN1", PIN_RANK_BUS));
    TEST_ASSERT_EQUAL(0, getPinRegistrySize());
}

// --- a full registry is a refusal, not a partial claim ---------------------
static void test_registry_full_refuses_the_whole_group(void) {
    for (uint8_t i = 0; i < MAX_PIN_REGISTRY; i++) {
        TEST_ASSERT_TRUE(registerPin_unchecked(i, PIN_INPUT, "filler", PIN_RANK_INPUT));
    }
    // Free exactly one slot: a two-pin group cannot fit.
    unregisterPin(0);

    const uint8_t pins[2] = { 100, 101 };
    TEST_ASSERT_FALSE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
    TEST_ASSERT_TRUE(isPinAvailable(100));
    TEST_ASSERT_TRUE(isPinAvailable(101));
}

// --- release is the mirror image, and is owner-scoped ----------------------
static void test_release_frees_only_the_named_owners_pins(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(16, PIN_RESERVED, OWNER_B, PIN_RANK_SERIAL));
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));

    const uint8_t overlap[3] = { 18, 19, 16 };
    releasePins(overlap, 3, OWNER_A);

    TEST_ASSERT_TRUE(isPinAvailable(18));
    TEST_ASSERT_TRUE(isPinAvailable(19));
    TEST_ASSERT_EQUAL_STRING(OWNER_B, getPinDescription(16));   // not ours to free
}

// --- an input pin is not "reserved", so it must not be mistaken for ours ---
static void test_same_named_non_reserved_pin_is_still_a_conflict(void) {
    TEST_ASSERT_TRUE(registerPin_unchecked(18, PIN_INPUT, OWNER_A, PIN_RANK_INPUT));

    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_FALSE(reservePins_unchecked(pins, 2, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));
    TEST_ASSERT_EQUAL(PIN_INPUT, getPinUsage(18));
}

// ===========================================================================
// Rank (#384): no eviction either way — a claim always refuses on conflict.
// Rank only decides which of two refusal messages is printed.
// ===========================================================================

// --- the legitimate case: the holder outranks the claimant ------------------
static void test_claim_refused_when_holder_outranks_claimant(void) {
    // A bus (rank 3) already holds the pin; an action (rank 5, lower
    // precedence) tries to take it. The bus is legitimately ahead in the
    // ladder — this is NOT a stale-config situation.
    const uint8_t pin = 20;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_RESERVED, OWNER_A, PIN_RANK_BUS));

    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_ACTION, "Action output"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "precedence"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "Stale"));
}

// --- the degraded case: the claimant outranks the holder --------------------
static void test_claim_refused_when_claimant_outranks_holder(void) {
    // An action (rank 5) holds the pin; a bus (rank 3, higher precedence)
    // tries to take it. By the ladder the bus should have won — the action
    // sitting there is the anomaly, and the message must say so, loudly and
    // DIFFERENTLY from the legitimate-precedence case above.
    const uint8_t pin = 21;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_OUTPUT, "Action output", PIN_RANK_ACTION));

    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_BUS, OWNER_A));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "Stale"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "That function has precedence"));
}

// --- the same conflict, AFTER boot: rank must NOT pick the wording ----------
// Bench-caught on teensy40 during #384 verification. `BUS SERIAL 5 ENABLE`
// over pin 21 (held by a configured input) refused correctly, then told the
// user "Stale configuration: ... Reconfigure or remove it" — advice to delete
// a sensor that was doing nothing wrong. Serial (rank 2) does outrank an input
// (rank 4), but that comparison only means "the stored config is inconsistent"
// while setup() is reconciling it. Once boot is done, a conflict is one the
// user is creating right now.
static void test_after_boot_claimant_outranking_holder_is_not_called_stale(void) {
    const uint8_t pin = 21;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_INPUT, "Intake Air Temperature", PIN_RANK_INPUT));

    pinOwnerBootComplete();

    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_SERIAL, "Serial5"));
    TEST_ASSERT_NULL(strstr(g_capture.buf, "Stale"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "precedence"));
}

// The boot-phase wording must survive the gate: same ranks, boot not finished.
static void test_during_boot_claimant_outranking_holder_is_still_stale(void) {
    const uint8_t pin = 21;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_INPUT, "Intake Air Temperature", PIN_RANK_INPUT));

    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_SERIAL, "Serial5"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "Stale"));
}

// --- both cases still refuse: there is no runtime eviction -------------------
static void test_neither_rank_direction_evicts_the_holder(void) {
    const uint8_t pin = 22;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_OUTPUT, "Action output", PIN_RANK_ACTION));

    TEST_ASSERT_FALSE(claimPins(&pin, 1, PIN_RESERVED, PIN_RANK_BUS, "TEST", OWNER_A));
    TEST_ASSERT_EQUAL(PIN_OUTPUT, getPinUsage(pin));
    TEST_ASSERT_EQUAL_STRING("Action output", getPinDescription(pin));
}

// ===========================================================================
// Shuffled order (#384): pin ownership must be decided by WHO asks for WHICH
// pin, never by the sequence unrelated claims happen to run in. This is the
// fence against a setup() line move silently becoming policy again: each of
// three independent claimants (different rank, different pin) must end up
// owning the SAME pin it always did, no matter which order the three run in.
// ===========================================================================

struct Claimant { uint8_t pin; PinUsageType type; uint8_t rank; const char* owner; };

static void claimAllAndAssertOwners(const Claimant* order, uint8_t n) {
    initPinRegistry();
    for (uint8_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(claimPins(&order[i].pin, 1, order[i].type, order[i].rank,
                                   "TEST", order[i].owner));
    }
    // Independent of the order just run, each pin belongs to its own claimant.
    TEST_ASSERT_EQUAL_STRING("Button",  getPinDescription(5));
    TEST_ASSERT_EQUAL_STRING("Serial1", getPinDescription(6));
    TEST_ASSERT_EQUAL_STRING("Wire",    getPinDescription(7));
}

static void test_claim_order_does_not_change_who_ends_up_owning_each_pin(void) {
    const Claimant builtin = { 5, PIN_BUTTON,   PIN_RANK_BUILTIN, "Button" };
    const Claimant serial  = { 6, PIN_RESERVED, PIN_RANK_SERIAL,  "Serial1" };
    const Claimant bus     = { 7, PIN_RESERVED, PIN_RANK_BUS,     "Wire" };

    const Claimant order1[3] = { builtin, serial, bus };
    const Claimant order2[3] = { bus, builtin, serial };
    const Claimant order3[3] = { serial, bus, builtin };
    const Claimant order4[3] = { bus, serial, builtin };

    claimAllAndAssertOwners(order1, 3);
    claimAllAndAssertOwners(order2, 3);
    claimAllAndAssertOwners(order3, 3);
    claimAllAndAssertOwners(order4, 3);
}

// ===========================================================================
// Self-ownership must require BOTH a matching owner AND PIN_RESERVED — regression
// test for the bug the coordinator caught in review: dropping the PIN_RESERVED
// gate widened "already mine" from "this specific reserved owner" to "anything
// whose description string matches", which let a second Action silently share
// a relay target pin with the first (control_manager.cpp's ACTION_PIN_DESC is
// one shared string across every action, by design).
// ===========================================================================

// --- two claimants sharing a description, but NOT registered PIN_RESERVED,
//     must still conflict — this is exactly the Action-target case.
static void test_shared_description_non_reserved_claim_still_conflicts(void) {
    const uint8_t first = 23;
    TEST_ASSERT_TRUE(claimPins(&first, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", "Action output"));

    // A second action, same shared description, same pin: must be refused,
    // not silently treated as "already ours".
    const uint8_t second = 23;
    TEST_ASSERT_FALSE(claimPins(&second, 1, PIN_OUTPUT, PIN_RANK_ACTION, "TEST", "Action output"));
    TEST_ASSERT_EQUAL(1, getPinRegistrySize());
}

// --- a bus re-claiming its own already-held RESERVED pins must still succeed
//     (guards against over-correcting the fix above).
static void test_reserved_owner_reclaim_still_self_skips(void) {
    const uint8_t pins[2] = { 18, 19 };
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "TEST", OWNER_A));
    TEST_ASSERT_TRUE(claimPins(pins, 2, PIN_RESERVED, PIN_RANK_BUS, "TEST", OWNER_A));
    TEST_ASSERT_EQUAL(2, getPinRegistrySize());
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(18));
    TEST_ASSERT_EQUAL_STRING(OWNER_A, getPinDescription(19));
}

// ===========================================================================
// Boot diagnostics buffer (#396): a refusal made before the router has a
// transport must still reach the user, once it does. Mirrors the real boot
// order: registerSystemPins()/initConfiguredSerialPorts() claim pins BEFORE
// router.begin() runs (main.cpp :289/:293 vs ~:334) — g_capture stands in for
// "the transport router.begin() eventually attaches".
// ===========================================================================

// --- the actual bug: buffered while transport-less, visible after flush ----
static void test_boot_refusal_is_buffered_then_visible_after_flush(void) {
    bootDiagBuffering = true;   // mirrors main.cpp before router.begin()
    const uint8_t pin = 30;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_RESERVED, OWNER_B, PIN_RANK_BUS));

    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_INPUT, "candidate input"));
    // Nothing printed yet — that IS the bug this buffer exists to fix.
    TEST_ASSERT_EQUAL(0, g_capture.len);

    flushPinOwnerBootDiagnostics();
    // Now it's visible, with the same wording pinsConflict() always used.
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "pin 30"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, OWNER_B));
}

// --- flush is idempotent: a second call emits nothing -----------------------
static void test_flush_is_idempotent(void) {
    bootDiagBuffering = true;
    const uint8_t pin = 31;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_RESERVED, OWNER_B, PIN_RANK_BUS));
    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_INPUT, "candidate input"));

    flushPinOwnerBootDiagnostics();
    TEST_ASSERT_TRUE(g_capture.len > 0);

    g_capture.reset();
    flushPinOwnerBootDiagnostics();
    TEST_ASSERT_EQUAL(0, g_capture.len);
}

// --- after boot (flush already ran), a refusal prints immediately ----------
static void test_after_flush_refusal_prints_immediately_not_buffered(void) {
    bootDiagBuffering = true;         // start buffering, like early boot
    flushPinOwnerBootDiagnostics();   // router now "has a transport"
    g_capture.reset();

    const uint8_t pin = 32;
    TEST_ASSERT_TRUE(registerPin_unchecked(pin, PIN_RESERVED, OWNER_B, PIN_RANK_BUS));
    TEST_ASSERT_TRUE(pinsConflict(&pin, 1, PIN_RANK_INPUT, "candidate input"));

    // No buffering left to flush — the message must already be there.
    TEST_ASSERT_NOT_NULL(strstr(g_capture.buf, "pin 32"));
    TEST_ASSERT_EQUAL(0, bootDiagCount);
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

    RUN_TEST(test_claim_refused_when_holder_outranks_claimant);
    RUN_TEST(test_claim_refused_when_claimant_outranks_holder);
    RUN_TEST(test_after_boot_claimant_outranking_holder_is_not_called_stale);
    RUN_TEST(test_during_boot_claimant_outranking_holder_is_still_stale);
    RUN_TEST(test_neither_rank_direction_evicts_the_holder);

    RUN_TEST(test_claim_order_does_not_change_who_ends_up_owning_each_pin);

    RUN_TEST(test_shared_description_non_reserved_claim_still_conflicts);
    RUN_TEST(test_reserved_owner_reclaim_still_self_skips);

    RUN_TEST(test_boot_refusal_is_buffered_then_visible_after_flush);
    RUN_TEST(test_flush_is_idempotent);
    RUN_TEST(test_after_flush_refusal_prints_immediately_not_buffered);
    return UNITY_END();
}
