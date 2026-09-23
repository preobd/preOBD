// Native unit test: nRF52840 mode-negotiation state machine
// (nrf_firmware/src/tier_state.cpp).
//
// This is nRF firmware, not host firmware, but the logic is pure decision
// making over a clock and two collaborators, so it runs natively here rather
// than only on a bench rig where the failure it guards is a timing window
// (#449): the host stops answering PINGs for a moment -- rebooting, or its
// framer stuck mid-frame after a UART overrun -- and the nRF concludes the
// host cannot speak framed at all. It then passes the host's binary frames
// raw into the BLE client's terminal for the rest of the session, which is
// how a wedged import surfaced as `pOBD` magic and a boot banner in the
// webapp's console. A host that HAS answered once is known framed-capable,
// so the ping is retried instead; a host that never answered keeps the
// original one-shot fallback so an HM-10 / stock-Bluefruit setup still Just
// Works.
#include <unity.h>
#include <stdint.h>
#include <stddef.h>

#include "../stubs/Arduino.h"

// --- Collaborator stubs, defined before the unit under test ---------------
// Serial1 is the host UART; tier_state only ever writes whole PING frames.
static int g_pingsWritten = 0;
struct FakeSerial1 {
    size_t write(const uint8_t*, size_t n) { ++g_pingsWritten; return n; }
};
static FakeSerial1 Serial1;

static int g_decoderResets = 0;
void uartBridgeDecoderReset() { ++g_decoderResets; }
// Resync PINGs go through the TX ring; accepted unless a test says otherwise.
static bool g_txRingAccepts = true;
bool uartBridgeSendFrame(uint8_t type, const uint8_t*, uint16_t) {
    if (!g_txRingAccepts) return false;
    if (type == 0xFF) ++g_pingsWritten;
    return true;
}

#include "../../nrf_firmware/src/frame_codec.cpp"
#include "../../nrf_firmware/src/tier_state.cpp"

// Timeouts are private to the .cpp; mirror them here so a change to either
// constant breaks this test rather than silently rescoping the behaviour.
static constexpr uint32_t kPing = 2000;    // kPingTimeoutMs
static constexpr uint8_t  kRetries = 4;    // kPingRetriesForFramedHost

static void advance(uint32_t ms) { g_test_now += ms; tierStatePoll(); }

void setUp(void) {
    g_test_now = 1000;
    g_pingsWritten = 0;
    g_decoderResets = 0;
    g_txRingAccepts = true;
    tierStateInit();
}
void tearDown(void) {}

// Baseline: the host answers the connect PING and the link is framed.
static void test_host_answers_ping_resolves_framed(void) {
    tierStateOnConnect();
    TEST_ASSERT_TRUE(tierStateIsPinging());
    TEST_ASSERT_EQUAL_INT(1, g_pingsWritten);

    tierStateOnPingFrame();
    TEST_ASSERT_TRUE(tierStateIsFramed());
    TEST_ASSERT_FALSE(tierStateIsPinging());

    // No retry machinery runs once resolved.
    advance(kPing * 10);
    TEST_ASSERT_TRUE(tierStateIsFramed());
    TEST_ASSERT_EQUAL_INT(1, g_pingsWritten);
}

// A host that has NEVER answered is assumed transparent after one window --
// the HM-10 / stock-Bluefruit path, unchanged by #449.
static void test_silent_host_falls_back_after_one_window(void) {
    tierStateOnConnect();
    advance(kPing);
    TEST_ASSERT_FALSE(tierStateIsFramed());
    TEST_ASSERT_FALSE(tierStateIsPinging());
    TEST_ASSERT_EQUAL_INT(1, g_pingsWritten);   // no retry for an unknown host
}

// #449 core: a host that answered earlier is known framed-capable, so a
// missed window is treated as "busy", not "transparent" -- it keeps pinging,
// and the link resolves FRAMED once the host comes back.
static void test_known_framed_host_is_retried_not_demoted(void) {
    tierStateOnConnect();
    tierStateOnPingFrame();                     // first session: framed
    tierStateOnDisconnect();

    tierStateOnConnect();                       // host is mid-reboot now
    g_pingsWritten = 0;
    g_decoderResets = 0;

    advance(kPing);                             // window 1 lapses
    TEST_ASSERT_TRUE(tierStateIsPinging());     // NOT transparent
    TEST_ASSERT_FALSE(tierStateIsFramed());
    TEST_ASSERT_EQUAL_INT(1, g_pingsWritten);   // retried
    // Each retry clears any half-frame the host's silence left behind.
    TEST_ASSERT_EQUAL_INT(1, g_decoderResets);

    advance(kPing);                             // window 2 lapses
    TEST_ASSERT_TRUE(tierStateIsPinging());
    TEST_ASSERT_EQUAL_INT(2, g_pingsWritten);

    tierStateOnPingFrame();                     // host is back
    TEST_ASSERT_TRUE(tierStateIsFramed());
}

