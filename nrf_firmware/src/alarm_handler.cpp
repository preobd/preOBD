// alarm_handler.cpp -- Alarm Notify GATT plumbing.
//
// See alarm_handler.h for the contract; mirrors ota_handler.cpp's structure.

#include "alarm_handler.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "ble_gatt_defs.h"
#include "uuid_util.h"

namespace {

BLECharacteristic g_alarm;

} // namespace

void alarmHandlerInit() {
    // The Extended Service is owned by extended_service.cpp and must have
    // been .begin()-ed before this call so the characteristic attaches to
    // the right parent. See main.cpp for the ordering contract.
    static uint8_t kAlarmUuid[16];

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
