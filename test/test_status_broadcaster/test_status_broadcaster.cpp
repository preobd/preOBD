// Native unit test: StatusBroadcaster (src/outputs/ble/status_broadcaster.cpp).
//
// Every status push is a PAIR on the wire: the 8-byte PreobdStatusExt (frame
// 0x07) immediately followed by the 32-byte PreobdSystemStatus (0x03), which
// the nRF joins into one 40-byte characteristic value (#455). We capture the
// frames handed to the transport and assert the pairing, the extension
// layout, and that an ack/config generation edge sends NOW rather than on the
// 5 s heartbeat. The ack counter here is a small model; the real
// rules_manager bumps are driven in test_rule_warn.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "boot_order_fixture.h"  // inputsTableIsLiveForAssert() stub (#352, #369)
#include "../../src/outputs/ble/status_broadcaster.cpp"
#include "../../src/core/config_generation.cpp"

// ---- Globals / collaborators the broadcaster TU expects ----
uint8_t numActiveInputs = 3;
static SystemMode g_mode = MODE_RUN;
SystemMode getCurrentMode() { return g_mode; }
uint8_t rulesActionCount() { return 2; }

// Ack state: a tiny model of rules_manager's acked set whose generation is
// bumped exactly where the real one is (ack of >=1 rule, lapse of each rule).
static uint8_t g_acked = 0;
static uint8_t g_ackGen = 0;
uint8_t rulesAckCount() { return g_acked; }
uint8_t rulesAckGeneration() { return g_ackGen; }
static void ack(uint8_t n)   { if (n) { g_acked += n; g_ackGen++; } }
static void lapse()          { if (g_acked) { g_acked--; g_ackGen++; } }
static bool g_silenceLed = false;
bool alarmSilenceIncludesLed() { return g_silenceLed; }

#define ADV(ms) (g_test_now += (ms))

// ---- Capturing transport ----
struct Sent { uint8_t type; uint16_t len; uint8_t payload[40]; };
static Sent g_sent[64];
static int  g_sentCount = 0;
static int  g_failType  = -1;   // refuse frames of this type

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if ((int)type == g_failType) return false;
    if (g_sentCount < 64) {
        g_sent[g_sentCount].type = type;
        g_sent[g_sentCount].len  = len;
        memcpy(g_sent[g_sentCount].payload, payload, len <= 40 ? len : 40);
    }
    ++g_sentCount;
    return true;
}
// The pair goes through the same capture, but atomically: g_pairBudget models
// the TX buffer's free bytes (-1 = unlimited) and the pair needs BOTH encoded
// frames (13 + 37 B) to fit, exactly like the real availableForWrite check.
static int g_pairBudget = -1;
static int g_pairRefusals = 0;
bool MuxTransport::sendFramePair(uint8_t t1, const uint8_t* p1, uint16_t l1,
                                 uint8_t t2, const uint8_t* p2, uint16_t l2) {
    const int need = (FRAME_HEADER_BYTES + l1 + 1) + (FRAME_HEADER_BYTES + l2 + 1);
    if ((int)t1 == g_failType || (int)t2 == g_failType ||
        (g_pairBudget >= 0 && g_pairBudget < need)) {
        ++g_pairRefusals;
        return false;
    }
    return sendFrame(t1, p1, l1) && sendFrame(t2, p2, l2);
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

static const PreobdStatusExt& lastExt() {
    // The ext is the second-to-last frame of the most recent pair.
    return *reinterpret_cast<const PreobdStatusExt*>(g_sent[g_sentCount - 2].payload);
}

void setUp(void) {
    g_sentCount = 0;
    g_failType = -1;
    g_pairBudget = -1;
    g_pairRefusals = 0;
    g_acked = 0;
    g_ackGen = 0;
    g_silenceLed = false;
    g_mode = MODE_RUN;
    g_test_now = 100000;
    StatusBroadcaster::init();
    StatusBroadcaster::tick();     // first send
    g_sentCount = 0;
}
void tearDown(void) {}

void test_first_send_is_ext_then_status_pair(void) {
    StatusBroadcaster::init();
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_STATUS_EXT, g_sent[0].type);
    TEST_ASSERT_EQUAL(PREOBD_STATUS_EXT_SIZE, g_sent[0].len);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_STATUS, g_sent[1].type);
    TEST_ASSERT_EQUAL(PREOBD_STATUS_SIZE, g_sent[1].len);
    const PreobdStatusExt& e = lastExt();
    TEST_ASSERT_EQUAL(PREOBD_STATUS_EXT_LAYOUT, e.extLayout);
    TEST_ASSERT_EQUAL(0, e.flags);
    TEST_ASSERT_EQUAL(0, e.reserved);
}

