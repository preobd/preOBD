// Native unit test: RelayBroadcaster (src/lib/relay_broadcaster.cpp).
//
// The relay half of issue #221. Two directions are exercised entirely on the
// host: (a) tick() packs systemConfig.relays[] into 16-byte PreobdRelayState
// frames and emits on change, and (b) handleInbound() applies a client-written
// PreobdRelayState to the relay config API. We capture the emitted bytes and
// record the config-API calls; the real transport and relay HW are not linked.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/relay_broadcaster.cpp"

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
SystemConfig systemConfig;

// getRelayState() is declared in output_relay.h; we own the HW truth here.
static bool g_relayState[MAX_RELAYS];
bool getRelayState(uint8_t i) { return (i < MAX_RELAYS) ? g_relayState[i] : false; }

// Record config-API calls made by handleInbound().
static int       g_setModeCalls = 0;
static uint8_t   g_setModeIdx = 0;
static RelayMode g_setModeVal = RELAY_DISABLED;
bool setRelayMode(uint8_t idx, RelayMode m) {
    ++g_setModeCalls; g_setModeIdx = idx; g_setModeVal = m; return true;
}
static int   g_setThreshCalls = 0;
static uint8_t g_setThreshIdx = 0;
static float g_setThreshOn = 0, g_setThreshOff = 0;
bool setRelayThresholds(uint8_t idx, float on, float off) {
    ++g_setThreshCalls; g_setThreshIdx = idx; g_setThreshOn = on; g_setThreshOff = off;
    return true;
}

// ---- Capturing transport (records every frame this tick) ----
struct CapturedFrame { uint8_t type; uint16_t len; uint8_t payload[32]; };
static CapturedFrame g_frames[8];
static int           g_frameCount = 0;

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (g_frameCount < 8) {
        g_frames[g_frameCount].type = type;
        g_frames[g_frameCount].len  = len;
        if (len <= sizeof(g_frames[0].payload)) memcpy(g_frames[g_frameCount].payload, payload, len);
    }
    ++g_frameCount;
    return true;
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

static const PreobdRelayState& frameState(int i) {
    return *reinterpret_cast<const PreobdRelayState*>(g_frames[i].payload);
}

void setUp(void) {
    g_frameCount = 0;
    g_test_now = 0;   // tick() now reads millis(); start each test at t=0
    memset(g_frames, 0, sizeof(g_frames));
    memset(&systemConfig, 0, sizeof(systemConfig));
    memset(g_relayState, 0, sizeof(g_relayState));
    g_setModeCalls = g_setThreshCalls = 0;
    RelayBroadcaster::init();
}
void tearDown(void) {}

static void configureRelay(uint8_t i, uint8_t mode, uint8_t inputIdx,
                           uint8_t pin, float on, float off) {
    systemConfig.relays[i].mode = mode;
    systemConfig.relays[i].inputIndex = inputIdx;
    systemConfig.relays[i].outputPin = pin;
    systemConfig.relays[i].thresholdOn = on;
    systemConfig.relays[i].thresholdOff = off;
}

// First tick snapshots every relay slot (cache starts empty) → MAX_RELAYS
// frames, with strictly increasing sequence numbers.
static void test_first_tick_snapshots_all_relays(void) {
    configureRelay(0, RELAY_AUTO_HIGH, 3, 9, 100.0f, 95.0f);
    g_relayState[0] = true;  // relay 0 currently ON

    RelayBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(MAX_RELAYS, g_frameCount);          // 2
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_RELAY, g_frames[0].type);
    TEST_ASSERT_EQUAL_UINT16(PREOBD_RELAY_STATE_SIZE, g_frames[0].len);  // 16

    const PreobdRelayState& r0 = frameState(0);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, r0.protocolVersion);
    TEST_ASSERT_EQUAL_UINT8(0, r0.relayIndex);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_RELAY_MODE_AUTO_HIGH, r0.mode);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_RELAY_STATE_ON, r0.state);   // getRelayState()==true
    TEST_ASSERT_EQUAL_UINT8(3, r0.inputIndex);
    TEST_ASSERT_EQUAL_UINT8(9, r0.outputPin);
    TEST_ASSERT_EQUAL_UINT16(0, r0.sequence);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, r0.thresholdOn);
    TEST_ASSERT_EQUAL_FLOAT(95.0f, r0.thresholdOff);

    // Sequence strictly increases across the two slots.
    TEST_ASSERT_EQUAL_UINT8(1, frameState(1).relayIndex);
    TEST_ASSERT_EQUAL_UINT16(1, frameState(1).sequence);
}

// No field changed since the last tick → nothing re-emitted.
static void test_idempotent_after_snapshot(void) {
    configureRelay(0, RELAY_AUTO_HIGH, 3, 9, 100.0f, 95.0f);
    RelayBroadcaster::tick();        // snapshot both
    g_frameCount = 0;                // ignore the snapshot
    RelayBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// Flipping a relay's physical state emits exactly that relay, state updated.
static void test_state_change_emits_one_frame(void) {
    configureRelay(0, RELAY_AUTO_HIGH, 3, 9, 100.0f, 95.0f);
    g_relayState[0] = false;
    RelayBroadcaster::tick();        // snapshot, seq 0/1
    g_frameCount = 0;

    g_relayState[0] = true;          // relay 0 turns ON
    RelayBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdRelayState& r = frameState(0);
    TEST_ASSERT_EQUAL_UINT8(0, r.relayIndex);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_RELAY_STATE_ON, r.state);
    TEST_ASSERT_EQUAL_UINT16(2, r.sequence);   // continued from snapshot's 0,1
}

