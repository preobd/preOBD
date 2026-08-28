// Native unit test: LiveValueBroadcaster wire payload
// (src/lib/live_value_broadcaster.cpp).
//
// The broadcaster scans the sparse inputs[] table at a 500 ms gate and emits
// a 16-byte PreobdLiveValue on change + 5 s heartbeat. We capture the bytes
// handed to the transport and assert layout, change detection, the NaN
// single-fire contract, display-unit conversion, and backpressure retry.
// Transport + unit conversion are faked.
#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>

#include "../../src/lib/live_value_broadcaster.cpp"

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
Input inputs[MAX_INPUTS];

// Fake conversion: unitsIndex 1 behaves like C→F (x1.8+32), everything else
// is a base unit (identity) — enough to assert conversion is applied.
float convertFromBaseUnits(float baseValue, uint8_t unitsIndex) {
    return (unitsIndex == 1) ? baseValue * 1.8f + 32.0f : baseValue;
}

// ---- Capturing transport ----
static int      g_frameCount = 0;
static bool     g_sendResult = true;
static uint8_t  g_frameType  = 0;
static uint16_t g_frameLen   = 0;
static PreobdLiveValue g_lastBySlot[MAX_INPUTS];  // last payload per inputIndex
static int      g_countBySlot[MAX_INPUTS];

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (!g_sendResult) return false;
    g_frameType = type;
    g_frameLen  = len;
    if (len == PREOBD_LIVE_VALUE_SIZE) {
        PreobdLiveValue v;
        memcpy(&v, payload, sizeof(v));
        if (v.inputIndex < MAX_INPUTS) {
            g_lastBySlot[v.inputIndex] = v;
            ++g_countBySlot[v.inputIndex];
        }
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

static void resetCapture(void) {
    g_frameCount = 0;
    g_frameType = 0;
    g_frameLen = 0;
    memset(g_lastBySlot, 0, sizeof(g_lastBySlot));
    memset(g_countBySlot, 0, sizeof(g_countBySlot));
}

// Advance past the scan gate and tick once.
static void scanTick(void) {
    g_test_now += 500;
    LiveValueBroadcaster::tick();
}

void setUp(void) {
    g_test_now = 0;
    g_sendResult = true;
    resetCapture();
    memset(inputs, 0, sizeof(inputs));
    for (int i = 0; i < MAX_INPUTS; ++i) inputs[i].pin = 0xFF;  // sparse-table sentinel
    LiveValueBroadcaster::init();
}
void tearDown(void) {}

static void configInput(int slot, uint8_t pin, float value, uint8_t unitsIndex, bool enabled) {
    inputs[slot].pin = pin;
    inputs[slot].value = value;
    inputs[slot].unitsIndex = unitsIndex;
    inputs[slot].flags.isEnabled = enabled ? 1 : 0;
}

// Cold start emits the CONFIGURED slots only — the catch-up burst scales with
// the input count, not with MAX_INPUTS (#337). Assert the payload layout too.
static void test_cold_start_payload_layout(void) {
    configInput(2, 21, 42.0f, /*units*/0, /*enabled*/true);
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);            // the one occupied slot
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_LIVE, g_frameType);
    TEST_ASSERT_EQUAL_UINT16(PREOBD_LIVE_VALUE_SIZE, g_frameLen);

    const PreobdLiveValue& v = g_lastBySlot[2];
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, v.protocolVersion);
    TEST_ASSERT_EQUAL_UINT8(2, v.inputIndex);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_LIVE_FLAG_CONFIGURED | PREOBD_LIVE_FLAG_ENABLED |
                            PREOBD_LIVE_FLAG_VALID, v.flags);
    TEST_ASSERT_EQUAL_UINT8(0, v.unitsIndex);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, v.value);

    TEST_ASSERT_EQUAL_INT(0, g_countBySlot[0]);        // …and nothing for an empty one
}

// The empty-slot payload shape, kept from the pre-#337 cold-start test: when an
// empty slot DOES emit (a resync of a freed slot inside its grace window), it is
// all-zero apart from version/index.
static void test_empty_slot_payload_shape(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    inputs[2].pin = 0xFF;
    scanTick();
    const PreobdLiveValue& e = g_lastBySlot[2];
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, e.protocolVersion);
    TEST_ASSERT_EQUAL_UINT8(2, e.inputIndex);
    TEST_ASSERT_EQUAL_UINT8(0, e.flags);
    TEST_ASSERT_EQUAL_UINT8(0, e.unitsIndex);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.value);
}

