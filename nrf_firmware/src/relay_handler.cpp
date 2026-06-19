// relay_handler.cpp -- Relay Control GATT plumbing.
//
// See relay_handler.h. Mirrors alarm_handler.cpp's shape plus a Write
// callback that re-wraps client writes as framed type 0x05 packets and
// queues them for the host MCU.

#include "relay_handler.h"

#include <Arduino.h>
#include <string.h>

#include <bluefruit.h>

#include "ble_gatt_defs.h"
#include "ble_services.h"
#include "frame_codec.h"
#include "uart_bridge.h"
#include "uuid_util.h"

namespace {

BLECharacteristic g_relay;

void relayWriteCallback(uint16_t /*conn*/, BLECharacteristic* /*chr*/,
                        uint8_t* data, uint16_t len) {
    if (!data || len != PREOBD_RELAY_STATE_SIZE) return;
    // Re-wrap as a framed type 0x05 packet to the host. Best-effort -- a
    // dropped enqueue here means the host didn't get the write; the client
    // will see no confirmation notify and can retry.
    uartBridgeSendFrame(FRAME_TYPE_RELAY, data, len);
}

} // namespace

void relayHandlerInit() {
    // Extended Service is owned by extended_service.cpp; see main.cpp.
    static uint8_t kRelayUuid[16];

    if (parseUuid128(PREOBD_CHAR_RELAY_UUID, kRelayUuid)) {
        g_relay = BLECharacteristic(kRelayUuid);
    }
    g_relay.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    g_relay.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_relay.setFixedLen(PREOBD_RELAY_STATE_SIZE);
    g_relay.setWriteCallback(relayWriteCallback);
    g_relay.begin();

    // Seed the cache with zeros so reads before the host's first push
    // return a well-defined payload.
    uint8_t zeros[PREOBD_RELAY_STATE_SIZE] = {};
    g_relay.write(zeros, sizeof(zeros));
}

bool relayHandlerPush(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_RELAY_STATE_SIZE) return false;
    g_relay.write(payload, len);
    bleServicesNotifyAll(g_relay, payload, len);
    return true;
}
