/*
 * bus_defaults.h - Platform-Specific Bus Pin Defaults
 *
 * Defines default hardware pins for I2C, SPI, CAN, and Serial buses across platforms.
 * Also defines default bus selections and speeds for the simplified "pick one" model.
 *
 * Platform Support:
 * - Teensy 4.1 (IMXRT1062): 3x I2C, 3x SPI, 3x CAN, 8x Serial (fixed pins)
 * - Teensy 4.0 (IMXRT1062): 3x I2C, 3x SPI, 3x CAN, 7x Serial (fixed pins)
 * - Teensy 3.6 (MK66FX1M0): 3x I2C, 2x SPI, 2x CAN, 6x Serial (I2C remappable)
 * - ESP32: 2x I2C, 1x SPI, 1x CAN, 2x Serial (fully remappable)
 * - Arduino Mega (ATmega2560): 1x I2C, 1x SPI, 0x CAN, 3x Serial
 */

#ifndef BUS_DEFAULTS_H
#define BUS_DEFAULTS_H

#include "platform.h"

// ============================================================================
// DEFAULT BUS SELECTIONS AND SPEEDS
// ============================================================================

#define DEFAULT_I2C_BUS        0        // Wire
#define DEFAULT_I2C_CLOCK      400      // kHz
#define DEFAULT_SPI_BUS        0        // SPI
#define DEFAULT_SPI_CLOCK      4000000  // Hz (4MHz)
#define DEFAULT_CAN_BUS        0        // CAN1
#define DEFAULT_CAN_BAUDRATE   500000   // bps
#define DEFAULT_SERIAL_BAUDRATE 115200  // bps

// ============================================================================
// TEENSY 4.x (IMXRT1062) - Teensy 4.0, 4.1
// ============================================================================
#if defined(__IMXRT1062__)

// ----- I2C Default Pins -----
// Source: https://www.pjrc.com/teensy/td_libs_Wire.html
#define DEFAULT_I2C0_SDA 18
#define DEFAULT_I2C0_SCL 19
#define DEFAULT_I2C1_SDA 17
#define DEFAULT_I2C1_SCL 16
#define DEFAULT_I2C2_SDA 25
#define DEFAULT_I2C2_SCL 24

// ----- SPI Default Pins -----
// Source: https://www.pjrc.com/teensy/td_libs_SPI.html
#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13
#define DEFAULT_SPI1_MOSI 26
#define DEFAULT_SPI1_MISO 1
#define DEFAULT_SPI1_SCK  27
#define DEFAULT_SPI2_MOSI 35
#define DEFAULT_SPI2_MISO 34
#define DEFAULT_SPI2_SCK  37

// ----- CAN Default Pins -----
// Source: https://github.com/tonton81/FlexCAN_T4
#define DEFAULT_CAN1_TX 22
#define DEFAULT_CAN1_RX 23
#define DEFAULT_CAN2_TX 0
#define DEFAULT_CAN2_RX 1
#define DEFAULT_CAN3_TX 30
#define DEFAULT_CAN3_RX 31

