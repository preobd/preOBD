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

PreobdSystemStatus buildStatusSnapshot() {
    PreobdSystemStatus s = {};
    s.magic = PREOBD_STATUS_MAGIC;
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    s.systemMode = PREOBD_MODE_CONFIG;
    s.activeInputCount = 0;
    s.relayCount = 0;
    s.uptimeSeconds = millis() / 1000UL;
    // Firmware version: not yet sourced from a version header; zero is fine
    // for Phase 1, host MCU will push real values via UART/Tier 2 later.
    s.versionMajor = 0;
    s.versionMinor = 0;
    s.versionPatch = 0;
    s.versionBuild = 0;
    strncpy(s.deviceName, "preOBD", sizeof(s.deviceName) - 1);
    return s;
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

    bleServicesPublishStatus();
}

bool bleServicesNotifyCmdRx(const uint8_t* data, size_t len) {
    if (!Bluefruit.connected()) return true;
    return g_cmdRx.notify(data, len);
}

void bleServicesPublishStatus() {
    PreobdSystemStatus snap = buildStatusSnapshot();
    g_status.write(reinterpret_cast<uint8_t*>(&snap), sizeof(snap));
    if (Bluefruit.connected()) {
        g_status.notify(reinterpret_cast<uint8_t*>(&snap), sizeof(snap));
    }
}

BLEService& bleServicesControlService() {
    return g_controlService;
}
