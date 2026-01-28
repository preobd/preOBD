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
// MAIN INITIALIZATION
// ============================================================================

void initConfiguredBuses() {

    // Initialize the active I2C bus
    if (!initI2CBus(systemConfig.buses.active_i2c, systemConfig.buses.i2c_clock)) {
        // Fall back to bus 0 if requested bus fails
        if (systemConfig.buses.active_i2c != 0) {
            msg.debug.warn(TAG_I2C, "Falling back to Wire (bus 0)");
            initI2CBus(0, systemConfig.buses.i2c_clock);
        }
    }

    // Initialize the active SPI bus
    if (!initSPIBus(systemConfig.buses.active_spi, systemConfig.buses.spi_clock)) {
        // Fall back to bus 0 if requested bus fails
        if (systemConfig.buses.active_spi != 0) {
            msg.debug.warn(TAG_SPI, "Falling back to SPI (bus 0)");
            initSPIBus(0, systemConfig.buses.spi_clock);
        }
    }

    // Initialize the active CAN bus
    if (!initCANBus(systemConfig.buses.active_can, systemConfig.buses.can_baudrate)) {
        // Fall back to bus 0 if requested bus fails
        if (systemConfig.buses.active_can != 0) {
            msg.debug.warn(TAG_CAN, "Falling back to CAN1 (bus 0)");
            initCANBus(0, systemConfig.buses.can_baudrate);
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
    uint8_t sda = getDefaultI2CSDA(bus_id);
    uint8_t scl = getDefaultI2CSCL(bus_id);

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

        // Register pins as reserved in pin registry
        static const char* i2c_desc[] = {"Wire", "Wire1", "Wire2"};
        registerPin(sda, PIN_RESERVED, i2c_desc[bus_id]);
        registerPin(scl, PIN_RESERVED, i2c_desc[bus_id]);

        msg.debug.info(TAG_I2C, "I2C bus initialized");
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
    uint8_t mosi = getDefaultSPIMOSI(bus_id);
    uint8_t miso = getDefaultSPIMISO(bus_id);
    uint8_t sck = getDefaultSPISCK(bus_id);

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

        // Register pins as reserved in pin registry
        static const char* spi_desc[] = {"SPI", "SPI1", "SPI2"};
        registerPin(mosi, PIN_RESERVED, spi_desc[bus_id]);
        registerPin(miso, PIN_RESERVED, spi_desc[bus_id]);
        registerPin(sck, PIN_RESERVED, spi_desc[bus_id]);

        msg.debug.info(TAG_SPI, "SPI bus initialized");
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

    // Get default pins for this bus
    uint8_t tx = getDefaultCANTX(bus_id);
    uint8_t rx = getDefaultCANRX(bus_id);

    // CAN initialization is handled in output_can.cpp since it needs FlexCAN_T4 objects
    // Here we just mark the bus as ready and register pins
    active_can_id = bus_id;
    can_ready = true;

    // Register pins as reserved in pin registry
    static const char* can_desc[] = {"CAN1", "CAN2", "CAN3"};
    if (tx != 0xFF) registerPin(tx, PIN_RESERVED, can_desc[bus_id]);
    if (rx != 0xFF) registerPin(rx, PIN_RESERVED, can_desc[bus_id]);

    msg.debug.info(TAG_CAN, "CAN bus initialized");

    return true;
#endif
}

// ============================================================================
// ACTIVE BUS ACCESS
// ============================================================================

TwoWire* getActiveI2C() {
    return active_i2c ? active_i2c : &Wire;
}

SPIClass* getActiveSPI() {
    return active_spi ? active_spi : &SPI;
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
    uint8_t bus_id = systemConfig.buses.active_i2c;
    msg.control.println();
    msg.control.println(F("=== I2C Bus Configuration ==="));
    msg.control.print(F("Active: "));
    msg.control.print(getI2CBusName(bus_id));
    msg.control.print(F(" (SDA="));
    msg.control.print(getDefaultI2CSDA(bus_id));
    msg.control.print(F(", SCL="));
    msg.control.print(getDefaultI2CSCL(bus_id));
    msg.control.print(F(") @ "));
    msg.control.print(systemConfig.buses.i2c_clock);
    msg.control.println(F("kHz"));
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
    uint8_t bus_id = systemConfig.buses.active_can;
    msg.control.print(F("Active: "));
    msg.control.print(getCANBusName(bus_id));
    msg.control.print(F(" (TX="));
    msg.control.print(getDefaultCANTX(bus_id));
    msg.control.print(F(", RX="));
    msg.control.print(getDefaultCANRX(bus_id));
    msg.control.print(F(") @ "));
    msg.control.print(systemConfig.buses.can_baudrate / 1000);
    msg.control.println(F("kbps"));
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