// ----- Serial Port Default Pins -----
// Source: https://www.pjrc.com/teensy/td_uart.html
#define DEFAULT_SERIAL1_RX  0
#define DEFAULT_SERIAL1_TX  1
#define DEFAULT_SERIAL2_RX  7
#define DEFAULT_SERIAL2_TX  8
#define DEFAULT_SERIAL3_RX  15
#define DEFAULT_SERIAL3_TX  14
#define DEFAULT_SERIAL4_RX  16
#define DEFAULT_SERIAL4_TX  17
#define DEFAULT_SERIAL5_RX  21
#define DEFAULT_SERIAL5_TX  20
#define DEFAULT_SERIAL6_RX  25
#define DEFAULT_SERIAL6_TX  24
#define DEFAULT_SERIAL7_RX  28
#define DEFAULT_SERIAL7_TX  29
#if defined(ARDUINO_TEENSY41)
#define DEFAULT_SERIAL8_RX  34
#define DEFAULT_SERIAL8_TX  35
#endif

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 3
#define NUM_SPI_BUSES 3
#define NUM_CAN_BUSES 3
#if defined(ARDUINO_TEENSY41)
#define NUM_SERIAL_PORTS 8
#else
#define NUM_SERIAL_PORTS 7
#endif
#define SUPPORTS_WIRE_PIN_REMAP 0  // Teensy 4.x uses fixed hardware pins
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// TEENSY 3.6 (MK66FX1M0)
// ============================================================================
#elif defined(__MK66FX1M0__)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA 18
#define DEFAULT_I2C0_SCL 19
#define DEFAULT_I2C1_SDA 37
#define DEFAULT_I2C1_SCL 38
#define DEFAULT_I2C2_SDA 3
#define DEFAULT_I2C2_SCL 4

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13
#define DEFAULT_SPI1_MOSI 0
#define DEFAULT_SPI1_MISO 1
#define DEFAULT_SPI1_SCK  32
// No SPI2 on Teensy 3.6

// ----- CAN Default Pins -----
#define DEFAULT_CAN1_TX 3
#define DEFAULT_CAN1_RX 4
#define DEFAULT_CAN2_TX 33  // CAN FD capable
#define DEFAULT_CAN2_RX 34
// No CAN3 on Teensy 3.6

// ----- Serial Port Default Pins -----
#define DEFAULT_SERIAL1_RX  0
#define DEFAULT_SERIAL1_TX  1
#define DEFAULT_SERIAL2_RX  9
#define DEFAULT_SERIAL2_TX  10
#define DEFAULT_SERIAL3_RX  7
#define DEFAULT_SERIAL3_TX  8
#define DEFAULT_SERIAL4_RX  31
#define DEFAULT_SERIAL4_TX  32
#define DEFAULT_SERIAL5_RX  34
#define DEFAULT_SERIAL5_TX  33
#define DEFAULT_SERIAL6_RX  47
#define DEFAULT_SERIAL6_TX  48

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 3
#define NUM_SPI_BUSES 2
#define NUM_CAN_BUSES 2
#define NUM_SERIAL_PORTS 6
#define SUPPORTS_WIRE_PIN_REMAP 1  // Teensy 3.x supports I2C pin remapping
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// TEENSY 3.5 (MK64FX512)
// ============================================================================
#elif defined(__MK64FX512__)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA 18
#define DEFAULT_I2C0_SCL 19
#define DEFAULT_I2C1_SDA 37
#define DEFAULT_I2C1_SCL 38
#define DEFAULT_I2C2_SDA 3
#define DEFAULT_I2C2_SCL 4

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13
#define DEFAULT_SPI1_MOSI 0
#define DEFAULT_SPI1_MISO 1
#define DEFAULT_SPI1_SCK  32

// ----- CAN Default Pins -----
#define DEFAULT_CAN1_TX 3
#define DEFAULT_CAN1_RX 4
// Only 1 CAN bus on Teensy 3.5

// ----- Serial Port Default Pins -----
#define DEFAULT_SERIAL1_RX  0
#define DEFAULT_SERIAL1_TX  1
#define DEFAULT_SERIAL2_RX  9
#define DEFAULT_SERIAL2_TX  10
#define DEFAULT_SERIAL3_RX  7
#define DEFAULT_SERIAL3_TX  8
#define DEFAULT_SERIAL4_RX  31
#define DEFAULT_SERIAL4_TX  32
#define DEFAULT_SERIAL5_RX  34
#define DEFAULT_SERIAL5_TX  33
#define DEFAULT_SERIAL6_RX  47
#define DEFAULT_SERIAL6_TX  48

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 3
#define NUM_SPI_BUSES 2
#define NUM_CAN_BUSES 1
#define NUM_SERIAL_PORTS 6
#define SUPPORTS_WIRE_PIN_REMAP 1
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// TEENSY 3.1/3.2 (MK20DX256)
// ============================================================================
#elif defined(__MK20DX256__)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA 18
#define DEFAULT_I2C0_SCL 19
#define DEFAULT_I2C1_SDA 29
#define DEFAULT_I2C1_SCL 30
// No Wire2 on Teensy 3.1/3.2

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13
// Only 1 SPI bus on Teensy 3.1/3.2

