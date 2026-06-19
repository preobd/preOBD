// ble_bridge.h -- BLE connection lifecycle + NRF_IRQ signaling to host MCU.

#ifndef BLE_BRIDGE_H
#define BLE_BRIDGE_H

#include <stdbool.h>

void bleBridgeInit();

bool bleBridgeConnected();

#endif // BLE_BRIDGE_H
