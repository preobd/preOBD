// alarm_handler.cpp -- Alarm Notify GATT plumbing.
//
// See alarm_handler.h for the contract; mirrors ota_handler.cpp's structure.

#include "alarm_handler.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "ble_gatt_defs.h"
#include "uuid_util.h"

namespace {

uint8_t g_zeroUuid[16] = {};

BLEService        g_extService(g_zeroUuid);
BLECharacteristic g_alarm;

} // namespace

void alarmHandlerInit() {
    // BLEUuid stores a pointer, not a copy -- each service/characteristic
    // needs its own persistent backing array. See ble_services.cpp.
    static uint8_t kExtServiceUuid[16];
    static uint8_t kAlarmUuid[16];

    if (parseUuid128(PREOBD_EXT_SERVICE_UUID, kExtServiceUuid)) {
        g_extService = BLEService(kExtServiceUuid);
    }
    g_extService.begin();

    if (parseUuid128(PREOBD_CHAR_ALARM_UUID, kAlarmUuid)) {
        g_alarm = BLECharacteristic(kAlarmUuid);
    }
    g_alarm.setProperties(CHR_PROPS_NOTIFY);
    g_alarm.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_alarm.setFixedLen(PREOBD_ALARM_EVENT_SIZE);
    g_alarm.begin();
}

bool alarmHandlerPush(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_ALARM_EVENT_SIZE) return false;
    if (!Bluefruit.connected()) return true;
    g_alarm.notify(payload, len);
    return true;
}