// ----- CAN Default Pins -----
#define DEFAULT_CAN1_TX 3
#define DEFAULT_CAN1_RX 4

// ----- Serial Port Default Pins -----
#define DEFAULT_SERIAL1_RX  0
#define DEFAULT_SERIAL1_TX  1
#define DEFAULT_SERIAL2_RX  9
#define DEFAULT_SERIAL2_TX  10
#define DEFAULT_SERIAL3_RX  7
#define DEFAULT_SERIAL3_TX  8

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 2
#define NUM_SPI_BUSES 1
#define NUM_CAN_BUSES 1
#define NUM_SERIAL_PORTS 3
#define SUPPORTS_WIRE_PIN_REMAP 1
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// ESP32
// ============================================================================
#elif defined(ESP32)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA 21
#define DEFAULT_I2C0_SCL 22
#define DEFAULT_I2C1_SDA 26
#define DEFAULT_I2C1_SCL 27
// No Wire2 on ESP32

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 23
#define DEFAULT_SPI0_MISO 19
#define DEFAULT_SPI0_SCK  18
// ESP32 can have multiple SPI buses, but default library config uses one

// ----- CAN (TWAI) Default Pins -----
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEFAULT_CAN1_TX 20
#define DEFAULT_CAN1_RX 21
#else
#define DEFAULT_CAN1_TX 21
#define DEFAULT_CAN1_RX 22
#endif

// ----- Serial Port Default Pins -----
#define DEFAULT_SERIAL1_RX  9
#define DEFAULT_SERIAL1_TX  10
#define DEFAULT_SERIAL2_RX  16
#define DEFAULT_SERIAL2_TX  17

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 2
#define NUM_SPI_BUSES 1
#define NUM_CAN_BUSES 1
#define NUM_SERIAL_PORTS 2
#define SUPPORTS_WIRE_PIN_REMAP 1  // ESP32 supports full pin remapping
#define SUPPORTS_SPI_PIN_REMAP 1
#define SUPPORTS_CAN_PIN_REMAP 1
#define SUPPORTS_SERIAL_PIN_REMAP 1

// ============================================================================
// ARDUINO MEGA 2560 (ATmega2560)
// ============================================================================
#elif defined(__AVR_ATmega2560__)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA 20
#define DEFAULT_I2C0_SCL 21
// Only Wire on Mega

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 51
#define DEFAULT_SPI0_MISO 50
#define DEFAULT_SPI0_SCK  52
// Only SPI on Mega

// ----- CAN Default Pins -----
// No native CAN on Arduino Mega (requires external MCP2515)

// ----- Serial Port Default Pins -----
#define DEFAULT_SERIAL1_RX  19
#define DEFAULT_SERIAL1_TX  18
#define DEFAULT_SERIAL2_RX  17
#define DEFAULT_SERIAL2_TX  16
#define DEFAULT_SERIAL3_RX  15
#define DEFAULT_SERIAL3_TX  14

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 1
#define NUM_SPI_BUSES 1
#define NUM_CAN_BUSES 0
#define NUM_SERIAL_PORTS 3
#define SUPPORTS_WIRE_PIN_REMAP 0
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// ARDUINO UNO (ATmega328P)
// ============================================================================
#elif defined(__AVR_ATmega328P__)

// ----- I2C Default Pins -----
#define DEFAULT_I2C0_SDA A4
#define DEFAULT_I2C0_SCL A5

