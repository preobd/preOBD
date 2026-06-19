// ble_bridge.cpp -- BLE connection lifecycle + NRF_IRQ line.
//
// NRF_IRQ contract (locked Phase 1):
//   active-high, sustained. HIGH while a BLE central is connected, LOW
//   otherwise. The host MCU watches both edges.
//
// Two-central concurrency: connection state is derived from Bluefruit's
// authoritative peripheral count (Bluefruit.Periph.connected()), NOT a
// hand-maintained counter. A free-running counter drifts permanently if any
// connect/disconnect event is ever missed or doubled over a long churny
// session -- which left the IRQ line / status LED stuck HIGH with nothing
// connected. syncConnectionState() detects the 0<->1 edges (IRQ + tier
// negotiation/teardown) and keeps advertising armed whenever a slot is free.
//
// It is driven SOLELY from the loop() poll (bleBridgePoll), never from the BLE
// connect/disconnect callbacks. Single-context edge detection avoids a race on
// the previous-count state between the BLE-event task and the loop() task (and
// double-firing tierStateOnConnect). The poll runs every loop iteration, so
// IRQ/tier latency is sub-ms, and reading the SoftDevice's count every loop is
// the self-healing mechanism: state always re-converges to ground truth.

#include "ble_bridge.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "board_pins.h"
#include "tier_state.h"
#include "uart_bridge.h"

namespace {

constexpr uint8_t kMaxPrphConns = 2;   // must match Bluefruit.begin(2)

// Last-known authoritative peripheral connection count, for 0<->1 edge
// detection only. Touched only by syncConnectionState() (loop-task context),
// so it needs no volatile/atomic guard. Ground truth is always
// Bluefruit.Periph.connected().
uint8_t g_prevConnected = 0;

void setIrqLine(bool high) {
    digitalWrite(PIN_NRF_IRQ_OUT, high ? HIGH : LOW);
    digitalWrite(PIN_STATUS_LED, high ? HIGH : LOW);
}

void syncConnectionState() {
    const uint8_t now = Bluefruit.Periph.connected();   // authoritative count

    if (now != g_prevConnected) {
        const uint8_t prev = g_prevConnected;
        g_prevConnected = now;
        if (prev == 0 && now > 0) {
            // First central: raise the IRQ line and start tier auto-detect.
            setIrqLine(true);
            tierStateOnConnect();
        } else if (prev > 0 && now == 0) {
            // Last central gone: drop the IRQ line, reset the host RX
            // accumulator, and tear framing back down to DISCONNECTED.
            setIrqLine(false);
            uartBridgeResetRx();
            tierStateOnDisconnect();
        }
    }

    // Keep advertising whenever a peripheral slot is free. The SoftDevice stops
    // advertising on every connect, and Bluefruit's restartOnDisconnect only
    // re-arms on the LAST disconnect, so a freed slot (second central drops
    // while the first stays) -- or any silently-stopped advertising -- is
    // re-armed here. isRunning() guards against redundant/double start() calls.
    if (now < kMaxPrphConns && !Bluefruit.Advertising.isRunning()) {
        Bluefruit.Advertising.start(0);
    }
}

} // namespace

void bleBridgeInit() {
    pinMode(PIN_NRF_IRQ_OUT, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    setIrqLine(false);
    // No connect/disconnect callbacks: connection state is polled from loop()
    // (bleBridgePoll) so edge detection is single-context and self-healing.
}

void bleBridgePoll() {
    // Polled from loop(): re-converges IRQ / tier / advertising to the
    // authoritative connection count even if a BLE event was missed.
    syncConnectionState();
}

bool bleBridgeConnected() {
    return Bluefruit.Periph.connected() > 0;
}
