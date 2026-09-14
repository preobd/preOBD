// Native unit test: LiveValueBroadcaster wire payload
// (src/outputs/ble/live_value_broadcaster.cpp).
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

#include "boot_order_fixture.h"  // inputsTableIsLiveForAssert() stub (#352, #369)
#include "../../src/outputs/ble/live_value_broadcaster.cpp"

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

// Frames this pass may still accept before the "TX buffer" refuses; -1 = no
// limit. Models the real 64-byte buffer, which fits exactly three 21-byte
// frames — the shape of #350.
static int g_sendBudget = -1;
// Every call, accepted or refused. Refused sends are invisible to g_frameCount,
// but they are exactly what a scan running at loop rate produces, so the
// gate-bypass tests count attempts.
static int g_sendAttempts = 0;

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    ++g_sendAttempts;
    if (g_sendBudget == 0) return false;
    if (g_sendBudget > 0) --g_sendBudget;
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
    g_sendAttempts = 0;
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
    g_sendBudget = -1;
    resetCapture();
    memset(inputs, 0, sizeof(inputs));
    for (int i = 0; i < MAX_INPUTS; ++i) inputs[i].pin = 0xFF;  // sparse-table sentinel
    setInputsTableLive(true);  // every test but the boot-order ones below
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


// Configure `n` inputs in slots 0..n-1 (defined below with the #350 tests).
static void configInputs(int n);

// --- #352: init() runs BEFORE the config load ---------------------------

// Simulate the real boot order (main.cpp: LiveValueBroadcaster::init() at 310,
// initInputManager() at 364). At init() time inputs[] is still BSS-zeroed, so
// every slot's pin is 0 — NOT the 0xFF sentinel — and the config load fills the
// table afterwards. Every other test in this file initialises the sentinel
// first, which is why this never showed up here.
static void bootAsFirmwareDoes(int configured) {
    memset(inputs, 0, sizeof(inputs));          // BSS: every pin == 0
    setInputsTableLive(false);                  // table not ground truth yet (#369)
    LiveValueBroadcaster::init();               // …before the table exists
    setInputsTableLive(true);                   // initInputManager() flips this
    for (int i = 0; i < MAX_INPUTS; ++i) inputs[i].pin = 0xFF;   // config load
    configInputs(configured);
    resetCapture();
}

// #352: slots that never held an input emitted a flags-0 "tile is gone" frame.
// init() snapshotted occupancy from the not-yet-loaded table, read every
// pin == 0 slot as OCCUPIED, and the first scan then saw MAX_INPUTS - n
// occupied→freed edges — arming the post-free grace window for every empty
// slot, which puts them in the heartbeat.
static void test_boot_order_never_clears_never_configured_slots(void) {
    bootAsFirmwareDoes(8);

    for (int hb = 0; hb < 12; ++hb) {           // ~1 minute of heartbeats
        g_test_now += 5000;
        LiveValueBroadcaster::tick();
    }
    for (int i = 8; i < MAX_INPUTS; ++i)
        TEST_ASSERT_EQUAL_INT(0, g_countBySlot[i]);   // never-configured: silent
    for (int i = 0; i < 8; ++i)
        TEST_ASSERT_GREATER_THAN_INT(0, g_countBySlot[i]);  // …the real ones still beat
}

// The bench sighting was a drip: with a 3-frame TX budget the phantom clears
// came out a few slots at a time, ascending, over minutes — which is why the
// report caught slots 31..39 back to back rather than the whole tail at once.
// Also covers the reconnect path: resync() re-invalidates whatever inHeartbeat()
// admits, so a wrongly-armed grace window survives a reconnect.
static void test_boot_order_phantom_clears_survive_backpressure_and_resync(void) {
    bootAsFirmwareDoes(8);

    for (int hb = 0; hb < 12; ++hb) {
        g_test_now += 5000;
        for (int pass = 0; pass < 20; ++pass) { // let each sweep drain at loop rate
            g_sendBudget = 3;                   // 64-byte TX buffer
            g_test_now += 1;
            LiveValueBroadcaster::tick();
        }
    }
    LiveValueBroadcaster::resync();             // a central reconnects
    for (int pass = 0; pass < 60; ++pass) { g_sendBudget = 3; g_test_now += 1; LiveValueBroadcaster::tick(); }
    g_sendBudget = -1;

    for (int i = 8; i < MAX_INPUTS; ++i)
        TEST_ASSERT_EQUAL_INT(0, g_countBySlot[i]);
}