// ----- SPI Default Pins -----
#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13

// ----- Serial Port Default Pins -----
// No additional hardware serial on ATmega328P (Serial0 is USB)

// ----- Platform Capabilities -----
#define NUM_I2C_BUSES 1
#define NUM_SPI_BUSES 1
#define NUM_CAN_BUSES 0
#define NUM_SERIAL_PORTS 0
#define SUPPORTS_WIRE_PIN_REMAP 0
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

// ============================================================================
// UNKNOWN PLATFORM
// ============================================================================
#else

// ----- Fallback Defaults -----
#warning "Unknown platform - using conservative bus defaults"

#define DEFAULT_I2C0_SDA 18
#define DEFAULT_I2C0_SCL 19

#define DEFAULT_SPI0_MOSI 11
#define DEFAULT_SPI0_MISO 12
#define DEFAULT_SPI0_SCK  13

#define NUM_I2C_BUSES 1
#define NUM_SPI_BUSES 1
#define NUM_CAN_BUSES 0
#define NUM_SERIAL_PORTS 0
#define SUPPORTS_WIRE_PIN_REMAP 0
#define SUPPORTS_SPI_PIN_REMAP 0
#define SUPPORTS_CAN_PIN_REMAP 0
#define SUPPORTS_SERIAL_PIN_REMAP 0

#endif

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Get default SDA pin for I2C bus
 * @param bus_id I2C bus number (0-2)
 * @return Default SDA pin, or 0xFF if bus not available
 */
