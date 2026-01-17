/*
 * bus_manager.cpp - Multi-Bus Initialization Manager Implementation
 */

#include "bus_manager.h"
#include "bus_config.h"
#include "bus_defaults.h"
#include "pin_registry.h"
#include "system_config.h"
#include "message_api.h"  // For msg.debug
#include <Wire.h>
#include <SPI.h>

// ============================================================================
// GLOBAL BUS INSTANCE STORAGE
// ============================================================================

// I2C bus instances (Wire, Wire1, Wire2)
static TwoWire* i2c_instances[3] = {nullptr, nullptr, nullptr};

// SPI bus instances (SPI, SPI1, SPI2)
static SPIClass* spi_instances[3] = {nullptr, nullptr, nullptr};

// CAN bus ready flags (actual FlexCAN objects are in output_can.cpp)
static bool can_bus_ready[3] = {false, false, false};

// ============================================================================
// MAIN INITIALIZATION
// ============================================================================

void initConfiguredBuses() {
    msg.debug.println(F("=== Initializing Configured Buses ==="));

    // Initialize I2C buses
    for (uint8_t i = 0; i < NUM_I2C_BUSES; i++) {
        if (systemConfig.buses.i2c[i].enabled) {
            if (!initI2CBus(i, &systemConfig.buses.i2c[i])) {
                // Mark as disabled if init failed
                systemConfig.buses.i2c[i].enabled = 0;
            }
        }
    }

    // Initialize SPI buses
    for (uint8_t i = 0; i < NUM_SPI_BUSES; i++) {
        if (systemConfig.buses.spi[i].enabled) {
            if (!initSPIBus(i, &systemConfig.buses.spi[i])) {
                systemConfig.buses.spi[i].enabled = 0;
            }
        }
    }

    // Initialize CAN buses
    for (uint8_t i = 0; i < NUM_CAN_BUSES; i++) {
        if (systemConfig.buses.can[i].enabled) {
            if (!initCANBus(i, &systemConfig.buses.can[i])) {
                systemConfig.buses.can[i].enabled = 0;
            }
        }
    }

    msg.debug.println(F("=== Bus Initialization Complete ==="));
    msg.debug.println();
}

// ============================================================================
// I2C BUS INITIALIZATION
// ============================================================================

bool initI2CBus(uint8_t bus_id, I2CBusConfig* config) {
    if (bus_id >= NUM_I2C_BUSES) {
        msg.debug.print(F("✗ I2C bus "));
        msg.debug.print(bus_id);
        msg.debug.println(F(" not available on this platform"));
        return false;
    }

    // Get default pins if config specifies BUS_PIN_DEFAULT (0xFF)
    uint8_t sda = (config->sda_pin == BUS_PIN_DEFAULT) ? getDefaultI2CSDA(bus_id) : config->sda_pin;
    uint8_t scl = (config->scl_pin == BUS_PIN_DEFAULT) ? getDefaultI2CSCL(bus_id) : config->scl_pin;

    // Validate pins are available (only if not using platform defaults)
    if (config->sda_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(sda, PIN_RESERVED, getI2CBusName(bus_id))) {
            return false;
        }
    }
    if (config->scl_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(scl, PIN_RESERVED, getI2CBusName(bus_id))) {
            return false;
        }
    }

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Fixed hardware pins, multiple Wire instances
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(config->clock_speed * 1000UL);
            i2c_instances[0] = &Wire;
            success = true;
            break;
        case 1:
            Wire1.begin();
            Wire1.setClock(config->clock_speed * 1000UL);
            i2c_instances[1] = &Wire1;
            success = true;
            break;
        case 2:
            Wire2.begin();
            Wire2.setClock(config->clock_speed * 1000UL);
            i2c_instances[2] = &Wire2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__) || defined(__MK20DX256__)
    // Teensy 3.x: Remappable I2C pins via setSDA/setSCL
    switch (bus_id) {
        case 0:
            Wire.begin();
            if (config->sda_pin != BUS_PIN_DEFAULT) Wire.setSDA(sda);
            if (config->scl_pin != BUS_PIN_DEFAULT) Wire.setSCL(scl);
            Wire.setClock(config->clock_speed * 1000UL);
            i2c_instances[0] = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin();
            if (config->sda_pin != BUS_PIN_DEFAULT) Wire1.setSDA(sda);
            if (config->scl_pin != BUS_PIN_DEFAULT) Wire1.setSCL(scl);
            Wire1.setClock(config->clock_speed * 1000UL);
            i2c_instances[1] = &Wire1;
            success = true;
            break;
#endif
#if NUM_I2C_BUSES >= 3
        case 2:
            Wire2.begin();
            if (config->sda_pin != BUS_PIN_DEFAULT) Wire2.setSDA(sda);
            if (config->scl_pin != BUS_PIN_DEFAULT) Wire2.setSCL(scl);
            Wire2.setClock(config->clock_speed * 1000UL);
            i2c_instances[2] = &Wire2;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Fully remappable via Wire.begin(sda, scl)
    switch (bus_id) {
        case 0:
            Wire.begin(sda, scl);
            Wire.setClock(config->clock_speed * 1000UL);
            i2c_instances[0] = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin(sda, scl);
            Wire1.setClock(config->clock_speed * 1000UL);
            i2c_instances[1] = &Wire1;
            success = true;
            break;
#endif
    }

#else
    // Generic Arduino (Uno, Mega, etc.): Single Wire bus
    if (bus_id == 0) {
        Wire.begin();
        Wire.setClock(config->clock_speed * 1000UL);
        i2c_instances[0] = &Wire;
        success = true;
    }
#endif

    if (success) {
        // Register pins as reserved in pin registry
        // Use static strings for descriptions to avoid memory issues
        static const char* i2c_sda_desc[] = {"Wire SDA", "Wire1 SDA", "Wire2 SDA"};
        static const char* i2c_scl_desc[] = {"Wire SCL", "Wire1 SCL", "Wire2 SCL"};
        registerPin(sda, PIN_RESERVED, i2c_sda_desc[bus_id]);
        registerPin(scl, PIN_RESERVED, i2c_scl_desc[bus_id]);

        msg.debug.print(F("✓ "));
        msg.debug.print(getI2CBusName(bus_id));
        msg.debug.print(F(" initialized: SDA="));
        msg.debug.print(sda);
        msg.debug.print(F(", SCL="));
        msg.debug.print(scl);
        msg.debug.print(F(", "));
        msg.debug.print(config->clock_speed);
        msg.debug.println(F("kHz"));
    }

    return success;
}

