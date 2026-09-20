// alarm_handler.cpp -- Alarm Notify GATT plumbing.
//
// See alarm_handler.h for the contract; mirrors ota_handler.cpp's structure.

#include "alarm_handler.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "ble_services.h"
#include "extended_service.h"
#include "uuid_util.h"

namespace {

BLECharacteristic g_alarm;

// Latest event + retry state. Alarm notifies are edge-triggered (the host
// only re-pushes on the next state transition), so unlike Status/Relay a
// dropped notify here is genuinely missed by that central. We keep the most
// recent event and retry until every subscribed central accepts it; a newer
// event supersedes a still-pending one -- the latest event is the only one
// worth delivering.
uint8_t g_event[PREOBD_ALARM_EVENT_SIZE];
uint8_t g_doneMask = 0;
bool    g_pending  = false;

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
    g_alarm.setCccdWriteCallback(extendedServiceOnCccdWrite);  // #435 resync
    g_alarm.begin();
}

bool alarmHandlerPush(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_ALARM_EVENT_SIZE) return false;
    memcpy(g_event, payload, PREOBD_ALARM_EVENT_SIZE);
    g_doneMask = 0;
    g_pending  = true;
    alarmHandlerPoll();   // first attempt immediately; leftovers retry in loop()
    return true;
}

void alarmHandlerPoll() {
    if (!g_pending) return;
    if (bleServicesNotifyRetry(g_alarm, g_event, PREOBD_ALARM_EVENT_SIZE,
                               g_doneMask)) {
        g_pending = false;
    }
}
