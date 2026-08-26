// live_handler.h -- Live Values characteristic on the Extended Service.
//
// Host → BLE only: 16-byte PreobdLiveValue frames (framed type 0x06) arrive
// via uart_bridge; cached into the GATT char (Read returns the most recent
// frame) and pushed as notifies. Best-effort — a dropped notify is refreshed
// by the host's 5s heartbeat, so there is no alarm-style retry queue.
//
// Follows the same Bluefruit registration shape as relay_handler.

#ifndef LIVE_HANDLER_H
#define LIVE_HANDLER_H

#include <stdint.h>

void liveHandlerInit();

// Push a 16-byte PreobdLiveValue as a GATT notify on the Live Values
// characteristic and refresh the cached read value. Returns false on size
// mismatch.
bool liveHandlerPush(const uint8_t* payload, uint16_t len);

#endif // LIVE_HANDLER_H
