// ble_bridge.cpp -- connect/disconnect callbacks + NRF_IRQ line.
//
// NRF_IRQ contract (locked Phase 1):
//   active-high, sustained. HIGH while a BLE central is connected, LOW
//   otherwise. The host MCU watches both edges.

#include "ble_bridge.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "board_pins.h"
#include "uart_bridge.h"

namespace {

volatile bool g_connected = false;

void setIrqLine(bool high) {
    digitalWrite(PIN_NRF_IRQ_OUT, high ? HIGH : LOW);
    digitalWrite(PIN_STATUS_LED, high ? HIGH : LOW);
}

void onConnect(uint16_t /*conn_handle*/) {
    g_connected = true;
    setIrqLine(true);
}

void onDisconnect(uint16_t /*conn_handle*/, uint8_t /*reason*/) {
    g_connected = false;
    setIrqLine(false);
    uartBridgeResetRx();
    // Bluefruit auto-restarts advertising by default; nothing to do here.
}

} // namespace

void bleBridgeInit() {
    pinMode(PIN_NRF_IRQ_OUT, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    setIrqLine(false);

    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);
}

bool bleBridgeConnected() {
    return g_connected;
}
