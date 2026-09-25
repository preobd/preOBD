// status_compose.h -- Joins the host's status extension (frame 0x07) onto the
// next System Status snapshot (frame 0x03) for the GATT characteristic.
//
// The host sends 0x07 immediately before every 0x03 (#455). The extension
// is held ONE-SHOT: the next 0x03 publishes 32 + 8 bytes and consumes it, and a
// 0x03 with nothing held publishes the plain 32 bytes. So a host that stops
// sending 0x07 (an older build flashed while this nRF stays up) degrades the
// characteristic to 32 B on its very next snapshot instead of pinning a stale
// extension, and no version negotiation is needed in either direction.
//
// Pure logic, no BLE -- natively tested (test/test_status_compose). Sizes are
// restated here so this header needs no ble_gatt_defs.h; uart_bridge.cpp
// static_asserts they match.

#ifndef STATUS_COMPOSE_H
#define STATUS_COMPOSE_H

#include <stdint.h>

//
// The nRF is a dumb pipe for the extension: any length from the current 8 B up
// to kStatusComposeExtMaxSize passes through unread, so a host that APPENDS
// fields (ble_gatt_defs.h: append-only within an extLayout) needs no nRF
// update. Only outgrowing the max would.

constexpr uint16_t kStatusComposeBaseSize   = 32;   // PREOBD_STATUS_SIZE
constexpr uint16_t kStatusComposeExtMinSize = 8;    // PREOBD_STATUS_EXT_SIZE (layout 1)
constexpr uint16_t kStatusComposeExtMaxSize = 32;   // headroom for appended fields
constexpr uint16_t kStatusComposeMaxSize    = kStatusComposeBaseSize + kStatusComposeExtMaxSize;

struct StatusComposer {
    uint8_t  ext[kStatusComposeExtMaxSize];
    uint16_t extLen;    // 0 = nothing held
};

void statusComposeInit(StatusComposer& c);

// Frame 0x07 arrived. A payload outside [ExtMin, ExtMax] is dropped and also
// discards any held extension (never publish a half-trusted one).
void statusComposeOnExt(StatusComposer& c, const uint8_t* payload, uint16_t len);

// Frame 0x03 arrived. Writes the characteristic value to `out` (room for
// kStatusComposeMaxSize) and returns its length: 32 + the held extension's
// length (40 today), 32 with none held, 0 for a wrong-sized snapshot (drop it).
// Always consumes the extension.
uint16_t statusComposeOnStatus(StatusComposer& c, const uint8_t* payload,
                               uint16_t len, uint8_t* out);

#endif // STATUS_COMPOSE_H
