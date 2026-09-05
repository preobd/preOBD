/*
 * bus_manager.cpp - Bus Initialization Manager Implementation
 *
 * Simplified "pick one" model - each bus type has one active instance.
 */

#include "bus_manager.h"
#include "bus_config.h"
#include "bus_defaults.h"
#include "pin_registry.h"
#include "system_config.h"
#include "message_api.h"
#include "log_tags.h"
#include <Wire.h>
#include <SPI.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Active bus instances
static TwoWire* active_i2c = nullptr;
static SPIClass* active_spi = nullptr;

// Active bus IDs (for reference)
static uint8_t active_i2c_id = 0;
static uint8_t active_spi_id = 0;
static uint8_t active_can_id = 0;

// CAN ready flag (actual FlexCAN objects are in output_can.cpp)
static bool can_ready = false;

// ============================================================================
// PIN CLAIMING
// ============================================================================

// A bus rests on "bus up implies its pins are reserved" — that is what stops an
// input being assigned to a pin a bus is clocking (#351). registerPin() is
// first-writer-wins, and the overlaps are real on stock hardware (Teensy 4.x
// I2C1 17/16 vs Serial4 16/17, and serial ports register first), so a caller
// that ignores the result publishes a bus the registry attributes to someone
// else. Every bus therefore claims its pins BEFORE bringing the peripheral up,
// all or nothing, and refuses to start if the claim is denied.
//
// One shared format string: msg.debug.* format strings are RAM-resident on AVR,
// so three per-bus variants would cost real bytes on the 8 KB mega.
static const char BUS_PINS_BUSY[] = "%s not started: pins already claimed";
static const char BUS_STARTED[]   = "%s up";

static bool claimBusPins(const uint8_t* pins, uint8_t count,
                         const char* tag, const char* bus_name) {
    if (reservePins(pins, count, bus_name)) return true;

    msg.debug.error(tag, BUS_PINS_BUSY, bus_name);
    msg.control.print(F("ERROR: "));
    msg.control.print(bus_name);
    msg.control.println(F(" not started - a pin it needs is already in use"));
    msg.control.println(F("  SYSTEM PINS shows the owner; reconfigure one of them."));
    return false;
}

// ============================================================================
// MAIN INITIALIZATION
// ============================================================================

void initConfiguredBuses() {

    // Initialize the active I2C bus (only if explicitly selected)
    if (systemConfig.buses.active_i2c != 0xFF) {
        if (!initI2CBus(systemConfig.buses.active_i2c, systemConfig.buses.i2c_clock)) {
            if (systemConfig.buses.active_i2c != 0) {
                msg.debug.warn(TAG_I2C, "Falling back to Wire (bus 0)");
                initI2CBus(0, systemConfig.buses.i2c_clock);
            }
        }
    }

    // Initialize the active SPI bus (only if explicitly selected)
    if (systemConfig.buses.active_spi != 0xFF) {
        if (!initSPIBus(systemConfig.buses.active_spi, systemConfig.buses.spi_clock)) {
            if (systemConfig.buses.active_spi != 0) {
                msg.debug.warn(TAG_SPI, "Falling back to SPI (bus 0)");
                initSPIBus(0, systemConfig.buses.spi_clock);
            }
        }
    }

    // Initialize CAN output bus if enabled
    if (systemConfig.buses.can_output_enabled && systemConfig.buses.output_can_bus != 0xFF) {
        if (!initCANBus(systemConfig.buses.output_can_bus, systemConfig.buses.can_output_baudrate)) {
            // Fall back to bus 0 if requested bus fails
            if (systemConfig.buses.output_can_bus != 0) {
                msg.debug.warn(TAG_CAN, "CAN output: Falling back to CAN1 (bus 0)");
                initCANBus(0, systemConfig.buses.can_output_baudrate);
            }
        }
    }

    // Initialize CAN input bus if enabled
    // Supports two modes:
    // 1. SHARED BUS: input_can_bus == output_can_bus (same physical bus)
    //    - Both input and output use same CAN peripheral (e.g., both on CAN1)
    //    - Common on single-bus platforms (AVR with MCP2515, ESP32 with TWAI)
    //    - HAL handles concurrent access safely (non-blocking reads, queued writes)
    // 2. DUAL BUS: input_can_bus != output_can_bus (separate physical buses)
    //    - Input on CAN1, output on CAN2 (Teensy 3.6/4.x only)
    //    - Allows isolation of sensor import from OBD-II output traffic
    if (systemConfig.buses.can_input_mode != CAN_INPUT_OFF && systemConfig.buses.input_can_bus != 0xFF) {
        // Only initialize if different bus than output (avoid double-init in shared mode)
        if (systemConfig.buses.input_can_bus != systemConfig.buses.output_can_bus) {
            if (!initCANBus(systemConfig.buses.input_can_bus, systemConfig.buses.can_input_baudrate)) {
                msg.debug.warn(TAG_CAN, "CAN input: Failed to initialize bus %d", systemConfig.buses.input_can_bus);
            }
        } else {
            // Shared bus mode - already initialized by output, just log status
            msg.debug.info(TAG_CAN, "CAN input sharing bus with output (bus %d)", systemConfig.buses.input_can_bus);
        }
    }
}

