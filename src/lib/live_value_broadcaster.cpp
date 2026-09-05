#include "live_value_broadcaster.h"

#include "../config.h"

#if INCLUDE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "transport_mux.h"
#include "slotted_broadcaster.h"
#include "sensor_library.h"   // convertFromBaseUnits()
#include "../inputs/input.h"
#include "../inputs/input_manager.h"

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

// How many heartbeats a slot keeps taking part in AFTER it is freed. The
// clearing frame is edge-triggered, and this transport has no end-to-end ACK:
// sendFrame() reports only LOCAL flow refusal, so a frame dropped further
// downstream (the nRF's per-destination stash, a notify a central does not
// accept) is invisible here. Before #337 the heartbeat re-sent the flags-0
// frame forever and a lost clear healed on its own — live values are the
// self-healing-heartbeat tier, not the retry-until-accepted tier alarms are in.
// Excluding a freed slot the instant it frees would have made that one frame
// the only chance, and a client that missed it would show a stale tile
// permanently, across reconnects. Repeating it over a bounded window restores
// the healing without restoring the per-MAX_INPUTS sweep.
constexpr uint8_t kClearHeartbeats = 3;

// The window is spent per clearing frame the transport ACCEPTS (see sendSlot),
// not per heartbeat edge, so it survives a busy link: the edge-triggered push
// costs one, leaving kClearHeartbeats heartbeat repeats. Counting edges instead
// let refused sends burn the window down to nothing — bench-confirmed on
// teensy40, where the repeats never arrived and a client disconnected across a
// CLEAR kept its tile for good.
constexpr uint8_t kClearFramesTotal = kClearHeartbeats + 1;

bool    g_wasOccupied[MAX_INPUTS];
uint8_t g_clearGrace[MAX_INPUTS];

FLASHMEM LiveKey packKey(int i) {
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
FLASHMEM bool slotOccupied(int i) { return inputs[i].pin != 0xFF; }

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
    const bool sent = hwSerial1.sendFrame(FRAME_TYPE_LIVE,
                                          reinterpret_cast<const uint8_t*>(&v),
                                          sizeof(v));
    // Spend the grace window on clearing frames the transport ACCEPTED, never on
    // heartbeat edges. sendFrame() refuses while the TX buffer is full, and
    // counting edges burned the whole window on refused sends — on a link busy
    // enough to starve the heartbeat the window expired having delivered nothing,
    // which is exactly the self-healing it exists to provide. Bench-confirmed on
    // teensy40: the freed slot's clear went out once as a change push and the
    // three repeats never arrived, so a client that missed it (or was
    // disconnected across the CLEAR) kept a stale tile permanently.
    if (sent && !slotOccupied(i) && g_clearGrace[i] > 0) --g_clearGrace[i];
    return sent;
}

// Heartbeat membership (#337): the occupied slots, plus any slot still inside
// its post-free grace window so the tile-clearing frame gets more than one
// chance. Change detection never consults this — a slot it excludes still emits
// the moment its key changes — so nothing here can make a transition invisible.
FLASHMEM bool inHeartbeat(int i) { return slotOccupied(i) || g_clearGrace[i] > 0; }

// The scan pass (incl. the SlottedBroadcaster::tick instantiation, which
// inlines here) lives in flash; only the cheap gate in tick() runs per loop.
FLASHMEM void scanOnce(uint32_t now) {
    // Arm the grace window on the occupied→freed edge, before the tick that
    // emits the clearing frame.
    for (int i = 0; i < MAX_INPUTS; ++i) {
        const bool occ = slotOccupied(i);
        if (!occ && g_wasOccupied[i]) g_clearGrace[i] = kClearFramesTotal;
        g_wasOccupied[i] = occ;
    }

    // sendSlot() spends the grace window itself, per clearing frame the transport
    // accepted, so a starved or refused heartbeat costs the slot nothing.
    g_bc.tick(now, kHeartbeatMs, packKey, sameKey, sendSlot, inHeartbeat);
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
    g_bc.prime(empty);
    g_lastScanMs = 0;
    memset(g_clearGrace, 0, sizeof(g_clearGrace));
    // Seed occupancy as "nothing occupied" rather than from inputs[]: init()
    // runs BEFORE the config load (main.cpp brings the broadcasters up next to
    // the transports; initInputManager() comes later), so the table is still
    // BSS-zeroed here and every slot reads pin == 0 — which slotOccupied()
    // cannot tell from a real input on digital pin 0. Snapshotting that made the
    // first scan see MAX_INPUTS - n occupied→freed edges and arm the post-free
    // grace window for every slot that had never held an input, putting them all
    // in the heartbeat: each then emitted the flags-0 "tile is gone" frame,
    // dripping out a few slots at a time as the sweep drained (#352).
    //
    // Seeding empty is safe in both orders. The first scan records real
    // occupancy and can only arm on a LATER free, and a slot that is occupied
    // at this point has nothing to clear anyway — no client can hold a tile for
    // it before the first loop() pass.
    memset(g_wasOccupied, 0, sizeof(g_wasOccupied));
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
    for (int i = 0; i < MAX_INPUTS; ++i) {
        if (inHeartbeat(i)) g_bc.invalidate(i);
    }
}

} // namespace LiveValueBroadcaster

#else // !INCLUDE_FRAMED_TRANSPORT

namespace LiveValueBroadcaster {
void init() {}
void tick() {}
void resync() {}
}

#endif // INCLUDE_FRAMED_TRANSPORT