// Retries are bounded: a host that answered once but is now gone for good
// (user swapped to transparent firmware) still ends up transparent rather
// than pinging forever at a client that will never see text.
static void test_retries_are_bounded_then_transparent(void) {
    tierStateOnConnect();
    tierStateOnPingFrame();
    tierStateOnDisconnect();

    tierStateOnConnect();
    g_pingsWritten = 0;
    for (uint8_t i = 0; i < kRetries; ++i) {
        advance(kPing);
        TEST_ASSERT_TRUE(tierStateIsPinging());
    }
    TEST_ASSERT_EQUAL_INT(kRetries, g_pingsWritten);

    advance(kPing);                             // budget exhausted
    TEST_ASSERT_FALSE(tierStateIsPinging());
    TEST_ASSERT_FALSE(tierStateIsFramed());
    TEST_ASSERT_EQUAL_INT(kRetries, g_pingsWritten);   // no further pings
}

// Each connect gets a fresh budget: a host that was merely slow last time
// (some retries spent, then it answered) must not inherit that spent count.
static void test_retry_budget_resets_per_connect(void) {
    tierStateOnConnect();
    tierStateOnPingFrame();
    tierStateOnDisconnect();

    tierStateOnConnect();                       // slow host: 2 retries, then up
    advance(kPing);
    advance(kPing);
    tierStateOnPingFrame();
    TEST_ASSERT_TRUE(tierStateIsFramed());
    tierStateOnDisconnect();

    tierStateOnConnect();                       // new session, full budget
    g_pingsWritten = 0;
    for (uint8_t i = 0; i < kRetries; ++i) {
        advance(kPing);
        TEST_ASSERT_TRUE(tierStateIsPinging());
    }
    TEST_ASSERT_EQUAL_INT(kRetries, g_pingsWritten);
}

// Spending a WHOLE budget is the host telling us it no longer speaks framed
// (reflashed without framed support, FRAMED DISABLE + reboot). The 10 s
// penalty is therefore paid once, not on every reconnect for as long as the
// nRF stays powered -- 10 s of dead terminal each time.
static void test_exhausted_budget_forgets_the_host_was_framed(void) {
    tierStateOnConnect();
    tierStateOnPingFrame();
    tierStateOnDisconnect();

    tierStateOnConnect();                       // host is gone for good
    for (uint8_t i = 0; i <= kRetries; ++i) advance(kPing);
    TEST_ASSERT_FALSE(tierStateIsPinging());
    TEST_ASSERT_FALSE(tierStateIsFramed());

    tierStateOnDisconnect();
    tierStateOnConnect();                       // next connect: no penalty
    g_pingsWritten = 0;
    advance(kPing);
    TEST_ASSERT_FALSE(tierStateIsPinging());    // straight to transparent
    TEST_ASSERT_EQUAL_INT(0, g_pingsWritten);   // and no retry pings

    // ...and if the host comes back framed, it is trusted again from then on.
    tierStateOnDisconnect();
    tierStateOnConnect();
    tierStateOnPingFrame();
    TEST_ASSERT_TRUE(tierStateIsFramed());
    tierStateOnDisconnect();
    tierStateOnConnect();
    advance(kPing);
    TEST_ASSERT_TRUE(tierStateIsPinging());     // retried once more
}

// A transparent link never emits a resync PING (#435): there is no framer on
// the other end to resync, and a raw 0xAA... would land in the user's text.
static void test_resync_ping_dropped_when_transparent(void) {
    tierStateOnConnect();
    advance(kPing);                             // silent host -> transparent
    g_pingsWritten = 0;

    tierStateRequestResync();
    advance(300);                               // past kResyncDebounceMs
    TEST_ASSERT_EQUAL_INT(0, g_pingsWritten);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_host_answers_ping_resolves_framed);
    RUN_TEST(test_silent_host_falls_back_after_one_window);
    RUN_TEST(test_known_framed_host_is_retried_not_demoted);
    RUN_TEST(test_retries_are_bounded_then_transparent);
    RUN_TEST(test_retry_budget_resets_per_connect);
    RUN_TEST(test_exhausted_budget_forgets_the_host_was_framed);
    RUN_TEST(test_resync_ping_dropped_when_transparent);
    return UNITY_END();
}
