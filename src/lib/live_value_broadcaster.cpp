#include "live_value_broadcaster.h"

#include "../config.h"

#if ENABLE_FRAMED_TRANSPORT

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

FLASHMEM LiveKey packKey(int i) {
    LiveKey k;
    memset(&k, 0, sizeof(k));
    const Input& in = inputs[i];
    // Occupancy: freed/never-allocated slots carry pin == 0xFF (the sparse
    // inputs[] table sentinel — do NOT bound by numActiveInputs, a count).
    // Empty and disabled slots both key to all-zero (flags = 0).
    if (in.pin == 0xFF || !in.flags.isEnabled) return k;
    k.flags      = PREOBD_LIVE_FLAG_ENABLED;
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
    g_bc.tick(now, kHeartbeatMs, packKey, sameKey, sendSlot);
}

void init() {
    // Cold start: nothing cached, so the first tick emits every slot once.
    g_bc.reset();
    g_lastScanMs = 0;
}

void tick() {
    const uint32_t now = millis();
    if (now - g_lastScanMs < kScanIntervalMs) return;
    g_lastScanMs = now;
    scanOnce(now);
}

FLASHMEM void resync() {
    g_bc.invalidateAll();
}

} // namespace LiveValueBroadcaster

#else // !ENABLE_FRAMED_TRANSPORT

namespace LiveValueBroadcaster {
void init() {}
void tick() {}
void resync() {}
}

#endif // ENABLE_FRAMED_TRANSPORT