// A configured-but-DISABLED input still has a tile, so it keeps its cold-start
// frame (flags 0 tells the client "disabled", which is not the same as absent).
static void test_cold_start_includes_disabled_slot(void) {
    configInput(2, 21, 42.0f, 0, /*enabled*/false);
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_countBySlot[2]);
    // CONFIGURED without ENABLED — "switched off", NOT "gone". A client that
    // dropped its tile on flags == 0 would otherwise drop every disabled input.
    TEST_ASSERT_EQUAL_UINT8(PREOBD_LIVE_FLAG_CONFIGURED, g_lastBySlot[2].flags);
    TEST_ASSERT_EQUAL_INT(0, g_countBySlot[0]);   // the truly empty slot stays quiet
}

// The transition #337 must NOT lose: a slot that goes occupied → freed emits
// exactly one frame so the client can drop the tile, then falls silent.
static void test_freed_slot_emits_one_clearing_frame(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();

    inputs[2].pin = 0xFF;                    // CLEAR <pin>
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(2, g_lastBySlot[2].inputIndex);
    TEST_ASSERT_EQUAL_UINT8(0, g_lastBySlot[2].flags);   // 0, not CONFIGURED: gone

    resetCapture();                          // no change → no change-driven frame
    scanTick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// The clearing frame is edge-triggered and the transport has no end-to-end ACK,
// so one shot is not enough: a client that misses it would show a stale tile
// forever. The freed slot stays in the heartbeat for a bounded window, then
// drops out for good — the #337 win without losing the self-healing.
static void test_freed_slot_clear_repeats_then_stops(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    inputs[2].pin = 0xFF;
    scanTick();                              // the edge-triggered clear
    resetCapture();

    for (int hb = 0; hb < 3; ++hb) {         // kClearHeartbeats
        g_test_now += 5000;
        LiveValueBroadcaster::tick();
        TEST_ASSERT_EQUAL_INT(1, g_countBySlot[2]);
        TEST_ASSERT_EQUAL_UINT8(0, g_lastBySlot[2].flags);
        resetCapture();
    }

    g_test_now += 5000;                      // window closed: silent for good
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
    g_test_now += 5000;
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// A slot that was ALREADY disabled when it was cleared must still emit the
// clear: disabled packs CONFIGURED and freed packs 0, so the transition is
// visible. This is the case a naive "both are flags 0" key would have lost.
static void test_disabled_then_freed_still_emits_clear(void) {
    configInput(2, 21, 42.0f, 0, /*enabled*/false);
    scanTick();
    resetCapture();

    inputs[2].pin = 0xFF;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(0, g_lastBySlot[2].flags);
}

// Regression (#349 bench, teensy40): the grace window is spent per clearing
// frame the transport ACCEPTS, never per heartbeat edge. It used to decrement on
// every heartbeat that came due, so on a link busy enough to refuse the sends —
// which is the normal case there, the Action broadcaster's 64-slot burst fills
// the 64-byte TX buffer — the window expired having delivered nothing. The clear
// went out once as the edge push and the repeats never arrived, so a client that
// missed it (or was disconnected across the CLEAR) showed a stale tile for good:
// precisely the self-healing this window exists to provide.
static void test_starved_heartbeats_do_not_consume_clear_window(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    inputs[2].pin = 0xFF;
    scanTick();                              // the edge-triggered clear
    resetCapture();

    g_sendResult = false;                    // transport refuses everything
    for (int hb = 0; hb < 6; ++hb) {         // twice the window, all starved
        g_test_now += 5000;
        LiveValueBroadcaster::tick();
    }
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);  // nothing got out, by construction

    g_sendResult = true;                     // link recovers
    for (int hb = 0; hb < 3; ++hb) {         // window intact: kClearHeartbeats
        g_test_now += 5000;
        LiveValueBroadcaster::tick();
        TEST_ASSERT_EQUAL_INT(1, g_countBySlot[2]);
        TEST_ASSERT_EQUAL_UINT8(0, g_lastBySlot[2].flags);
        resetCapture();
    }

    g_test_now += 5000;                      // and still closes for good
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

static void test_no_reemit_when_unchanged(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    scanTick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

static void test_scan_gate_holds_below_interval(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    inputs[2].value = 99.0f;   // changed, but gate not yet elapsed
    g_test_now += 100;
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

static void test_change_emits_display_converted(void) {
    configInput(2, 21, 100.0f, /*units*/1, true);   // fake C→F conversion
    scanTick();
    resetCapture();
    inputs[2].value = 110.0f;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);         // only the changed slot
    TEST_ASSERT_EQUAL_FLOAT(110.0f * 1.8f + 32.0f, g_lastBySlot[2].value);
    TEST_ASSERT_EQUAL_UINT8(1, g_lastBySlot[2].unitsIndex);
}

// A faulted (non-finite) sensor must emit exactly one !VALID transition:
// packKey zeroes the key value and clears VALID, so the cached key matches
// on the next scan and the slot stays quiet.
static void test_nan_single_fire(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    inputs[2].value = NAN;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_LIVE_FLAG_CONFIGURED | PREOBD_LIVE_FLAG_ENABLED,
                            g_lastBySlot[2].flags);  // VALID cleared
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_lastBySlot[2].value);
    resetCapture();
    scanTick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);          // no re-fire while still NAN
}