// ============================================================================
// SPI BUS INITIALIZATION
// ============================================================================

bool initSPIBus(uint8_t bus_id, SPIBusConfig* config) {
    if (bus_id >= NUM_SPI_BUSES) {
        msg.debug.print(F("✗ SPI bus "));
        msg.debug.print(bus_id);
        msg.debug.println(F(" not available on this platform"));
        return false;
    }

    // Get default pins if config specifies BUS_PIN_DEFAULT
    uint8_t mosi = (config->mosi_pin == BUS_PIN_DEFAULT) ? getDefaultSPIMOSI(bus_id) : config->mosi_pin;
    uint8_t miso = (config->miso_pin == BUS_PIN_DEFAULT) ? getDefaultSPIMISO(bus_id) : config->miso_pin;
    uint8_t sck = (config->sck_pin == BUS_PIN_DEFAULT) ? getDefaultSPISCK(bus_id) : config->sck_pin;

    // Validate pins are available (only if not using platform defaults)
    if (config->mosi_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(mosi, PIN_RESERVED, getSPIBusName(bus_id))) return false;
    }
    if (config->miso_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(miso, PIN_RESERVED, getSPIBusName(bus_id))) return false;
    }
    if (config->sck_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(sck, PIN_RESERVED, getSPIBusName(bus_id))) return false;
    }

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Multiple SPI instances with fixed pins
    switch (bus_id) {
        case 0:
            SPI.begin();
            spi_instances[0] = &SPI;
            success = true;
            break;
        case 1:
            SPI1.begin();
            spi_instances[1] = &SPI1;
            success = true;
            break;
        case 2:
            SPI2.begin();
            spi_instances[2] = &SPI2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__)
    // Teensy 3.6/3.5: Multiple SPI instances
    switch (bus_id) {
        case 0:
            SPI.begin();
            spi_instances[0] = &SPI;
            success = true;
            break;
#if NUM_SPI_BUSES >= 2
        case 1:
            SPI1.begin();
            spi_instances[1] = &SPI1;
            success = true;
            break;
#endif
#if NUM_SPI_BUSES >= 3
        case 2:
            SPI2.begin();
            spi_instances[2] = &SPI2;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Custom pins via SPI.begin(sck, miso, mosi)
    if (bus_id == 0) {
        SPI.begin(sck, miso, mosi);
        spi_instances[0] = &SPI;
        success = true;
    }

#else
    // Generic Arduino: Single SPI bus
    if (bus_id == 0) {
        SPI.begin();
        spi_instances[0] = &SPI;
        success = true;
    }
#endif

    if (success) {
        // Register pins as reserved in pin registry
        static const char* spi_mosi_desc[] = {"SPI MOSI", "SPI1 MOSI", "SPI2 MOSI"};
        static const char* spi_miso_desc[] = {"SPI MISO", "SPI1 MISO", "SPI2 MISO"};
        static const char* spi_sck_desc[] = {"SPI SCK", "SPI1 SCK", "SPI2 SCK"};
        registerPin(mosi, PIN_RESERVED, spi_mosi_desc[bus_id]);
        registerPin(miso, PIN_RESERVED, spi_miso_desc[bus_id]);
        registerPin(sck, PIN_RESERVED, spi_sck_desc[bus_id]);

        msg.debug.print(F("✓ "));
        msg.debug.print(getSPIBusName(bus_id));
        msg.debug.print(F(" initialized: MOSI="));
        msg.debug.print(mosi);
        msg.debug.print(F(", MISO="));
        msg.debug.print(miso);
        msg.debug.print(F(", SCK="));
        msg.debug.print(sck);
        msg.debug.print(F(", "));
        msg.debug.print(config->clock_speed / 1000000.0, 1);
        msg.debug.println(F("MHz"));
    }

    return success;
}

