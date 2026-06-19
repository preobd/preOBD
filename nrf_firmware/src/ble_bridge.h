// ble_bridge.h -- BLE connection lifecycle + NRF_IRQ signaling to host MCU.

#ifndef BLE_BRIDGE_H
#define BLE_BRIDGE_H

#include <stdbool.h>

void bleBridgeInit();

// Poll from loop(): re-converges the IRQ line, tier state, and advertising to
// Bluefruit's authoritative peripheral connection count, so they self-heal if a
// connect/disconnect event is ever missed (which otherwise left the IRQ/LED
// stuck HIGH with nothing connected).
void bleBridgePoll();

bool bleBridgeConnected();

#endif // BLE_BRIDGE_H
