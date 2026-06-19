// ble_services.h -- GATT service / characteristic registration.
//
// Phase 1 exposes only the Control Service (Tier 1):
//   CMD_TX  Write / Write Without Response  -- client to host MCU
//   CMD_RX  Notify / Read                   -- host MCU to client
//   Status  Read / Notify (32-byte struct)  -- pushed on change

#ifndef BLE_SERVICES_H
#define BLE_SERVICES_H

#include <stdint.h>
#include <stddef.h>

#include <bluefruit.h>

void bleServicesInit();

// Push UART bytes out as a CMD_RX notify. Returns true on success; returns
// false when the SoftDevice's HVN TX queue is full (caller must retry the
// same bytes on a later poll rather than discard them, or notifications
// will be silently dropped). No-op (returns true) when no client is
// connected. Caller guarantees `len` <= 20 (conservative BLE 4.2 payload).
bool bleServicesNotifyCmdRx(const uint8_t* data, size_t len);

// Update the System Status characteristic with a fresh 32-byte snapshot
// pushed by the host MCU via framed transport (type 0x03). Writes the cache
// (so subsequent GATT reads return the latest value) and pushes a notify
// to any subscribed centrals. Caller guarantees `len == PREOBD_STATUS_SIZE`.
// No-op (silent) when no client is connected — the write still updates the
// cache so a future connect's first read returns current data.
void bleServicesUpdateStatus(const uint8_t* payload, uint16_t len);

// Reference to the primary service object so main.cpp can hand it to the
// advertiser.
BLEService& bleServicesControlService();

#endif // BLE_SERVICES_H
