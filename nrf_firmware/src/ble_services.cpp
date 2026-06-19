// ble_services.cpp -- Control Service registration + System Status snapshot.

#include "ble_services.h"

#include <Arduino.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "uart_bridge.h"
#include "uuid_util.h"

namespace {

// BLEService(uint8_t*) reads 16 bytes for the 128-bit UUID. At global-init
// time we don't have the parsed UUID yet, so seed with a zero buffer and
// overwrite via copy-assignment in bleServicesInit().
uint8_t g_zeroUuid[16] = {};

BLEService    g_controlService(g_zeroUuid);
BLECharacteristic g_cmdTx;
BLECharacteristic g_cmdRx;
BLECharacteristic g_status;

void cmdTxWriteCallback(uint16_t /*conn_handle*/, BLECharacteristic* chr,
                        uint8_t* data, uint16_t len) {
    (void)chr;
    if (data && len > 0) {
        uartBridgeWrite(data, len);
    }
}

} // namespace

void bleServicesInit() {
    // Bluefruit's BLEUuid stores a pointer to the 16-byte UUID buffer, not a
    // copy (see BLEUuid.h: `uint8_t const* _uuid128`). Each service /
    // characteristic needs its own persistent backing array -- a single
    // stack-local buffer reused across calls leaves dangling pointers once
    // the function returns, and advertising emits garbage UUIDs.
    static uint8_t kServiceUuid[16];
    static uint8_t kCmdTxUuid[16];
    static uint8_t kCmdRxUuid[16];
    static uint8_t kStatusUuid[16];

    if (parseUuid128(PREOBD_SERVICE_UUID, kServiceUuid)) {
        g_controlService = BLEService(kServiceUuid);
    }
    g_controlService.begin();

    if (parseUuid128(PREOBD_CHAR_CMD_TX_UUID, kCmdTxUuid)) {
        g_cmdTx = BLECharacteristic(kCmdTxUuid);
    }
    g_cmdTx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    g_cmdTx.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_cmdTx.setMaxLen(244);
    g_cmdTx.setWriteCallback(cmdTxWriteCallback);
    g_cmdTx.begin();

    if (parseUuid128(PREOBD_CHAR_CMD_RX_UUID, kCmdRxUuid)) {
        g_cmdRx = BLECharacteristic(kCmdRxUuid);
    }
    g_cmdRx.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    g_cmdRx.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_cmdRx.setMaxLen(244);
    g_cmdRx.begin();

    if (parseUuid128(PREOBD_CHAR_STATUS_UUID, kStatusUuid)) {
        g_status = BLECharacteristic(kStatusUuid);
    }
    g_status.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    g_status.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_status.setFixedLen(PREOBD_STATUS_SIZE);
    g_status.begin();

    // Initialize the cache to 32 zero bytes so reads before the host's first
    // push return a well-defined payload. Clients detect "no data yet" via
    // magic != PREOBD_STATUS_MAGIC.
    uint8_t zeros[PREOBD_STATUS_SIZE] = {};
    g_status.write(zeros, sizeof(zeros));
}

bool bleServicesNotifyCmdRx(const uint8_t* data, size_t len) {
    if (!Bluefruit.connected()) return true;
    return g_cmdRx.notify(data, len);
}

void bleServicesUpdateStatus(const uint8_t* payload, uint16_t len) {
    if (len != PREOBD_STATUS_SIZE) return;
    g_status.write(payload, len);
    if (Bluefruit.connected()) {
        g_status.notify(payload, len);
    }
}

BLEService& bleServicesControlService() {
    return g_controlService;
}
