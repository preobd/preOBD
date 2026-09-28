// host_uart.h -- the UART link to the host MCU, on UARTE1 with EasyDMA (#491).
//
// Replaces Serial1 for the host link. The Adafruit core's Uart receives one
// byte per EasyDMA transfer and re-arms from its IRQ, so SoftDevice radio
// events can hold that IRQ off long enough for the UARTE's small FIFO to
// overrun. Measured (#491, with the GPIO flow control wired): Serial1 lost
// frames on every bulk dump at 921600; this driver lost none, and nRF->host
// writes got faster at every rate. Here the hardware fills a chain of large
// RAM buffers on its own; the CPU only points it at the next buffer once per
// buffer, and a TIMER counts received bytes through PPI, so radio events can
// delay the reader without losing data. TX is chained the same way from an
// internal ring. (Flow control is still what protects a bulk transfer to a
// slowly draining central: the ring is finite.)
//
// UARTE1 is free on boards whose variant defines no Serial2 (ItsyBitsy
// nRF52840); Serial1 (UARTE0) is simply never started. No core changes.

#ifndef HOST_UART_H
#define HOST_UART_H

#include <stdint.h>
#include <stddef.h>

// Start the link on the board's Serial1 pins (PIN_SERIAL1_RX/TX).
// `ctsPin` (Arduino pin, or -1) is the host's "pause your TX" line: while it
// reads HIGH no new TX chunk starts.
void hostUartBegin(uint32_t baud, int ctsPin = -1);

// Bytes received and not yet read. Bytes the hardware overwrote before they
// were read are dropped and counted (hostUartRxLost).
size_t hostUartAvailable();

// Next received byte, or -1 if none.
int hostUartRead();

// Queue up to `len` bytes for transmission. Non-blocking: returns how many
// fit in the TX ring.
size_t hostUartWrite(const uint8_t* data, size_t len);

// Free space in the TX ring.
size_t hostUartWriteSpace();

// Restart a TX chain that paused on CTS or ran dry. Call from loop().
void hostUartService();

// Diagnostics: bytes lost to the reader falling behind the DMA ring, and
// UARTE ERROR events (overrun / framing / break) seen since begin.
uint32_t hostUartRxLost();
uint32_t hostUartRxErrors();
// Receiver restarts after a detected buffer slip (see host_uart.cpp).
uint32_t hostUartRxRestarts();

#endif // HOST_UART_H
