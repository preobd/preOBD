/*
 * bus_config.h - Bus Configuration Structure
 *
 * Simple "pick one" model for I2C, SPI, and CAN buses.
 * Select which bus to use for each type and set its speed.
 * All sensors/outputs of a given type use the selected bus.
 *
 * Also configures hardware serial ports (Serial1-Serial8).
 *
 * Sizes: BusConfig ~19 bytes (platform padding varies) + 16 bytes for SerialPortConfig
 */

#ifndef BUS_CONFIG_H
#define BUS_CONFIG_H

#include <stdint.h>

/**
 * CAN bus mode (#612 phase 2a: one set of settings per bus, §14.7)
 *
 * - OFF:     the bus is never started; nothing may attach to it.
 * - NORMAL:  ACKs and may transmit (OBD answers, CAN input polling).
 * - LISTEN:  listen-only: no ACK bits, no error frames, no TX of any kind. For
 *            sniffing an existing bus (a car's OBD-II/ECU network) without
 *            disturbing it. A consumer that sends (OBD) is refused here.
 *
 * A bus that is on is started only when its first consumer attaches, so an
 * enabled bus nobody uses never ACKs on a car's network.
 */
enum CanBusMode : uint8_t {
    CAN_BUS_OFF    = 0,
    CAN_BUS_NORMAL = 1,
    CAN_BUS_LISTEN = 2
};

// Baud rates a CAN bus can be set to, stored as an index (CanBusSettings).
enum CanBaudIndex : uint8_t { CAN_BAUD_125K = 0, CAN_BAUD_250K = 1, CAN_BAUD_500K = 2, CAN_BAUD_1M = 3,
                              NUM_CAN_BAUDS = 4 };
inline uint32_t canBaudFromIndex(uint8_t i) {
    return i == CAN_BAUD_125K ? 125000UL : i == CAN_BAUD_250K ? 250000UL
         : i == CAN_BAUD_1M ? 1000000UL : 500000UL;
}
// 0xFF for a rate the controllers don't run at
inline uint8_t canBaudIndexOf(uint32_t bps) {
    return bps == 125000UL ? CAN_BAUD_125K : bps == 250000UL ? CAN_BAUD_250K
         : bps == 500000UL ? CAN_BAUD_500K : bps == 1000000UL ? CAN_BAUD_1M : 0xFF;
}

struct CanBusSettings {
    uint8_t mode;        // CanBusMode
    uint8_t baudIndex;   // CanBaudIndex
};

// CAN1..CAN3 in the stored layout on every board, so it doesn't depend on
// NUM_CAN_BUSES; slots past a board's NUM_CAN_BUSES stay OFF.
#define CAN_BUS_SLOTS 3

/**
 * Bus Configuration Structure
 *
 * For each bus type, stores:
 * - Which bus instance to use (0, 1, or 2)
 * - The speed/baudrate setting
 *
 * Example: active_i2c=1 means all I2C sensors use Wire1
 */
struct BusConfig {
    uint8_t active_i2c;         // 0=Wire, 1=Wire1, 2=Wire2
    uint16_t i2c_clock;         // kHz (100, 400, 1000)

    uint8_t active_spi;         // 0=SPI, 1=SPI1, 2=SPI2
    uint32_t spi_clock;         // Hz (e.g., 4000000 = 4MHz)

    // CAN: one set of settings per bus, and what attaches to each (§14.7)
    CanBusSettings can[CAN_BUS_SLOTS];
    uint8_t canInputBus;        // the bus CAN input reads: 0..2, 0xFF = CAN input off
    // (The OBD output's bus and the ELM327 output's port are their output
    // records' transports, output_table.h.)
};

/**
 * Serial Port Baud Rate Index
 *
 * Maps index values to standard baud rates.
 * Used by SerialPortConfig.baudrate_index[].
 */
enum SerialBaudIndex {
    BAUD_9600   = 0,   //   9600 bps
    BAUD_19200  = 1,   //  19200 bps
    BAUD_38400  = 2,   //  38400 bps
    BAUD_57600  = 3,   //  57600 bps
    BAUD_115200 = 4,   // 115200 bps (default)
    BAUD_230400 = 5,   // 230400 bps
    BAUD_460800 = 6,   // 460800 bps
    BAUD_921600 = 7,   // 921600 bps
    NUM_BAUD_RATES = 8
};

/**
 * Serial Port Configuration Structure
 *
 * Configures which hardware serial ports (Serial1-Serial8) are enabled
 * and their baud rates. Multiple ports can be enabled simultaneously
 * since TRANSPORT can assign different ports to different planes.
 *
 * Total size: 16 bytes
 */
struct SerialPortConfig {
    // Enabled port bitmask (1 byte)
    // Bit 0 = Serial1, Bit 1 = Serial2, ... Bit 7 = Serial8
    uint8_t enabled_mask;

    // Baud rate index for each port (8 bytes)
    // Index 0 = Serial1, Index 1 = Serial2, etc.
    // Values are SerialBaudIndex enum (0-7)
    uint8_t baudrate_index[8];

    // Per-port framed-mode bitmask (1 byte). Bit N (N=0..7) = Serial(N+1).
    // When set, MuxTransport on that port runs in framed mode (the host-MCU
    // end of the nRF52840 coprocessor link's binary framing protocol);
    // otherwise it acts as transparent passthrough. Toggle at runtime via
    // `BUS SERIAL <port> FRAMED ENABLE|DISABLE`. Default (zero) is
    // backwards-compatible with HM-10, stock Bluefruit, and pre-framed
    // nRF firmware.
    uint8_t framed_enabled_mask;

    // Reserved for future expansion (6 bytes, was 7 before framed_enabled_mask)
    uint8_t reserved[6];
};  // 16 bytes total

#endif // BUS_CONFIG_H
