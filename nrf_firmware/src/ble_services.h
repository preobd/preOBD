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

// Push UART bytes out as a CMD_RX notify. Caller guarantees `len` <= 20
// (the conservative BLE 4.2 payload). No-op when no client is connected.
void bleServicesNotifyCmdRx(const uint8_t* data, size_t len);

// Update the System Status characteristic and notify subscribers.
// Phase 1 just publishes a static snapshot at boot; future phases will
// repopulate this from host-MCU state.
void bleServicesPublishStatus();

// Reference to the primary service object so main.cpp can hand it to the
// advertiser.
BLEService& bleServicesControlService();

#endif // BLE_SERVICES_H
