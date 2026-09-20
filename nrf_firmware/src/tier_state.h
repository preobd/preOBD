// tier_state.h -- transparent / framed mode + auto-detection on the
// host-MCU UART link. (Historically called "Tier 1 / Tier 2.")
//
// State machine (ARCHITECTURE.md §3.5):
//   DISCONNECTED -> (BLE connect)    -> PINGING (emit 0xFF frame, 2s timer)
//   PINGING      -> (0xFF frame in)  -> FRAMED
//   PINGING      -> (timeout)        -> TRANSPARENT
//   TRANSPARENT|FRAMED -> (disconnect) -> DISCONNECTED
//
// While PINGING, behavior toward the host is intentionally inert (no text
// pumped to Serial1) so a transparent-only host doesn't see a stray 0xFF
// byte sequence and a framed-capable host either responds or we drop back.

#ifndef TIER_STATE_H
#define TIER_STATE_H

#include <stdint.h>

void tierStateInit();

// Called from the BLE connect/disconnect callbacks. With two-central
// concurrency these fire only on the 0<->1 connection edges (see
// ble_bridge.cpp): negotiation runs once on the *first* connect and framing
// is torn down only on the *last* disconnect, so a second central joining or
// leaving mid-session leaves the established tier untouched. A joining
// central still gets the host's state via tierStateRequestResync().
void tierStateOnConnect();
void tierStateOnDisconnect();

// Notify on a decoded ping frame received from the host (type 0xFF).
void tierStateOnPingFrame();

// Ask for a host resync outside the negotiation window (#435). Called from
// the Extended Service CCCD callbacks (BLE event task) whenever a central
// subscribes to Alarm / Action / Live Values: the host treats every inbound
// 0xFF as a (re)connection edge and re-emits its held alarm slots and full
// action / live-value tables (#430). Only meaningful once FRAMED; the
// request is dropped if the link resolves TRANSPARENT. Bursts (one client
// subscribing to all three characteristics) are coalesced by a short
// debounce so the host sees one PING, not three. The PING itself goes out
// from tierStatePoll() (loop task) through the TX ring, never from the
// callback.
void tierStateRequestResync();

// Poll from loop(). Drives the 2s pinging timeout.
void tierStatePoll();

// True once auto-detect has resolved into TIER2.
bool tierStateIsFramed();

// True while we're in the PINGING window. uart_bridge / ble_services use
// this to gate text traffic until the tier is known.
bool tierStateIsPinging();

#endif // TIER_STATE_H
