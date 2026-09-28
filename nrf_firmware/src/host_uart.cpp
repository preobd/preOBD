// host_uart.cpp -- UARTE1 + EasyDMA host link (#491). See host_uart.h.

#include "host_uart.h"

#include <Arduino.h>
#include <string.h>
#include <nrf_soc.h>
#include <nrf_sdm.h>

namespace {

// RX: kRxBufCount DMA buffers of kRxBufBytes, chained by the ENDRX->STARTRX
// shortcut. The IRQ must program the next pointer within one buffer's wire
// time (89 ms at 115200, 11 ms at 921600) -- far above SoftDevice latency.
// Readable backlog is one buffer short of the ring, so the buffer the
// hardware is filling never aliases unread data.
constexpr uint32_t kRxBufBytes = 1024;
constexpr uint32_t kRxBufCount = 8;
constexpr uint32_t kRxCapacity = kRxBufBytes * (kRxBufCount - 1);

uint8_t s_rxBuf[kRxBufCount][kRxBufBytes];
volatile uint32_t s_rxStarted = 0;   // buffers started (from TIMER4, not an IRQ tally)
volatile bool s_rxResync = false;    // hardware/mapping out of step: restart RX
uint32_t s_rxConsumed = 0;           // bytes read (monotonic, wraps)
uint32_t s_rxSeen = 0;               // last count sampled
uint32_t s_rxLost = 0;
uint32_t s_rxRestarts = 0;           // slips detected and recovered
volatile uint32_t s_rxErrors = 0;

// TIMER3 counts RXDRDY via PPI (TIMER2's CC[0] holds the bootloader
// version, wiring.c). PPI channel 15: the SoftDevice owns 17-31.
constexpr uint8_t kPpiCh = 15;
// TIMER4 counts RXSTARTED via PPI channel 14. The next buffer pointer is
// derived from this HARDWARE count, so two RXSTARTEDs merged into one late
// IRQ (a flash erase can stall the CPU ~85 ms; one buffer lasts 11 ms at
// 921600) are detected instead of silently shifting the byte->buffer mapping
// by one slot for good (#491 review).
constexpr uint8_t kPpiChStarted = 14;

// TX: a ring the IRQ drains in DMA chunks, from RAM (EasyDMA can't read
// flash). Chunks are small enough that the host's CTS is honoured promptly.
constexpr size_t kTxRingSize = 2048;
constexpr size_t kTxChunkMax = 64;
uint8_t s_txRing[kTxRingSize];
volatile size_t s_txHead = 0;        // written by the loop
volatile size_t s_txTail = 0;        // advanced by the IRQ on ENDTX
volatile size_t s_txInFlight = 0;    // bytes in the current DMA transfer
volatile bool s_txBusy = false;
int s_ctsPin = -1;

uint32_t baudValue(uint32_t baud) {
    switch (baud) {
        case 9600:    return UARTE_BAUDRATE_BAUDRATE_Baud9600;
        case 19200:   return UARTE_BAUDRATE_BAUDRATE_Baud19200;
        case 38400:   return UARTE_BAUDRATE_BAUDRATE_Baud38400;
        case 57600:   return UARTE_BAUDRATE_BAUDRATE_Baud57600;
        case 230400:  return UARTE_BAUDRATE_BAUDRATE_Baud230400;
        case 460800:  return UARTE_BAUDRATE_BAUDRATE_Baud460800;
        case 921600:  return UARTE_BAUDRATE_BAUDRATE_Baud921600;
        case 1000000: return UARTE_BAUDRATE_BAUDRATE_Baud1M;
        case 115200:
        default:      return UARTE_BAUDRATE_BAUDRATE_Baud115200;
    }
}

uint32_t rxCount() {
    NRF_TIMER3->TASKS_CAPTURE[0] = 1;
    return NRF_TIMER3->CC[0];
}

bool ctsPaused() {
    return s_ctsPin >= 0 && digitalRead(s_ctsPin) == HIGH;
}

// Start the next TX chunk if idle, data queued and the host isn't pausing us.
// Called from the IRQ (ENDTX) and from the loop.
void txKick() {
    if (s_txBusy) return;
    const size_t head = s_txHead;
    const size_t tail = s_txTail;
    if (head == tail || ctsPaused()) return;
    size_t run = (head > tail) ? (head - tail) : (kTxRingSize - tail);
    if (run > kTxChunkMax) run = kTxChunkMax;
    s_txInFlight = run;
    s_txBusy = true;
    NRF_UARTE1->TXD.PTR = reinterpret_cast<uint32_t>(&s_txRing[tail]);
    NRF_UARTE1->TXD.MAXCNT = run;
    NRF_UARTE1->EVENTS_ENDTX = 0;
    NRF_UARTE1->TASKS_STARTTX = 1;
}

void ppiAssign(uint8_t ch, volatile uint32_t* evt, volatile uint32_t* task) {
    uint8_t sdOn = 0;
    sd_softdevice_is_enabled(&sdOn);
    if (sdOn) {
        sd_ppi_channel_assign(ch, evt, task);
        sd_ppi_channel_enable_set(1UL << ch);
    } else {
        NRF_PPI->CH[ch].EEP = reinterpret_cast<uint32_t>(evt);
        NRF_PPI->CH[ch].TEP = reinterpret_cast<uint32_t>(task);
        NRF_PPI->CHENSET = 1UL << ch;
    }
}

uint32_t startedCount() {
    NRF_TIMER4->TASKS_CAPTURE[0] = 1;
    return NRF_TIMER4->CC[0];
}

void counterInit(NRF_TIMER_Type* t) {
    t->TASKS_STOP = 1;
    t->MODE = TIMER_MODE_MODE_Counter;
    t->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    t->TASKS_CLEAR = 1;
    t->TASKS_START = 1;
}

// (Re)start reception from buffer 0 with both counters at zero. Called at
// begin and, from the loop, after a detected slip -- whatever was in flight
// is lost (and counted), the stream itself resynchronises on the next frame.
// Busy-wait up to `us` for an event register to go nonzero.
bool awaitEvent(volatile uint32_t& evt, uint32_t us) {
    const uint32_t t0 = micros();
    while (!evt) {
        if (micros() - t0 >= us) return false;
    }
    return true;
}

void rxStart() {
    // Stop and drain the receiver (a no-op before the first start), and let
    // the stop's own ENDRX land before the events are cleared -- otherwise
    // the IRQ sees that short buffer and restarts us again.
    NRF_UARTE1->SHORTS = 0;
    NRF_UARTE1->EVENTS_RXTO = 0;
    NRF_UARTE1->EVENTS_ENDRX = 0;
    NRF_UARTE1->TASKS_STOPRX = 1;
    awaitEvent(NRF_UARTE1->EVENTS_RXTO, 2000);
    NRF_UARTE1->EVENTS_ENDRX = 0;
    NRF_UARTE1->TASKS_FLUSHRX = 1;   // drain the FIFO into the current buffer
    awaitEvent(NRF_UARTE1->EVENTS_ENDRX, 2000);
    NRF_UARTE1->EVENTS_RXTO = 0;
    NVIC_ClearPendingIRQ(UARTE1_IRQn);
    NRF_TIMER3->TASKS_CLEAR = 1;
    NRF_TIMER4->TASKS_CLEAR = 1;
    s_rxStarted = 0;
    s_rxConsumed = 0;
    s_rxSeen = 0;
    s_rxResync = false;
    NRF_UARTE1->EVENTS_RXSTARTED = 0;
    NRF_UARTE1->EVENTS_ENDRX = 0;
    NRF_UARTE1->SHORTS = UARTE_SHORTS_ENDRX_STARTRX_Msk;
    NRF_UARTE1->RXD.PTR = reinterpret_cast<uint32_t>(s_rxBuf[0]);
    NRF_UARTE1->RXD.MAXCNT = kRxBufBytes;
    NRF_UARTE1->TASKS_STARTRX = 1;
}

} // namespace

