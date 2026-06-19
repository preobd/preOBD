// ota_handler.cpp -- Phase 1 stub.
//
// The stub registers the Extended Service plus OTA_CTRL/OTA_DATA so the
// phone-side OTA flow can be wired end-to-end without a host bootloader.
// Behavior: ack START, swallow DATA, return COMPLETE{crc32_ok=false}.

#include "ota_handler.h"

#include <Arduino.h>
#include <string.h>

#include <bluefruit.h>

#include "ble_gatt_defs.h"
#include "ble_services.h"
#include "uuid_util.h"

namespace {

BLECharacteristic g_otaCtrl;
BLECharacteristic g_otaData;

// Stub state. A real implementation would track expected bytes, CRC, and
// would forward chunks over SPI to the host MCU.
bool     g_sessionActive = false;
uint32_t g_bytesSeen = 0;

constexpr uint8_t kOtaCmdStart    = 0x01;
constexpr uint8_t kOtaCmdAbort    = 0x02;
constexpr uint8_t kOtaCmdComplete = 0x03;

void notifyComplete(bool crcOk) {
    uint8_t pkt[2] = { kOtaCmdComplete, static_cast<uint8_t>(crcOk ? 1 : 0) };
    bleServicesNotifyAll(g_otaCtrl, pkt, sizeof(pkt));
}

void otaCtrlWrite(uint16_t /*conn*/, BLECharacteristic* /*chr*/,
                  uint8_t* data, uint16_t len) {
    if (!data || len < 1) return;
    const uint8_t cmd = data[0];

    if (cmd == kOtaCmdStart) {
        g_sessionActive = true;
        g_bytesSeen = 0;
        // Real impl would parse {image_size, crc32} and assert NRF_DFU.
    } else if (cmd == kOtaCmdAbort) {
        g_sessionActive = false;
        g_bytesSeen = 0;
    } else if (cmd == kOtaCmdComplete) {
        g_sessionActive = false;
        notifyComplete(false); // stub always fails
    }
}

void otaDataWrite(uint16_t /*conn*/, BLECharacteristic* /*chr*/,
                  uint8_t* /*data*/, uint16_t len) {
    if (g_sessionActive) {
        g_bytesSeen += len;
    }
    // Chunks are discarded in Phase 1.
}

} // namespace

void otaHandlerInit() {
    // Extended Service is owned by extended_service.cpp; see main.cpp.
    // BLEUuid stores a pointer, not a copy -- give each UUID its own
    // persistent backing buffer. See ble_services.cpp for the full story.
    static uint8_t kOtaCtrlUuid[16];
    static uint8_t kOtaDataUuid[16];

    if (parseUuid128(PREOBD_CHAR_OTA_CTRL_UUID, kOtaCtrlUuid)) {
        g_otaCtrl = BLECharacteristic(kOtaCtrlUuid);
    }
    g_otaCtrl.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    g_otaCtrl.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_otaCtrl.setMaxLen(32);
    g_otaCtrl.setWriteCallback(otaCtrlWrite);
    g_otaCtrl.begin();

    if (parseUuid128(PREOBD_CHAR_OTA_DATA_UUID, kOtaDataUuid)) {
        g_otaData = BLECharacteristic(kOtaDataUuid);
    }
    g_otaData.setProperties(CHR_PROPS_WRITE_WO_RESP);
    g_otaData.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_otaData.setMaxLen(512);
    g_otaData.setWriteCallback(otaDataWrite);
    g_otaData.begin();
}
