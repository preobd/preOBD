// relay_broadcaster.h -- Pushes 16-byte PreobdRelayState frames over the
// framed transport (type 0x05) whenever a relay's mode/state/threshold
// changes; receives inbound type 0x05 frames from the webapp (via the nRF)
// and applies the requested mode/thresholds via the existing relay config
// API.
//
// Design notes:
//   - tick() walks systemConfig.relays[0..MAX_RELAYS) every loop iteration.
//     For each configured relay, packs the current state into a
//     PreobdRelayState, compares against a per-relay cache, and emits a
//     frame on any field change. Send-failure leaves the cache untouched so
//     the next tick retries automatically.
//   - handleInbound(payload, len) is called by MuxTransport when a type 0x05
//     frame arrives from a client. Honors the writable fields (mode,
//     thresholdOn, thresholdOff); other fields are ignored. Invalidates the
//     per-relay cache so tick() emits a fresh confirmation notify.
//   - The broadcaster targets hwSerial1 directly.
//   - Entire module compiles to nothing when ENABLE_RELAY_OUTPUT is 0.

#ifndef RELAY_BROADCASTER_H
#define RELAY_BROADCASTER_H

#include <stdint.h>

namespace RelayBroadcaster {

void init();
void tick();

// Called by MuxTransport::handleFrame() when a type 0x05 frame arrives.
// Validates the payload and applies writable fields to the targeted relay.
void handleInbound(const uint8_t* payload, uint16_t len);

} // namespace RelayBroadcaster

#endif // RELAY_BROADCASTER_H