// ============================================================================
// I2C BUS INITIALIZATION
// ============================================================================

bool initI2CBus(uint8_t bus_id, uint16_t clock_khz) {
    if (bus_id >= NUM_I2C_BUSES) {
        msg.debug.error(TAG_I2C, "I2C bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus
    const uint8_t i2c_pins[2] = { getDefaultI2CSDA(bus_id), getDefaultI2CSCL(bus_id) };
    static const char* const i2c_desc[] = {"Wire", "Wire1", "Wire2"};

    // Claim SDA/SCL first: if the copper is already someone else's, the bus must
    // not come up at all. Refusing here rather than after Wire.begin() also means
    // there is nothing to un-begin — TwoWire has no portable end().
    if (!claimBusPins(i2c_pins, 2, TAG_I2C, i2c_desc[bus_id])) return false;

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Fixed hardware pins, multiple Wire instances
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
        case 2:
            Wire2.begin();
            Wire2.setClock(clock_khz * 1000UL);
            active_i2c = &Wire2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__) || defined(__MK20DX256__)
    // Teensy 3.x: Multiple Wire instances
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
#endif
#if NUM_I2C_BUSES >= 3
        case 2:
            Wire2.begin();
            Wire2.setClock(clock_khz * 1000UL);
            active_i2c = &Wire2;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Wire.begin() uses default pins
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
#endif
    }

#else
    // Generic Arduino (Uno, Mega, etc.): Single Wire bus
    if (bus_id == 0) {
        Wire.begin();
        Wire.setClock(clock_khz * 1000UL);
        active_i2c = &Wire;
        success = true;
    }
#endif

    if (success) {
        active_i2c_id = bus_id;
        msg.debug.info(TAG_I2C, BUS_STARTED, i2c_desc[bus_id]);
    } else {
        // Unreachable bus id for this platform: hand the pins back rather than
        // leaving them reserved to a bus that never started.
        releasePins(i2c_pins, 2, i2c_desc[bus_id]);
    }

    return success;
}

// ============================================================================
// SPI BUS INITIALIZATION
// ============================================================================

