// tier_state.h -- Tier 1 / Tier 2 mode + auto-detection.
//
// State machine (ARCHITECTURE.md §3.5):
//   DISCONNECTED -> (BLE connect)    -> PINGING (emit 0xFF frame, 2s timer)
//   PINGING      -> (0xFF frame in)  -> TIER2
//   PINGING      -> (timeout)        -> TIER1
//   TIER1|TIER2  -> (BLE disconnect) -> DISCONNECTED
//
// While PINGING, behavior toward the host is intentionally inert (no text
// pumped to Serial1) so a Tier 1 host doesn't see a stray 0xFF byte sequence
// and an active Tier 2 host either responds or we drop back.

#ifndef TIER_STATE_H
#define TIER_STATE_H

#include <stdint.h>

void tierStateInit();

// Called from the BLE connect/disconnect callbacks.
void tierStateOnConnect();
void tierStateOnDisconnect();

// Notify on a decoded ping frame received from the host (type 0xFF).
void tierStateOnPingFrame();

// Poll from loop(). Drives the 2s pinging timeout.
void tierStatePoll();

// True once auto-detect has resolved into TIER2.
bool tierStateIsFramed();

// True while we're in the PINGING window. uart_bridge / ble_services use
// this to gate text traffic until the tier is known.
bool tierStateIsPinging();

#endif // TIER_STATE_H