inline uint8_t getDefaultI2CSDA(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return DEFAULT_I2C0_SDA;
#if NUM_I2C_BUSES >= 2
        case 1: return DEFAULT_I2C1_SDA;
#endif
#if NUM_I2C_BUSES >= 3
        case 2: return DEFAULT_I2C2_SDA;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default SCL pin for I2C bus
 * @param bus_id I2C bus number (0-2)
 * @return Default SCL pin, or 0xFF if bus not available
 */
inline uint8_t getDefaultI2CSCL(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return DEFAULT_I2C0_SCL;
#if NUM_I2C_BUSES >= 2
        case 1: return DEFAULT_I2C1_SCL;
#endif
#if NUM_I2C_BUSES >= 3
        case 2: return DEFAULT_I2C2_SCL;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default MOSI pin for SPI bus
 * @param bus_id SPI bus number (0-2)
 * @return Default MOSI pin, or 0xFF if bus not available
 */
inline uint8_t getDefaultSPIMOSI(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return DEFAULT_SPI0_MOSI;
#if NUM_SPI_BUSES >= 2
        case 1: return DEFAULT_SPI1_MOSI;
#endif
#if NUM_SPI_BUSES >= 3
        case 2: return DEFAULT_SPI2_MOSI;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default MISO pin for SPI bus
 */
inline uint8_t getDefaultSPIMISO(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return DEFAULT_SPI0_MISO;
#if NUM_SPI_BUSES >= 2
        case 1: return DEFAULT_SPI1_MISO;
#endif
#if NUM_SPI_BUSES >= 3
        case 2: return DEFAULT_SPI2_MISO;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default SCK pin for SPI bus
 */
inline uint8_t getDefaultSPISCK(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return DEFAULT_SPI0_SCK;
#if NUM_SPI_BUSES >= 2
        case 1: return DEFAULT_SPI1_SCK;
#endif
#if NUM_SPI_BUSES >= 3
        case 2: return DEFAULT_SPI2_SCK;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default TX pin for CAN bus
 */
inline uint8_t getDefaultCANTX(uint8_t bus_id) {
    switch (bus_id) {
#if NUM_CAN_BUSES >= 1
        case 0: return DEFAULT_CAN1_TX;
#endif
#if NUM_CAN_BUSES >= 2
        case 1: return DEFAULT_CAN2_TX;
#endif
#if NUM_CAN_BUSES >= 3
        case 2: return DEFAULT_CAN3_TX;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default RX pin for CAN bus
 */
inline uint8_t getDefaultCANRX(uint8_t bus_id) {
    switch (bus_id) {
#if NUM_CAN_BUSES >= 1
        case 0: return DEFAULT_CAN1_RX;
#endif
#if NUM_CAN_BUSES >= 2
        case 1: return DEFAULT_CAN2_RX;
#endif
#if NUM_CAN_BUSES >= 3
        case 2: return DEFAULT_CAN3_RX;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default RX pin for Serial port
 * @param port_id Serial port number (1-8)
 * @return Default RX pin, or 0xFF if port not available
 */
inline uint8_t getDefaultSerialRX(uint8_t port_id) {
    switch (port_id) {
#if NUM_SERIAL_PORTS >= 1
        case 1: return DEFAULT_SERIAL1_RX;
#endif
#if NUM_SERIAL_PORTS >= 2
        case 2: return DEFAULT_SERIAL2_RX;
#endif
#if NUM_SERIAL_PORTS >= 3
        case 3: return DEFAULT_SERIAL3_RX;
#endif
#if NUM_SERIAL_PORTS >= 4
        case 4: return DEFAULT_SERIAL4_RX;
#endif
#if NUM_SERIAL_PORTS >= 5
        case 5: return DEFAULT_SERIAL5_RX;
#endif
#if NUM_SERIAL_PORTS >= 6
        case 6: return DEFAULT_SERIAL6_RX;
#endif
#if NUM_SERIAL_PORTS >= 7
        case 7: return DEFAULT_SERIAL7_RX;
#endif
#if NUM_SERIAL_PORTS >= 8
        case 8: return DEFAULT_SERIAL8_RX;
#endif
        default: return 0xFF;
    }
}

/**
 * Get default TX pin for Serial port
 * @param port_id Serial port number (1-8)
 * @return Default TX pin, or 0xFF if port not available
 */
inline uint8_t getDefaultSerialTX(uint8_t port_id) {
    switch (port_id) {
#if NUM_SERIAL_PORTS >= 1
        case 1: return DEFAULT_SERIAL1_TX;
#endif
#if NUM_SERIAL_PORTS >= 2
        case 2: return DEFAULT_SERIAL2_TX;
#endif
#if NUM_SERIAL_PORTS >= 3
        case 3: return DEFAULT_SERIAL3_TX;
#endif
#if NUM_SERIAL_PORTS >= 4
        case 4: return DEFAULT_SERIAL4_TX;
#endif
#if NUM_SERIAL_PORTS >= 5
        case 5: return DEFAULT_SERIAL5_TX;
#endif
#if NUM_SERIAL_PORTS >= 6
        case 6: return DEFAULT_SERIAL6_TX;
#endif
#if NUM_SERIAL_PORTS >= 7
        case 7: return DEFAULT_SERIAL7_TX;
#endif
#if NUM_SERIAL_PORTS >= 8
        case 8: return DEFAULT_SERIAL8_TX;
#endif
        default: return 0xFF;
    }
}

/**
 * Get serial port name string
 * @param port_id Serial port number (1-8)
 * @return Port name string ("Serial1", etc.) or "Unknown"
 */
inline const char* getSerialPortName(uint8_t port_id) {
    switch (port_id) {
        case 1: return "Serial1";
        case 2: return "Serial2";
        case 3: return "Serial3";
        case 4: return "Serial4";
        case 5: return "Serial5";
        case 6: return "Serial6";
        case 7: return "Serial7";
        case 8: return "Serial8";
        default: return "Unknown";
    }
}

/**
 * Check if a serial port is available on this platform
 * @param port_id Serial port number (1-8)
 * @return true if available
 */
inline bool isSerialPortAvailable(uint8_t port_id) {
    return (port_id >= 1 && port_id <= NUM_SERIAL_PORTS);
}

#endif // BUS_DEFAULTS_H