// ============================================================================
// CAN BUS INITIALIZATION
// ============================================================================

bool initCANBus(uint8_t bus_id, CANBusConfig* config) {
    if (bus_id >= NUM_CAN_BUSES) {
        msg.debug.print(F("✗ CAN bus "));
        msg.debug.print(bus_id);
        msg.debug.println(F(" not available on this platform"));
        return false;
    }

    // Get default pins if config specifies BUS_PIN_DEFAULT
    uint8_t tx = (config->tx_pin == BUS_PIN_DEFAULT) ? getDefaultCANTX(bus_id) : config->tx_pin;
    uint8_t rx = (config->rx_pin == BUS_PIN_DEFAULT) ? getDefaultCANRX(bus_id) : config->rx_pin;

    // Validate pins are available (only if not using platform defaults)
    if (config->tx_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(tx, PIN_RESERVED, getCANBusName(bus_id))) return false;
    }
    if (config->rx_pin != BUS_PIN_DEFAULT) {
        if (!validateNoPinConflict(rx, PIN_RESERVED, getCANBusName(bus_id))) return false;
    }

    // CAN initialization is handled in output_can.cpp since it needs FlexCAN_T4 objects
    // Here we just mark the bus as ready and register pins
    can_bus_ready[bus_id] = true;

    // Register pins as reserved in pin registry
    static const char* can_tx_desc[] = {"CAN1 TX", "CAN2 TX", "CAN3 TX"};
    static const char* can_rx_desc[] = {"CAN1 RX", "CAN2 RX", "CAN3 RX"};
    if (tx != 0xFF) registerPin(tx, PIN_RESERVED, can_tx_desc[bus_id]);
    if (rx != 0xFF) registerPin(rx, PIN_RESERVED, can_rx_desc[bus_id]);

    msg.debug.print(F("✓ "));
    msg.debug.print(getCANBusName(bus_id));
    msg.debug.print(F(" configured: TX="));
    msg.debug.print(tx);
    msg.debug.print(F(", RX="));
    msg.debug.print(rx);
    msg.debug.print(F(", "));
    msg.debug.print(config->baudrate / 1000);
    msg.debug.println(F("kbps"));

    return true;
}

// ============================================================================
// BUS INSTANCE ACCESS
// ============================================================================

TwoWire* getI2CInstance(uint8_t bus_id) {
    if (bus_id >= NUM_I2C_BUSES) return nullptr;
    return i2c_instances[bus_id];
}

SPIClass* getSPIInstance(uint8_t bus_id) {
    if (bus_id >= NUM_SPI_BUSES) return nullptr;
    return spi_instances[bus_id];
}

// ============================================================================
// BUS STATUS QUERIES
// ============================================================================

bool isI2CBusReady(uint8_t bus_id) {
    if (bus_id >= NUM_I2C_BUSES) return false;
    return (i2c_instances[bus_id] != nullptr);
}

bool isSPIBusReady(uint8_t bus_id) {
    if (bus_id >= NUM_SPI_BUSES) return false;
    return (spi_instances[bus_id] != nullptr);
}

bool isCANBusReady(uint8_t bus_id) {
    if (bus_id >= NUM_CAN_BUSES) return false;
    return can_bus_ready[bus_id];
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