// Inf (e.g. conversion overflow) must not ship as a VALID reading either —
// VALID is gated on isfinite, not !isnan.
static void test_inf_clears_valid(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    inputs[2].value = INFINITY;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_LIVE_FLAG_CONFIGURED | PREOBD_LIVE_FLAG_ENABLED,
                            g_lastBySlot[2].flags);  // VALID cleared
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_lastBySlot[2].value);
}

// A units change alone (same base value, e.g. `SET <pin> UNITS`) must re-emit
// with the new unitsIndex and re-converted value.
static void test_units_change_reemits(void) {
    configInput(2, 21, 100.0f, /*units*/0, true);
    scanTick();
    resetCapture();
    inputs[2].unitsIndex = 1;                        // fake C→F conversion
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(1, g_lastBySlot[2].unitsIndex);
    TEST_ASSERT_EQUAL_FLOAT(100.0f * 1.8f + 32.0f, g_lastBySlot[2].value);
}

// Sequence stamps must be strictly increasing across emitted frames — the
// client uses them for drop detection.
static void test_sequence_increments(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    const uint16_t s1 = g_lastBySlot[2].sequence;
    inputs[2].value = 50.0f;
    scanTick();
    const uint16_t s2 = g_lastBySlot[2].sequence;
    inputs[2].value = 60.0f;
    scanTick();
    const uint16_t s3 = g_lastBySlot[2].sequence;
    TEST_ASSERT_GREATER_THAN_UINT16(s1, s2);
    TEST_ASSERT_GREATER_THAN_UINT16(s2, s3);
}

static void test_disable_emits_one_clear_frame(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    inputs[2].flags.isEnabled = 0;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_LIVE_FLAG_CONFIGURED, g_lastBySlot[2].flags);
    resetCapture();
    scanTick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// The heartbeat re-sends the OCCUPIED slots, not the whole table (#337).
static void test_heartbeat_reemits_occupied_only(void) {
    configInput(2, 21, 42.0f, 0, true);
    configInput(5, 22, 7.0f, 0, true);
    scanTick();
    resetCapture();
    g_test_now += 5000;                              // kHeartbeatMs elapsed
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(2, g_frameCount);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, g_lastBySlot[2].value);
    TEST_ASSERT_EQUAL_INT(1, g_countBySlot[5]);
    TEST_ASSERT_EQUAL_INT(0, g_countBySlot[0]);      // empty slot stays quiet
}

// Backpressure: a failed send leaves the cache untouched, so the changed slot
// retries on the next scan.
static void test_backpressure_retries_next_scan(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    inputs[2].value = 55.0f;
    g_sendResult = false;
    scanTick();
    TEST_ASSERT_EQUAL_INT(0, g_countBySlot[2]);      // dropped by transport
    g_sendResult = true;
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_countBySlot[2]);      // retried
    TEST_ASSERT_EQUAL_FLOAT(55.0f, g_lastBySlot[2].value);
}

// PING resync: every OCCUPIED slot re-emits on the next scan (#337).
static void test_resync_reemits_occupied_only(void) {
    configInput(2, 21, 42.0f, 0, true);
    scanTick();
    resetCapture();
    LiveValueBroadcaster::resync();
    scanTick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_INT(1, g_countBySlot[2]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cold_start_payload_layout);
    RUN_TEST(test_empty_slot_payload_shape);
    RUN_TEST(test_cold_start_includes_disabled_slot);
    RUN_TEST(test_freed_slot_emits_one_clearing_frame);
    RUN_TEST(test_freed_slot_clear_repeats_then_stops);
    RUN_TEST(test_starved_heartbeats_do_not_consume_clear_window);
    RUN_TEST(test_disabled_then_freed_still_emits_clear);
    RUN_TEST(test_no_reemit_when_unchanged);
    RUN_TEST(test_scan_gate_holds_below_interval);
    RUN_TEST(test_change_emits_display_converted);
    RUN_TEST(test_nan_single_fire);
    RUN_TEST(test_inf_clears_valid);
    RUN_TEST(test_units_change_reemits);
    RUN_TEST(test_sequence_increments);
    RUN_TEST(test_disable_emits_one_clear_frame);
    RUN_TEST(test_heartbeat_reemits_occupied_only);
    RUN_TEST(test_backpressure_retries_next_scan);
    RUN_TEST(test_resync_reemits_occupied_only);
    return UNITY_END();
}