bool initSPIBus(uint8_t bus_id, uint32_t clock_hz) {
    if (bus_id >= NUM_SPI_BUSES) {
        msg.debug.error(TAG_SPI, "SPI bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus
    const uint8_t spi_pins[3] = { getDefaultSPIMOSI(bus_id),
                                  getDefaultSPIMISO(bus_id),
                                  getDefaultSPISCK(bus_id) };
    static const char* const spi_desc[] = {"SPI", "SPI1", "SPI2"};

    // Same contract as I2C: claim the bus pins before SPI.begin() drives them.
    if (!claimBusPins(spi_pins, 3, TAG_SPI, spi_desc[bus_id])) return false;

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Multiple SPI instances with fixed pins
    switch (bus_id) {
        case 0:
            SPI.begin();
            active_spi = &SPI;
            success = true;
            break;
        case 1:
            SPI1.begin();
            active_spi = &SPI1;
            success = true;
            break;
        case 2:
            SPI2.begin();
            active_spi = &SPI2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__)
    // Teensy 3.6/3.5: Multiple SPI instances
    switch (bus_id) {
        case 0:
            SPI.begin();
            active_spi = &SPI;
            success = true;
            break;
#if NUM_SPI_BUSES >= 2
        case 1:
            SPI1.begin();
            active_spi = &SPI1;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Single SPI bus with default pins
    if (bus_id == 0) {
        SPI.begin();
        active_spi = &SPI;
        success = true;
    }

#else
    // Generic Arduino: Single SPI bus
    if (bus_id == 0) {
        SPI.begin();
        active_spi = &SPI;
        success = true;
    }
#endif

    if (success) {
        active_spi_id = bus_id;
        msg.debug.info(TAG_SPI, BUS_STARTED, spi_desc[bus_id]);
    } else {
        releasePins(spi_pins, 3, spi_desc[bus_id]);
    }

    return success;
}

// ============================================================================
// CAN BUS INITIALIZATION
// ============================================================================

bool initCANBus(uint8_t bus_id, uint32_t baudrate) {
#if NUM_CAN_BUSES == 0
    (void)bus_id;
    (void)baudrate;
    msg.debug.warn(TAG_CAN, "CAN not available on this platform");
    return false;
#else
    if (bus_id >= NUM_CAN_BUSES) {
        msg.debug.error(TAG_CAN, "CAN bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus. 0xFF entries (SPI-attached controllers
    // have no fixed TX/RX) are skipped by the claim.
    const uint8_t can_pins[2] = { getDefaultCANTX(bus_id), getDefaultCANRX(bus_id) };
    static const char* const can_desc[] = {"CAN1", "CAN2", "CAN3"};

    // Claim before declaring the bus ready: can_ready is what the rest of the
    // firmware reads as "this bus is mine", so it must not be set over pins the
    // registry attributes to another function.
    if (!claimBusPins(can_pins, 2, TAG_CAN, can_desc[bus_id])) return false;

    // CAN initialization is handled in output_can.cpp since it needs FlexCAN_T4 objects
    // Here we just mark the bus as ready
    active_can_id = bus_id;
    can_ready = true;

    msg.debug.info(TAG_CAN, BUS_STARTED, can_desc[bus_id]);

    return true;
#endif
}

// ============================================================================
// ACTIVE BUS ACCESS
// ============================================================================

TwoWire* getActiveI2C() {
    return active_i2c;
}

TwoWire* acquireI2C() {
    // Already up — initI2CBus() reserved SDA/SCL when it brought the bus up.
    if (active_i2c) return active_i2c;

    // 0xFF ("no explicit selection") is not "forbidden": clients such as the LCD
    // have always had a working bus here, they simply used to Wire.begin() it
    // themselves and leave the manager none the wiser. Bring up the platform
    // default on demand so ownership — and pin reservation — stays in one place.
    uint8_t bus_id = (systemConfig.buses.active_i2c != 0xFF)
                         ? systemConfig.buses.active_i2c
                         : (uint8_t)DEFAULT_I2C_BUS;

    if (!initI2CBus(bus_id, systemConfig.buses.i2c_clock)) {
        msg.debug.error(TAG_I2C, "I2C acquire failed");
        return nullptr;
    }
    return active_i2c;
}

SPIClass* getActiveSPI() {
    return active_spi;
}

uint8_t getActiveI2CId() {
    return active_i2c_id;
}

uint8_t getActiveSPIId() {
    return active_spi_id;
}

uint8_t getActiveCANId() {
    return active_can_id;
}

// ============================================================================
// BUS NAME HELPERS
// ============================================================================

const char* getI2CBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "Wire";
        case 1: return "Wire1";
        case 2: return "Wire2";
        default: return "I2C?";
    }
}

const char* getSPIBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "SPI";
        case 1: return "SPI1";
        case 2: return "SPI2";
        default: return "SPI?";
    }
}

const char* getCANBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "CAN1";
        case 1: return "CAN2";
        case 2: return "CAN3";
        default: return "CAN?";
    }
}

//=============================================================================
// BUS Command helpers
//=============================================================================

