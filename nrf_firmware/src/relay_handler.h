// relay_handler.h -- Relay Control characteristic on the Extended Service.
//
// Bidirectional:
//   host → BLE: 16-byte PreobdRelayState frames (framed type 0x05) arrive
//     via uart_bridge; cached into the GATT char and pushed as notifies.
//   BLE → host: GATT writes from the client are re-wrapped as framed type
//     0x05 packets and queued to the host via uartBridgeSendFrame().
//
// Follows the same Bluefruit registration shape as alarm_handler /
// ota_handler.

#ifndef RELAY_HANDLER_H
#define RELAY_HANDLER_H

#include <stdint.h>

void relayHandlerInit();

// Push a 16-byte PreobdRelayState as a GATT notify on the Relay
// characteristic and refresh the cached read value. Returns false on size
// mismatch.
bool relayHandlerPush(const uint8_t* payload, uint16_t len);

#endif // RELAY_HANDLER_H
