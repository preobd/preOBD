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

// Drop any accumulated RX. Called on BLE disconnect so a stale tail doesn't
// leak into the next session.
void uartBridgeResetRx();

#endif // UART_BRIDGE_H
