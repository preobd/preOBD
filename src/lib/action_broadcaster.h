// action_broadcaster.h -- Pushes 16-byte PreobdActionState frames over the
// framed transport (type 0x05) whenever a control-engine Action's state/config
// changes; receives inbound type 0x05 frames from the webapp (via the nRF) and
// applies a manual-toggle command.
//
// Supersedes RelayBroadcaster: a relay is now an Action of type RELAY (§11), so
// the …0031 characteristic / frame 0x05 payload carries the real Action model
// (id, type, target, triggering rule, on/off, failsafe) instead of the
// relay-shaped PreobdRelayState.
//
// Design notes (mirrors the proven RelayBroadcaster):
//   - tick() walks systemConfig.actions[0..MAX_ACTIONS) every loop iteration,
//     packs each slot into a PreobdActionState (empty slots carry
//     actionId == PREOBD_ACTION_ID_NONE), compares against a per-slot cache,
//     and emits a frame on any field change. A 5s heartbeat re-sends all slots
//     so a late subscriber gets the full set. Send-failure leaves the cache
//     untouched so the next tick retries.
//   - handleInbound(payload, len) honors the writable `command` field
//     (PREOBD_ACTION_CMD_MANUAL_ON/OFF) for the targeted actionId; manual
//     toggle only takes effect on manual (ruleRef == NONE) Actions.
//   - Compiles to empty stubs unless ENABLE_CONTROL_ENGINE and
//     ENABLE_FRAMED_TRANSPORT are both set.

#ifndef ACTION_BROADCASTER_H
#define ACTION_BROADCASTER_H

#include <stdint.h>

namespace ActionBroadcaster {

void init();
void tick();

// Called by MuxTransport::handleFrame() when a type 0x05 frame arrives.
void handleInbound(const uint8_t* payload, uint16_t len);

// Called on (re)connect (FRAME_TYPE_PING) to force a full re-broadcast next tick.
void resync();

} // namespace ActionBroadcaster

#endif // ACTION_BROADCASTER_H
