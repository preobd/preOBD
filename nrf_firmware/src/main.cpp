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
    // The 128-bit Control Service UUID already fills the 31-byte advertising
    // packet, so the HM-10 (FFE0) service goes in the separate scan-response
    // budget. It's a 16-bit UUID (4 bytes) -- cheap, and it lets phone OBD apps
    // that filter on FFE0 in scan data recognize us as an ELM327 adapter. NUS
    // is left to post-connect discovery (generic terminal apps connect then
    // enumerate services, and a second 128-bit UUID wouldn't leave room for
    // the name). The name stays in the scan response for human-readable scans.
    Bluefruit.ScanResponse.addService(bleServicesHm10Service());
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

    // Negotiate up the ATT MTU so the 32-byte PreobdSystemStatus notify fits
    // in a single ATT packet. BLE's default ATT MTU is 23 (20-byte payload),
    // which truncated the status struct mid-frame -- the webapp's initial
    // GATT Read worked (Read Blob fragments across multiple packets) so the
    // panel populated once and then never updated, because every notify
    // arrived 20 bytes long and applySystemStatus() rejected it. Alarm (18B)
    // and Relay (16B) notifies happened to fit in the default and were
    // unaffected, but raising the ceiling matters for any future binary
    // payloads (and for chunking efficiency on the CMD_RX text path).
    // BANDWIDTH_MAX sets ATT MTU to 247 and sizes the SoftDevice's event
    // buffers to match; Chrome auto-negotiates up on connect.
    //
    // MUST be called BEFORE Bluefruit.begin() -- begin() consumes the
    // bandwidth config to allocate SoftDevice event buffers. Calling after
    // begin() either no-ops or half-reconfigures the live GATT stack, which
    // is what made the GATT connection drop mid-discovery on the first
    // build attempting this fix.
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

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