// Helper function to display I2C bus configuration
void displayI2CStatus() {
    // Report the bus that is actually up, not just the stored selection: a client
    // may have acquired the platform default on demand (#351), and a status line
    // reading "NONE" while the peripheral is clocking is exactly the confusion
    // this command exists to resolve.
    uint8_t bus_id = active_i2c ? active_i2c_id : systemConfig.buses.active_i2c;
    msg.control.println();
    msg.control.println(F("=== I2C Bus Configuration ==="));
    msg.control.print(F("Active: "));
    if (bus_id == 0xFF) {
        msg.control.println(F("NONE (not initialized — use 'BUS I2C 0' to enable)"));
    } else {
        msg.control.print(getI2CBusName(bus_id));
        msg.control.print(F(" (SDA="));
        msg.control.print(getDefaultI2CSDA(bus_id));
        msg.control.print(F(", SCL="));
        msg.control.print(getDefaultI2CSCL(bus_id));
        msg.control.print(F(") @ "));
        msg.control.print(systemConfig.buses.i2c_clock);
        msg.control.println(F("kHz"));
        if (active_i2c && systemConfig.buses.active_i2c == 0xFF) {
            msg.control.println(F("  (acquired on demand — no explicit 'BUS I2C <n>' selection)"));
        }
    }
    msg.control.print(F("Available buses: "));
    for (uint8_t i = 0; i < NUM_I2C_BUSES; i++) {
        if (i > 0) msg.control.print(F(", "));
        msg.control.print(i);
        msg.control.print(F("="));
        msg.control.print(getI2CBusName(i));
    }
    msg.control.println();
}

// Helper function to display SPI bus configuration
void displaySPIStatus() {
    uint8_t bus_id = systemConfig.buses.active_spi;
    msg.control.println();
    msg.control.println(F("=== SPI Bus Configuration ==="));
    msg.control.print(F("Active: "));
    if (bus_id == 0xFF) {
        msg.control.println(F("NONE (not initialized — use 'BUS SPI 0' to enable)"));
    } else {
        msg.control.print(getSPIBusName(bus_id));
        msg.control.print(F(" (MOSI="));
        msg.control.print(getDefaultSPIMOSI(bus_id));
        msg.control.print(F(", MISO="));
        msg.control.print(getDefaultSPIMISO(bus_id));
        msg.control.print(F(", SCK="));
        msg.control.print(getDefaultSPISCK(bus_id));
        msg.control.print(F(") @ "));
        msg.control.print(systemConfig.buses.spi_clock / 1000000.0, 1);
        msg.control.println(F("MHz"));
    }
    msg.control.print(F("Available buses: "));
    for (uint8_t i = 0; i < NUM_SPI_BUSES; i++) {
        if (i > 0) msg.control.print(F(", "));
        msg.control.print(i);
        msg.control.print(F("="));
        msg.control.print(getSPIBusName(i));
    }
    msg.control.println();
}

// Helper function to display CAN bus configuration
void displayCANStatus() {
    msg.control.println();
    msg.control.println(F("=== CAN Bus Configuration ==="));
#if NUM_CAN_BUSES > 0
    // Display input bus
    msg.control.print(F("Input:  "));
    if (systemConfig.buses.input_can_bus != 0xFF && systemConfig.buses.can_input_mode != CAN_INPUT_OFF) {
        msg.control.print(getCANBusName(systemConfig.buses.input_can_bus));
        if (systemConfig.buses.can_input_mode == CAN_INPUT_LISTEN) {
            msg.control.print(F(" (LISTEN) @ "));
        } else {
            msg.control.print(F(" (NORMAL) @ "));
        }
        msg.control.print(systemConfig.buses.can_input_baudrate / 1000);
        msg.control.print(F("kbps"));
    } else {
        msg.control.print(F("DISABLED"));
    }
    msg.control.println();

    // Display output bus
    msg.control.print(F("Output: "));
    if (systemConfig.buses.output_can_bus != 0xFF && systemConfig.buses.can_output_enabled) {
        msg.control.print(getCANBusName(systemConfig.buses.output_can_bus));
        msg.control.print(F(" (ENABLED) @ "));
        msg.control.print(systemConfig.buses.can_output_baudrate / 1000);
        msg.control.print(F("kbps"));
    } else {
        msg.control.print(F("DISABLED"));
    }
    msg.control.println();
    msg.control.print(F("Available buses: "));
    for (uint8_t i = 0; i < NUM_CAN_BUSES; i++) {
        if (i > 0) msg.control.print(F(", "));
        msg.control.print(i);
        msg.control.print(F("="));
        msg.control.print(getCANBusName(i));
    }
    msg.control.println();
#else
    msg.control.println(F("No CAN buses available on this platform"));
#endif
}