// A client write (#221) applies mode + thresholds to the targeted relay and
// invalidates the cache so a confirmation notify follows on the next tick.
static void test_handle_inbound_applies_and_confirms(void) {
    configureRelay(0, RELAY_DISABLED, 0xFF, 0xFF, 0, 0);
    RelayBroadcaster::tick();        // snapshot
    g_frameCount = 0;

    PreobdRelayState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.relayIndex = 0;
    req.mode = PREOBD_RELAY_MODE_MANUAL_ON;
    req.thresholdOn = 88.0f;
    req.thresholdOff = 80.0f;

    RelayBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));

    TEST_ASSERT_EQUAL_INT(1, g_setModeCalls);
    TEST_ASSERT_EQUAL_UINT8(0, g_setModeIdx);
    TEST_ASSERT_EQUAL(RELAY_MANUAL_ON, g_setModeVal);
    TEST_ASSERT_EQUAL_INT(1, g_setThreshCalls);
    TEST_ASSERT_EQUAL_FLOAT(88.0f, g_setThreshOn);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, g_setThreshOff);

    // Cache invalidated → relay 0 re-emitted next tick (mode now reflects MANUAL_ON).
    systemConfig.relays[0].mode = PREOBD_RELAY_MODE_MANUAL_ON;  // host applied it
    RelayBroadcaster::tick();
    TEST_ASSERT_TRUE(g_frameCount >= 1);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_RELAY_MODE_MANUAL_ON, frameState(0).mode);
}

// Wrong payload length is rejected outright (no config mutation).
static void test_handle_inbound_rejects_bad_length(void) {
    uint8_t shortBuf[8] = {0};
    RelayBroadcaster::handleInbound(shortBuf, sizeof(shortBuf));
    TEST_ASSERT_EQUAL_INT(0, g_setModeCalls);
    TEST_ASSERT_EQUAL_INT(0, g_setThreshCalls);
}

// Out-of-range relayIndex is rejected.
static void test_handle_inbound_rejects_bad_index(void) {
    PreobdRelayState req;
    memset(&req, 0, sizeof(req));
    req.relayIndex = MAX_RELAYS;     // one past the end
    req.mode = PREOBD_RELAY_MODE_MANUAL_ON;
    RelayBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_setModeCalls);
}

// #271: with no field change, the broadcaster stays quiet until the heartbeat
// elapses, then re-emits every slot so a late-connecting client populates.
static void test_heartbeat_resends_all_relays(void) {
    configureRelay(0, RELAY_AUTO_HIGH, 3, 9, 100.0f, 95.0f);
    RelayBroadcaster::tick();              // snapshot at t=0
    g_frameCount = 0;

    RelayBroadcaster::tick();              // still t=0, no change → quiet
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);

    g_test_now = 5000;                     // kHeartbeatMs elapsed
    RelayBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(MAX_RELAYS, g_frameCount);  // full resend
}

// #271: resync() (wired to the connect-time ping) drops the cache so the next
// tick re-emits every slot immediately, without waiting for the heartbeat.
static void test_resync_forces_full_resend(void) {
    configureRelay(0, RELAY_AUTO_HIGH, 3, 9, 100.0f, 95.0f);
    RelayBroadcaster::tick();              // snapshot
    g_frameCount = 0;

    RelayBroadcaster::tick();              // no change → quiet
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);

    RelayBroadcaster::resync();            // a client (re)connected
    RelayBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(MAX_RELAYS, g_frameCount);  // full resend, same t
}

// An out-of-enum mode is not forwarded to setRelayMode (but thresholds still apply).
static void test_handle_inbound_clamps_invalid_mode(void) {
    PreobdRelayState req;
    memset(&req, 0, sizeof(req));
    req.relayIndex = 0;
    req.mode = PREOBD_RELAY_MODE_MANUAL_OFF + 1;  // 5: invalid
    RelayBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_setModeCalls);     // mode rejected
    TEST_ASSERT_EQUAL_INT(1, g_setThreshCalls);   // thresholds still applied
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_snapshots_all_relays);
    RUN_TEST(test_idempotent_after_snapshot);
    RUN_TEST(test_state_change_emits_one_frame);
    RUN_TEST(test_handle_inbound_applies_and_confirms);
    RUN_TEST(test_handle_inbound_rejects_bad_length);
    RUN_TEST(test_handle_inbound_rejects_bad_index);
    RUN_TEST(test_heartbeat_resends_all_relays);
    RUN_TEST(test_resync_forces_full_resend);
    RUN_TEST(test_handle_inbound_clamps_invalid_mode);
    return UNITY_END();
}