extern "C" void UARTE1_IRQHandler() {
    if (NRF_UARTE1->EVENTS_RXSTARTED) {
        NRF_UARTE1->EVENTS_RXSTARTED = 0;
        // Buffers started so far, counted in hardware. Exactly one more than
        // last time means the pointer the shortcut latched was the one we
        // programmed; more means an RXSTARTED was merged into this IRQ, so a
        // buffer started on a stale pointer and the mapping has slipped.
        const uint32_t started = startedCount();
        if (started != s_rxStarted + 1) s_rxResync = true;
        s_rxStarted = started;
        NRF_UARTE1->RXD.PTR = reinterpret_cast<uint32_t>(s_rxBuf[started % kRxBufCount]);
    }
    if (NRF_UARTE1->EVENTS_ENDRX) {
        NRF_UARTE1->EVENTS_ENDRX = 0;
        // Every buffer must end full for byte i -> buffer (i / kRxBufBytes)
        // to hold; a short one (STOPRX, anything unforeseen) forces a restart.
        if (NRF_UARTE1->RXD.AMOUNT != kRxBufBytes) s_rxResync = true;
    }
    if (NRF_UARTE1->EVENTS_ERROR) {
        NRF_UARTE1->EVENTS_ERROR = 0;
        const uint32_t src = NRF_UARTE1->ERRORSRC;
        NRF_UARTE1->ERRORSRC = src;   // write-1-to-clear
        s_rxErrors++;
    }
    if (NRF_UARTE1->EVENTS_ENDTX) {
        NRF_UARTE1->EVENTS_ENDTX = 0;
        s_txTail = (s_txTail + s_txInFlight) % kTxRingSize;
        s_txInFlight = 0;
        s_txBusy = false;
        txKick();
    }
}

