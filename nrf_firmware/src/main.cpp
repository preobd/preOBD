// main.cpp -- nRF52840 firmware entry point.
//
// Phase 1: BLE <-> UART transparent bridge. No business logic, no Tier 2
// framing, OTA stubbed.

#include <Arduino.h>
#include <bluefruit.h>

#include "alarm_handler.h"
#include "ble_bridge.h"
#include "ble_services.h"
#include "dfu_trigger.h"
#include "dis_handler.h"
#include "extended_service.h"
#include "ota_handler.h"
#include "relay_handler.h"
#include "tier_state.h"
#include "uart_bridge.h"

namespace {

void startAdvertising() {
    // The 31-byte advertising packet is tight: flags (3) + TX power (3) +
    // 128-bit service UUID (18) leaves only 7 bytes -- not enough for the
    // full "preOBD" name. Put the name in the scan response packet, which
    // gets a separate 31 bytes that scanners pull on demand.
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleServicesControlService());
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);   // 20ms fast / 152.5ms slow
    Bluefruit.Advertising.setFastTimeout(30);     // 30s fast then drop to slow
    Bluefruit.Advertising.start(0);               // advertise indefinitely
}

} // namespace

void setup() {
#if DEBUG_SERIAL
    Serial.begin(115200);
    // No while(!Serial) -- we want to run with USB unplugged.
#endif

    uartBridgeInit();
    tierStateInit();

    Bluefruit.begin();
    Bluefruit.setName("preOBD");
    Bluefruit.setTxPower(0);                      // 0 dBm for bench bring-up
    Bluefruit.autoConnLed(false);                 // we drive PIN_STATUS_LED

    // Request a tight connection interval (15-30ms, units of 1.25ms). Chrome
    // on macOS will otherwise stretch the interval to ~1s when "idle",
    // dropping the effective BLE drain rate to ~100 B/s during the dump --
    // at which point our flow control plumbing can't drain the nRF Serial1
    // RX ring fast enough to keep up with bursts. 15-30ms keeps the link
    // around 7-9 KB/s, comfortably above the throughput we need for
    // SYSTEM DUMP JSON and similar bulk responses.
    Bluefruit.Periph.setConnInterval(12, 24);

    bleBridgeInit();
    bleServicesInit();
    // Begin the Extended Service ONCE before its tenants register their
    // characteristics; Bluefruit attaches each characteristic.begin() to
    // the most-recently-begun service. Order-sensitive: any service.begin()
    // call between here and the last handler init below would orphan the
    // remaining characteristics.
    extendedServiceInit();
    otaHandlerInit();
    alarmHandlerInit();
    relayHandlerInit();
    disHandlerInit();
    dfuTriggerInit();

    startAdvertising();
}

void loop() {
    tierStatePoll();
    uartBridgePoll();
    dfuTriggerPoll();
}