void test_quiet_until_heartbeat(void) {
    ADV(1000);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(0, g_sentCount);   // uptime alone is not a change
    ADV(4000);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);   // heartbeat still a pair
}

void test_ack_edge_sends_immediately(void) {
    ADV(10);
    ack(1);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL(1, lastExt().ackedCount);
    TEST_ASSERT_EQUAL(1, lastExt().ackGeneration);
    TEST_ASSERT_BITS_HIGH(PREOBD_STATUS_FLAG_ACKED, lastExt().flags);

    // Set changes but count doesn't (one lapses, another is acked): the
    // generation still moves, so it still sends.
    g_sentCount = 0;
    lapse(); ack(1);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL(1, lastExt().ackedCount);
    TEST_ASSERT_EQUAL(3, lastExt().ackGeneration);

    g_sentCount = 0;
    lapse();
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_BITS_LOW(PREOBD_STATUS_FLAG_ACKED, lastExt().flags);
}

void test_config_bump_sends_immediately(void) {
    ADV(10);
    configGenerationBump();
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL(configGeneration(), lastExt().configGeneration);
}

void test_silence_led_flag(void) {
    g_silenceLed = true;
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_BITS_HIGH(PREOBD_STATUS_FLAG_SILENCE_LED, lastExt().flags);
}

void test_status_refused_retries_the_pair(void) {
    g_failType = FRAME_TYPE_STATUS;
    ack(1);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(0, g_sentCount);   // all-or-nothing: no lone EXT
    g_failType = -1;
    StatusBroadcaster::tick();           // nothing committed -> resend both
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_STATUS_EXT, g_sent[0].type);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_STATUS, g_sent[1].type);
    g_sentCount = 0;
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(0, g_sentCount);   // committed now
}

// Review finding (HIGH): with two independent sends, a TX buffer that keeps
// ~13 B free but never the whole 50 B pair emitted a lone EXT every tick and
// no 0x03 ever again. The pair must be refused whole, every tick, until it fits.
void test_busy_buffer_never_emits_a_lone_ext(void) {
    g_pairBudget = 20;                   // room for the EXT frame, not the pair
    ack(1);
    for (int i = 0; i < 50; i++) { ADV(3); StatusBroadcaster::tick(); }
    TEST_ASSERT_EQUAL(0, g_sentCount);
    TEST_ASSERT_EQUAL(50, g_pairRefusals);
    g_pairBudget = 64;                   // drains
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    TEST_ASSERT_EQUAL(1, lastExt().ackGeneration);
}

void test_ext_refused_does_not_send_status(void) {
    g_failType = FRAME_TYPE_STATUS_EXT;
    ack(1);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(0, g_sentCount);   // never a bare 0x03 for a change
}

void test_resync_sends_on_next_tick(void) {
    ADV(10);
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(0, g_sentCount);
    StatusBroadcaster::resync();
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
}

void test_mode_change_still_sends(void) {
    g_mode = MODE_CONFIG;
    StatusBroadcaster::tick();
    TEST_ASSERT_EQUAL(2, g_sentCount);
    const PreobdSystemStatus* s =
        reinterpret_cast<const PreobdSystemStatus*>(g_sent[1].payload);
    TEST_ASSERT_EQUAL(PREOBD_MODE_CONFIG, s->systemMode);
    TEST_ASSERT_EQUAL(PREOBD_BLE_PROTOCOL_VERSION, s->protocolVersion);  // byte 4 untouched
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_send_is_ext_then_status_pair);
    RUN_TEST(test_quiet_until_heartbeat);
    RUN_TEST(test_ack_edge_sends_immediately);
    RUN_TEST(test_config_bump_sends_immediately);
    RUN_TEST(test_silence_led_flag);
    RUN_TEST(test_status_refused_retries_the_pair);
    RUN_TEST(test_busy_buffer_never_emits_a_lone_ext);
    RUN_TEST(test_ext_refused_does_not_send_status);
    RUN_TEST(test_resync_sends_on_next_tick);
    RUN_TEST(test_mode_change_still_sends);
    return UNITY_END();
}
