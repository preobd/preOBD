// live_handler.cpp -- Live Values GATT plumbing.
//
// See live_handler.h. Mirrors relay_handler.cpp's shape minus the Write
// callback (live values are host-originated only).

#include "live_handler.h"

#include <Arduino.h>
#include <string.h>

#include <bluefruit.h>

#include "ble_gatt_defs.h"
#include "ble_services.h"
#include "extended_service.h"
#include "uuid_util.h"

namespace {

BLECharacteristic g_live;

} // namespace

void liveHandlerInit() {
    // Extended Service is owned by extended_service.cpp; see main.cpp.
    static uint8_t kLiveUuid[16];

    if (parseUuid128(PREOBD_CHAR_LIVE_UUID, kLiveUuid)) {
        g_live = BLECharacteristic(kLiveUuid);
    }
    g_live.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    g_live.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_live.setFixedLen(PREOBD_LIVE_VALUE_SIZE);
    g_live.setCccdWriteCallback(extendedServiceOnCccdWrite);  // #435 resync
    g_live.begin();

    // Seed the cache with zeros so reads before the host's first push
    // return a well-defined payload.
    uint8_t zeros[PREOBD_LIVE_VALUE_SIZE] = {};
    g_live.write(zeros, sizeof(zeros));
}

bool liveHandlerPush(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_LIVE_VALUE_SIZE) return false;
    g_live.write(payload, len);
    bleServicesNotifyAll(g_live, payload, len);
    return true;
}