void hostUartBegin(uint32_t baud, int ctsPin) {
    s_ctsPin = ctsPin;
    // Idle-high TX before the peripheral takes the pin (Nordic's advice).
    pinMode(PIN_SERIAL1_TX, OUTPUT);
    digitalWrite(PIN_SERIAL1_TX, HIGH);
    pinMode(PIN_SERIAL1_RX, INPUT);

    NRF_UARTE1->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
    NRF_UARTE1->PSEL.TXD = g_ADigitalPinMap[PIN_SERIAL1_TX];
    NRF_UARTE1->PSEL.RXD = g_ADigitalPinMap[PIN_SERIAL1_RX];
    NRF_UARTE1->PSEL.CTS = 0xFFFFFFFFUL;
    NRF_UARTE1->PSEL.RTS = 0xFFFFFFFFUL;
    NRF_UARTE1->CONFIG = 0;                       // 8N1, no HW flow control
    NRF_UARTE1->BAUDRATE = baudValue(baud);

    // TIMER3 counts received bytes (RXDRDY), TIMER4 started buffers
    // (RXSTARTED), both via PPI.
    counterInit(NRF_TIMER3);
    counterInit(NRF_TIMER4);
    ppiAssign(kPpiCh, &NRF_UARTE1->EVENTS_RXDRDY, &NRF_TIMER3->TASKS_COUNT);
    ppiAssign(kPpiChStarted, &NRF_UARTE1->EVENTS_RXSTARTED, &NRF_TIMER4->TASKS_COUNT);

    s_txHead = s_txTail = s_txInFlight = 0;
    s_txBusy = false;

    NRF_UARTE1->EVENTS_ERROR = 0;
    NRF_UARTE1->EVENTS_ENDTX = 0;
    NRF_UARTE1->INTENSET = UARTE_INTENSET_RXSTARTED_Msk | UARTE_INTENSET_ENDRX_Msk |
                           UARTE_INTENSET_ERROR_Msk | UARTE_INTENSET_ENDTX_Msk;
    NVIC_SetPriority(UARTE1_IRQn, 3);   // an application level the SoftDevice allows
    NVIC_ClearPendingIRQ(UARTE1_IRQn);
    NVIC_EnableIRQ(UARTE1_IRQn);

    NRF_UARTE1->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
    NVIC_DisableIRQ(UARTE1_IRQn);
    rxStart();
    NVIC_EnableIRQ(UARTE1_IRQn);
}

// A slip (late RXSTARTED or short buffer) restarts reception from the loop.
static void rxRecoverIfNeeded() {
    if (!s_rxResync) return;
    NVIC_DisableIRQ(UARTE1_IRQn);
    s_rxLost += rxCount() - s_rxConsumed;   // what was unread is gone
    s_rxRestarts++;
    rxStart();
    NVIC_EnableIRQ(UARTE1_IRQn);
}

size_t hostUartAvailable() {
    rxRecoverIfNeeded();
    const uint32_t c = rxCount();
    if (c != s_rxSeen) {
        s_rxSeen = c;
        // RXDRDY can precede the byte's EasyDMA write to RAM by a few cycles;
        // a microsecond covers it before anyone reads the new bytes.
        delayMicroseconds(1);
    }
    uint32_t backlog = c - s_rxConsumed;
    if (backlog > kRxCapacity) {
        s_rxLost += backlog - kRxCapacity;
        s_rxConsumed = c - kRxCapacity;
        backlog = kRxCapacity;
    }
    return backlog;
}

int hostUartRead() {
    if (s_rxConsumed == s_rxSeen && hostUartAvailable() == 0) return -1;
    const uint32_t i = s_rxConsumed++;
    return s_rxBuf[(i / kRxBufBytes) % kRxBufCount][i % kRxBufBytes];
}

size_t hostUartWriteSpace() {
    const size_t head = s_txHead;
    const size_t tail = s_txTail;
    return (tail + kTxRingSize - head - 1) % kTxRingSize;
}

size_t hostUartWrite(const uint8_t* data, size_t len) {
    size_t n = hostUartWriteSpace();
    if (n > len) n = len;
    size_t head = s_txHead;
    for (size_t i = 0; i < n; ++i) {
        s_txRing[head] = data[i];
        head = (head + 1) % kTxRingSize;
    }
    __DMB();   // the bytes are in RAM before the IRQ can see the new head
    s_txHead = head;
    if (n) {
        NVIC_DisableIRQ(UARTE1_IRQn);
        txKick();
        NVIC_EnableIRQ(UARTE1_IRQn);
    }
    return n;
}

void hostUartService() {
    NVIC_DisableIRQ(UARTE1_IRQn);
    txKick();
    NVIC_EnableIRQ(UARTE1_IRQn);
}

uint32_t hostUartRxLost() { return s_rxLost; }
uint32_t hostUartRxErrors() { return s_rxErrors; }
uint32_t hostUartRxRestarts() { return s_rxRestarts; }