// --- #350: the 500 ms scan gate vs. a parked heartbeat sweep -----------

// Configure `n` inputs in slots 0..n-1.
static void configInputs(int n) {
    for (int i = 0; i < n; ++i) configInput(i, 20 + i, 10.0f + i, 0, true);
}

// A sweep the transport parked mid-table must keep draining at LOOP rate, not
// at the 500 ms scan cadence. At 2 Hz and three frames per pass, a sweep of a
// realistic table would take longer than the 5 s heartbeat that started it —
// the next heartbeat would restart it from slot 0 and it would never reach the
// end, which is #350 rebuilt one cadence slower.
static void test_scan_gate_bypassed_while_sweep_drains(void) {
    configInputs(8);
    scanTick();                       // cold-start frames for the 8 inputs
    g_test_now += 5000;               // heartbeat due
    resetCapture();

    g_sendBudget = 3;                 // 64-byte buffer: three frames fit
    LiveValueBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(3, g_frameCount);

    // Loop-rate ticks, ALL well inside the 500 ms gate. The sweep must continue
    // through them; pre-fix the gate held it and nothing more arrived.
    for (int i = 0; i < 10 && g_frameCount < 8; ++i) {
        g_test_now += 1;              // ~1 kHz loop
        g_sendBudget = 3;
        LiveValueBroadcaster::tick();
    }
    TEST_ASSERT_EQUAL_INT(8, g_frameCount);       // whole table, inside 10 ms
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_INT(1, g_countBySlot[i]);
}

// Once the sweep completes, the gate goes back on: sub-500 ms ticks are silent
// again, so the bypass cannot become a permanent loop-rate scan.
static void test_scan_gate_reinstated_after_sweep_completes(void) {
    configInputs(8);
    scanTick();
    g_test_now += 5000;
    g_sendBudget = 3;
    LiveValueBroadcaster::tick();
    for (int i = 0; i < 10; ++i) { g_test_now += 1; g_sendBudget = 3; LiveValueBroadcaster::tick(); }

    resetCapture();
    g_sendBudget = -1;
    inputs[4].value = 999.0f;          // a real change, but the gate is closed
    for (int i = 0; i < 100; ++i) { g_test_now += 1; LiveValueBroadcaster::tick(); }
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
    TEST_ASSERT_EQUAL_INT(0, g_sendAttempts);     // the scan did not even run
}

// THE FINDING-1 GUARD. Framed mode is off by default, and with it off every
// sendFrame() is refused. A sweep that owes frames but never advances must not
// hold the gate open: keying the bypass on "owes frames" latched it true for
// the rest of the boot, so this MAX_INPUTS scan ran on EVERY loop iteration —
// on a Teensy 4 that deliberately never sleeps — instead of twice a second.
static void test_refusing_transport_does_not_pin_scan_at_loop_rate(void) {
    configInputs(8);
    scanTick();
    g_test_now += 5000;               // heartbeat due
    resetCapture();

    g_sendBudget = 0;                 // transport refuses everything, forever
    LiveValueBroadcaster::tick();     // opens the sweep, fails at the first slot
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);

    // 2000 loop iterations = 2 s of a 1 kHz loop. The gate allows four scans in
    // that window (2 Hz), each attempting one send before the refusal stops the
    // pass. Pre-fix the latched sweep bought a scan on every iteration: ~2000
    // attempts, plus 2000 sweeps of the MAX_INPUTS occupancy loop and packKey().
    resetCapture();
    for (int i = 0; i < 2000; ++i) { g_test_now += 1; LiveValueBroadcaster::tick(); }
    TEST_ASSERT_LESS_OR_EQUAL_INT(5, g_sendAttempts);
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
    RUN_TEST(test_boot_order_never_clears_never_configured_slots);
    RUN_TEST(test_boot_order_phantom_clears_survive_backpressure_and_resync);
    RUN_TEST(test_scan_gate_bypassed_while_sweep_drains);
    RUN_TEST(test_scan_gate_reinstated_after_sweep_completes);
    RUN_TEST(test_refusing_transport_does_not_pin_scan_at_loop_rate);
    return UNITY_END();
}
