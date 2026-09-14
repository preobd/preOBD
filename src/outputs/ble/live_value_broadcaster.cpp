#include "live_value_broadcaster.h"

#include "../../config.h"

#if INCLUDE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "../../comms/ble_gatt_defs.h"
#include "../../comms/frame_codec.h"
#include "../../comms/transport_mux.h"
#include "slotted_broadcaster.h"
#include "../../sensors/sensor_library.h"   // convertFromBaseUnits()
#include "../../inputs/input.h"
#include "../../inputs/input_manager.h"

extern MuxTransport hwSerial1;

// Keep this TU's code out of Teensy ITCM (RAM1) — it's a 500 ms scan, not a
// hot path, and RAM1 code space is the scarce resource (32 KB block padding).
// No-op on platforms without the macro (AVR, ESP32).
#ifndef FLASHMEM
#define FLASHMEM
#endif

namespace LiveValueBroadcaster {

namespace {

// Scan gate: sensors update every loop (~20 Hz+); without it, change
// detection would emit a frame per loop for every live sensor. 500 ms gives
// ~2 Hz per changing input — smooth for a gauge tile, trivial for the UART
// (worst case MAX_INPUTS changing: ~1.7 KB/s of the 11.5 KB/s link).
constexpr uint32_t kScanIntervalMs = 500;

// Re-send every slot this often regardless of change, so a mid-session
// subscriber catches up (PING resync only fires on the 0→1 connection edge).
constexpr uint32_t kHeartbeatMs = 5000;

// Change key = what a client renders from the frame. memset-zeroed and
// memcmp-compared (simple whole-key equality; the explicit pad keeps the
// compare well-defined). A non-finite reading never enters the key — packKey
// zeroes the value and clears VALID — so a faulted sensor emits exactly one
// !VALID transition rather than re-firing per scan.
struct LiveKey {
    float   value;
    uint8_t flags;
    uint8_t unitsIndex;
    uint8_t pad[2];
};
inline bool sameKey(const LiveKey& a, const LiveKey& b) {
    return memcmp(&a, &b, sizeof(LiveKey)) == 0;
}

SlottedBroadcaster<LiveKey, MAX_INPUTS> g_bc;
uint32_t g_lastScanMs = 0;

// Heartbeat membership and the post-free grace window that keeps a freed slot
// in it for a bounded number of ACCEPTED frames (#337, #349, #352) are the
// template's job — see slotted_broadcaster.h. All this TU owes it is
// slotOccupied().

FLASHMEM LiveKey packKey(int i) {
    ASSERT_INPUTS_TABLE_LIVE();  // inputs[] isn't ground truth until here (#369)
    LiveKey k;
    memset(&k, 0, sizeof(k));
    const Input& in = inputs[i];
    // Occupancy: freed/never-allocated slots carry pin == 0xFF (the sparse
    // inputs[] table sentinel — do NOT bound by numActiveInputs, a count).
    if (in.pin == 0xFF) return k;   // empty slot: all-zero key, flags = 0
    // CONFIGURED distinguishes a disabled input from a slot with nothing in it —
    // both of which are otherwise flags-0 — on the wire AND in the change key, so
    // a disabled slot does not inherit an empty slot's silence at cold start and
    // occupied→freed stays a detectable transition even for an input that was
    // already disabled when it was cleared (#337).
    k.flags = PREOBD_LIVE_FLAG_CONFIGURED;
    if (!in.flags.isEnabled) return k;
    k.flags     |= PREOBD_LIVE_FLAG_ENABLED;
    k.unitsIndex = in.unitsIndex;
    // isfinite, not !isnan: an Inf (e.g. conversion overflow) must not ship
    // as a VALID gauge reading any more than a NAN fault would.
    if (isfinite(in.value)) {
        k.flags |= PREOBD_LIVE_FLAG_VALID;
        k.value  = convertFromBaseUnits(in.value, in.unitsIndex);
    }
    return k;
}

} // namespace

// Occupancy, the sentinel packKey() uses. A DISABLED-but-configured input is
// deliberately NOT excluded — the client has a tile for it (it is in
// SYSTEM DUMP JSON) and a late subscriber must learn it is disabled rather than
// see nothing at all.
FLASHMEM bool slotOccupied(int i) {
    ASSERT_INPUTS_TABLE_LIVE();  // inputs[] isn't ground truth until here (#369)
    return inputs[i].pin != 0xFF;
}

FLASHMEM bool sendSlot(int i, uint16_t seq) {
    const LiveKey k = packKey(i);
    PreobdLiveValue v;
    memset(&v, 0, sizeof(v));
    v.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    v.inputIndex      = static_cast<uint8_t>(i);
    v.flags           = k.flags;
    v.unitsIndex      = k.unitsIndex;
    v.sequence        = seq;
    v.value           = k.value;
    v.timestampMs     = millis();
    return hwSerial1.sendFrame(FRAME_TYPE_LIVE,
                               reinterpret_cast<const uint8_t*>(&v),
                               sizeof(v));
}

// The scan pass (incl. the SlottedBroadcaster::tick instantiation, which
// inlines here) lives in flash; only the cheap gate in tick() runs per loop.
FLASHMEM void scanOnce(uint32_t now) {
    // The broadcaster arms the post-free grace window on the occupied→freed
    // edge and spends it per clearing frame the transport ACCEPTED, so a starved
    // or refused heartbeat costs the slot nothing (#349).
    g_bc.tick(now, kHeartbeatMs, packKey, sameKey, sendSlot, slotOccupied);
}

void init() {
    // Cold start: seed every slot with the EMPTY key rather than reset()ing to
    // "nothing cached". A configured slot's key differs from it, so it still
    // emits once on the first tick; an unoccupied slot does not, which is the
    // catch-up burst #337 is about — it used to scale with MAX_INPUTS (40 on
    // teensy41) instead of the configured input count. A slot freed LATER still
    // pushes one frame, because its cached key is then the occupied one.
    LiveKey empty;
    memset(&empty, 0, sizeof(empty));
    // prime() also seeds occupancy as "nothing occupied" and the grace window
    // closed — which matters here because init() runs BEFORE the config load
    // (main.cpp brings the broadcasters up next to the transports;
    // initInputManager() comes later), so inputs[] is still BSS-zeroed and every
    // slot reads pin == 0, indistinguishable from a real input on digital pin 0.
    // Snapshotting that was #352; see slotted_broadcaster.h.
    g_bc.prime(empty);
    g_lastScanMs = 0;
}

void tick() {
    const uint32_t now = millis();
    // The 500 ms scan gate paces CHANGE detection; it must not also pace a
    // heartbeat sweep the transport parked mid-table (#350). At 2 Hz and the
    // ~3 frames a 64-byte TX buffer accepts per pass, a MAX_INPUTS sweep would
    // take longer than kHeartbeatMs itself — the next heartbeat would restart
    // it from slot 0 before it ever reached the end, reproducing the very
    // starvation the resumable sweep exists to fix. While a sweep is DRAINING
    // we run at loop rate instead, so it finishes as fast as the UART allows.
    //
    // sweepDraining(), never sweepPending(): a sweep that owes frames but is
    // making no progress must not hold the gate open. Framed mode is off by
    // default (system_config.cpp:248), and with it off every sendFrame() is
    // refused — so a board with any input configured would open a sweep at the
    // first heartbeat, fail at the first owed slot, and keep this MAX_INPUTS
    // scan running every loop iteration for the rest of the boot.
    if (!g_bc.sweepDraining() && now - g_lastScanMs < kScanIntervalMs) return;
    g_lastScanMs = now;
    scanOnce(now);
}

FLASHMEM void resync() {
    // Occupied slots only, for the same reason the heartbeat skips the rest: a
    // resyncing client is rebuilding its tiles from scratch, and a slot with no
    // input has no tile to build (#337). Empty slots keep their cached empty
    // key, so a later occupied→freed transition still emits.
    g_bc.invalidateActive(slotOccupied);
}

} // namespace LiveValueBroadcaster

#else // !INCLUDE_FRAMED_TRANSPORT

namespace LiveValueBroadcaster {
void init() {}
void tick() {}
void resync() {}
}

#endif // INCLUDE_FRAMED_TRANSPORT
