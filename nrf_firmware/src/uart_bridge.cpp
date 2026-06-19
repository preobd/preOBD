// uart_bridge.cpp -- Tier 1 transparent relay.
//
// RX flush triggers (BLE notify, conservative 20-byte MTU):
//   - newline byte just seen
//   - buffer full (20 bytes)
//   - 50ms idle stall guard (catches responses without a trailing newline)
//
// TX path is buffered: BLE write callbacks enqueue into a ring and Serial1
// drains from loop() context. This keeps the SoftDevice event task from
// stalling on synchronous Serial1.write() (which can block tens of ms at
// 115200 for full BLE 5 MTU writes).
//
// No flow control in Phase 1; the burst-data test (`STREAM START`) is a
// known overrun risk. Phase 3 adds hardware CTS/RTS via direct nrfx_uarte
// alongside the MDBT50Q production port.

#include "uart_bridge.h"

#include <Arduino.h>

#include "ble_bridge.h"
#include "ble_services.h"

namespace {

constexpr size_t kRxFlushBytes = 20;       // BLE 4.2 minimum payload
constexpr uint32_t kRxStallMs = 50;        // flush after this much idle time
constexpr size_t kTxRingSize = 1024;       // ~89ms drain at 115200 worst case

uint8_t  g_rx[kRxFlushBytes];
size_t   g_rxLen = 0;
uint32_t g_lastByteMs = 0;

uint8_t  g_tx[kTxRingSize];
volatile size_t g_txHead = 0;              // producer index (BLE task)
volatile size_t g_txTail = 0;              // consumer index (loop task)

void flushRx() {
    if (g_rxLen == 0) return;
    bleServicesNotifyCmdRx(g_rx, g_rxLen);
    g_rxLen = 0;
}

inline size_t txCapacity() {
    // One slot is reserved to distinguish full from empty.
    const size_t head = g_txHead;
    const size_t tail = g_txTail;
    if (head >= tail) return kTxRingSize - (head - tail) - 1;
    return tail - head - 1;
}

void drainTx() {
    while (g_txTail != g_txHead) {
        const int wrote = Serial1.write(g_tx[g_txTail]);
        if (wrote <= 0) break;             // Serial1 buffer full, retry later
        g_txTail = (g_txTail + 1) % kTxRingSize;
    }
}

} // namespace

void uartBridgeInit() {
    // 115200 is fixed by contract: the host MCU's Serial1 MUST be configured
    // at 115200 for the BLE bridge to work. This is a hard-wired internal
    // link, not a user-facing port -- no runtime baud negotiation.
    Serial1.begin(115200);
}

void uartBridgePoll() {
    const bool connected = bleBridgeConnected();

    while (Serial1.available()) {
        const int b = Serial1.read();
        if (b < 0) break;

        if (!connected) {
            // Discard rather than buffer indefinitely.
            continue;
        }

        g_rx[g_rxLen++] = static_cast<uint8_t>(b);
        g_lastByteMs = millis();

        if (b == '\n' || g_rxLen >= kRxFlushBytes) {
            flushRx();
        }
    }

    if (connected && g_rxLen > 0 &&
        (millis() - g_lastByteMs) >= kRxStallMs) {
        flushRx();
    }

    drainTx();
}

size_t uartBridgeWrite(const uint8_t* data, size_t len) {
    // Called from BLE task; must not block on Serial1. Enqueue what fits and
    // let drainTx() (loop context) push it out.
    const size_t room = txCapacity();
    const size_t n = (len < room) ? len : room;
    for (size_t i = 0; i < n; ++i) {
        g_tx[g_txHead] = data[i];
        g_txHead = (g_txHead + 1) % kTxRingSize;
    }
    return n;
}

void uartBridgeResetRx() {
    // Called from BLE disconnect callback (BLE task) while uartBridgePoll()
    // runs in loop context. Aligned size_t writes are atomic on M4, but
    // guarding the reset keeps a poll-flush from racing the disconnect.
    noInterrupts();
    g_rxLen = 0;
    interrupts();
}
