// alarm_handler.h -- Alarm Notify characteristic on the Extended Service.
//
// Receives 18-byte PreobdAlarmEvent payloads via framed transport type 0x04
// (dispatched from uart_bridge.cpp) and pushes them out as GATT notifies on
// PREOBD_CHAR_ALARM_UUID (`...0030`). Notify-only -- the characteristic is
// not readable (alarm events are stateless transitions, not cached state).
//
// Follows the same Bluefruit registration shape as ota_handler.{cpp,h}.
// Like that module, this declares its own BLEService instance with the
// Extended Service UUID; consolidating into a shared extended_service
// module is a future refactor.

#ifndef ALARM_HANDLER_H
#define ALARM_HANDLER_H

#include <stdint.h>

void alarmHandlerInit();

// Push an 18-byte PreobdAlarmEvent out as a GATT notify on the Alarm
// characteristic. Returns false if `len` is wrong; otherwise returns true
// (best-effort -- a dropped notify is acceptable since the next state
// transition emits a fresh event with the latest sequence).
bool alarmHandlerPush(const uint8_t* payload, uint16_t len);

#endif // ALARM_HANDLER_H
