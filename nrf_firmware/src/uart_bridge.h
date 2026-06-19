// uart_bridge.h -- UART <-> BLE byte plumbing.
//
// Phase 1 is transparent: bytes flow unmodified in both directions. The RX
// accumulator exists only to coalesce small reads into BLE-friendly notifies.

#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

void uartBridgeInit();

// Pump UART <-> BLE in both directions. Call from loop().
void uartBridgePoll();

// Enqueue a buffer for transmission to the host MCU UART. Safe to call from
// BLE callbacks -- bytes are pushed into a ring buffer and drained from
// uartBridgePoll(), so the caller never blocks on Serial1. Returns bytes
// accepted into the queue (0 if full).
size_t uartBridgeWrite(const uint8_t* data, size_t len);

// Encode `payload` as a framed packet of the given type and queue it for
// transmission to the host MCU. Atomic: either the whole frame fits in the
// TX ring (returns true) or nothing is enqueued (returns false). Caller
// must retry on false. Intended for BLE-originated binary writes (e.g.
// relay control) where partial-frame transmission would corrupt the wire.
bool uartBridgeSendFrame(uint8_t type, const uint8_t* payload, uint16_t len);

// Drop any accumulated RX. Called on BLE disconnect so a stale tail doesn't
// leak into the next session.
void uartBridgeResetRx();

// Reset the framed-mode decoder state owned by this module. tier_state
// calls this on connect (before sending the ping) and on disconnect so a
// partial frame can't leak across sessions. Lives here because uart_bridge
// is the sole producer that feeds bytes into the decoder.
void uartBridgeDecoderReset();

#endif // UART_BRIDGE_H
