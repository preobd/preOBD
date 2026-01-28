/*
 * serial_manager.cpp - Serial Port Manager Implementation
 */

#include "serial_manager.h"
#include "bus_config.h"
#include "bus_defaults.h"
#include "pin_registry.h"
#include "system_config.h"
#include "message_api.h"
#include "log_tags.h"

// ============================================================================
// SERIAL CONFIG HELPERS
// ============================================================================

static inline bool isSerialPortEnabled(const SerialPortConfig* config, uint8_t port_id) {
    if (port_id < 1 || port_id > 8) return false;
    return (config->enabled_mask & (1 << (port_id - 1))) != 0;
}

static inline void setSerialPortEnabled(SerialPortConfig* config, uint8_t port_id, bool enabled) {
    if (port_id < 1 || port_id > 8) return;
    if (enabled) {
        config->enabled_mask |= (1 << (port_id - 1));
    } else {
        config->enabled_mask &= ~(1 << (port_id - 1));
    }
}

static inline void setSerialPortBaudrate(SerialPortConfig* config, uint8_t port_id, uint8_t baud_index) {
    if (port_id < 1 || port_id > 8) return;
    config->baudrate_index[port_id - 1] = baud_index;
}

// ============================================================================
// BAUD RATE LOOKUP TABLE
// ============================================================================

static const uint32_t BAUD_RATES[] = {
    9600,    // BAUD_9600   = 0
    19200,   // BAUD_19200  = 1
    38400,   // BAUD_38400  = 2
    57600,   // BAUD_57600  = 3
    115200,  // BAUD_115200 = 4
    230400,  // BAUD_230400 = 5
    460800,  // BAUD_460800 = 6
    921600   // BAUD_921600 = 7
};

static const char* BAUD_STRINGS[] = {
    "9600",
    "19200",
    "38400",
    "57600",
    "115200",
    "230400",
    "460800",
    "921600"
};

uint32_t getBaudRateFromIndex(uint8_t index) {
    if (index >= NUM_BAUD_RATES) return BAUD_RATES[BAUD_115200];
    return BAUD_RATES[index];
}

uint8_t getBaudRateIndex(uint32_t baudrate) {
    for (uint8_t i = 0; i < NUM_BAUD_RATES; i++) {
        if (BAUD_RATES[i] == baudrate) return i;
    }
    return BAUD_115200;  // Default
}

const char* getBaudRateString(uint8_t index) {
    if (index >= NUM_BAUD_RATES) return BAUD_STRINGS[BAUD_115200];
    return BAUD_STRINGS[index];
}

// ============================================================================
// SERIAL PORT STATE
// ============================================================================

// Track which ports have been initialized at runtime
static uint8_t active_ports_mask = 0;

// ============================================================================
// MAIN INITIALIZATION
// ============================================================================

void initConfiguredSerialPorts() {
    // Initialize each enabled serial port
    for (uint8_t port_id = 1; port_id <= NUM_SERIAL_PORTS; port_id++) {
        if (isSerialPortEnabled(&systemConfig.serial, port_id)) {
            uint32_t baudrate = getBaudRateFromIndex(systemConfig.serial.baudrate_index[port_id - 1]);
            initSerialPort(port_id, baudrate);
        }
    }
}

// ============================================================================
// SERIAL PORT INITIALIZATION
// ============================================================================

bool initSerialPort(uint8_t port_id, uint32_t baudrate) {
    // Validate port ID
    if (port_id < 1 || port_id > NUM_SERIAL_PORTS) {
        msg.debug.error(TAG_SERIAL, "Serial%d not available on this platform", port_id);
        return false;
    }

    // Get pins for this port
    uint8_t rx = getDefaultSerialRX(port_id);
    uint8_t tx = getDefaultSerialTX(port_id);

    // Check for pin conflicts
    if (!validateNoPinConflict(rx, PIN_RESERVED, getSerialPortName(port_id))) {
        return false;
    }
    if (!validateNoPinConflict(tx, PIN_RESERVED, getSerialPortName(port_id))) {
        return false;
    }

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Serial1-Serial7 (or Serial8 on 4.1)
    switch (port_id) {
        case 1: Serial1.begin(baudrate); success = true; break;
        case 2: Serial2.begin(baudrate); success = true; break;
        case 3: Serial3.begin(baudrate); success = true; break;
        case 4: Serial4.begin(baudrate); success = true; break;
        case 5: Serial5.begin(baudrate); success = true; break;
        case 6: Serial6.begin(baudrate); success = true; break;
        case 7: Serial7.begin(baudrate); success = true; break;
#if defined(ARDUINO_TEENSY41)
        case 8: Serial8.begin(baudrate); success = true; break;
#endif
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__)
    // Teensy 3.5/3.6: Serial1-Serial6
    switch (port_id) {
        case 1: Serial1.begin(baudrate); success = true; break;
        case 2: Serial2.begin(baudrate); success = true; break;
        case 3: Serial3.begin(baudrate); success = true; break;
        case 4: Serial4.begin(baudrate); success = true; break;
        case 5: Serial5.begin(baudrate); success = true; break;
        case 6: Serial6.begin(baudrate); success = true; break;
    }

#elif defined(__MK20DX256__)
    // Teensy 3.1/3.2: Serial1-Serial3
    switch (port_id) {
        case 1: Serial1.begin(baudrate); success = true; break;
        case 2: Serial2.begin(baudrate); success = true; break;
        case 3: Serial3.begin(baudrate); success = true; break;
    }

#elif defined(ESP32)
    // ESP32: Serial1-Serial2
    switch (port_id) {
        case 1: Serial1.begin(baudrate); success = true; break;
        case 2: Serial2.begin(baudrate); success = true; break;
    }

#elif defined(__AVR_ATmega2560__)
    // Arduino Mega: Serial1-Serial3
    switch (port_id) {
        case 1: Serial1.begin(baudrate); success = true; break;
        case 2: Serial2.begin(baudrate); success = true; break;
        case 3: Serial3.begin(baudrate); success = true; break;
    }

#endif

    if (success) {
        // Mark port as active
        active_ports_mask |= (1 << (port_id - 1));

        // Register pins
        registerPin(rx, PIN_RESERVED, getSerialPortName(port_id));
        registerPin(tx, PIN_RESERVED, getSerialPortName(port_id));

        msg.debug.info(TAG_SERIAL, "Serial%d initialized @ %lu baud", port_id, baudrate);
    }

    return success;
}

bool enableSerialPort(uint8_t port_id, uint8_t baud_index) {
    if (port_id < 1 || port_id > NUM_SERIAL_PORTS) return false;
    if (baud_index >= NUM_BAUD_RATES) baud_index = BAUD_115200;

    // Update config
    setSerialPortEnabled(&systemConfig.serial, port_id, true);
    setSerialPortBaudrate(&systemConfig.serial, port_id, baud_index);

    // Initialize the port
    return initSerialPort(port_id, getBaudRateFromIndex(baud_index));
}

bool disableSerialPort(uint8_t port_id) {
    if (port_id < 1 || port_id > NUM_SERIAL_PORTS) return false;

    // Update config
    setSerialPortEnabled(&systemConfig.serial, port_id, false);

    // Mark port as inactive
    active_ports_mask &= ~(1 << (port_id - 1));

    // Release pins from registry
    uint8_t rx = getDefaultSerialRX(port_id);
    uint8_t tx = getDefaultSerialTX(port_id);
    unregisterPin(rx);
    unregisterPin(tx);

    // Note: We don't call Serial.end() as it might be in use by transport layer

    msg.debug.info(TAG_SERIAL, "Serial%d disabled", port_id);

    return true;
}

// ============================================================================
// SERIAL PORT ACCESS
// ============================================================================

Stream* getSerialPort(uint8_t port_id) {
#if defined(__IMXRT1062__)
    switch (port_id) {
        case 1: return &Serial1;
        case 2: return &Serial2;
        case 3: return &Serial3;
        case 4: return &Serial4;
        case 5: return &Serial5;
        case 6: return &Serial6;
        case 7: return &Serial7;
#if defined(ARDUINO_TEENSY41)
        case 8: return &Serial8;
#endif
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__)
    switch (port_id) {
        case 1: return &Serial1;
        case 2: return &Serial2;
        case 3: return &Serial3;
        case 4: return &Serial4;
        case 5: return &Serial5;
        case 6: return &Serial6;
    }

#elif defined(__MK20DX256__)
    switch (port_id) {
        case 1: return &Serial1;
        case 2: return &Serial2;
        case 3: return &Serial3;
    }

#elif defined(ESP32)
    switch (port_id) {
        case 1: return &Serial1;
        case 2: return &Serial2;
    }

#elif defined(__AVR_ATmega2560__)
    switch (port_id) {
        case 1: return &Serial1;
        case 2: return &Serial2;
        case 3: return &Serial3;
    }

#endif

    return nullptr;
}

bool isSerialPortActive(uint8_t port_id) {
    if (port_id < 1 || port_id > 8) return false;
    return (active_ports_mask & (1 << (port_id - 1))) != 0;
}

// ============================================================================
// SERIAL PORT STATUS
// ============================================================================

void displaySerialStatus() {
    msg.control.println();
    msg.control.println(F("=== Serial Port Configuration ==="));
    msg.control.print(F("Platform supports Serial1-Serial"));
    msg.control.println(NUM_SERIAL_PORTS);
    msg.control.println();

    // Show each port's status
    for (uint8_t port_id = 1; port_id <= NUM_SERIAL_PORTS; port_id++) {
        bool enabled = isSerialPortEnabled(&systemConfig.serial, port_id);
        uint8_t baud_idx = systemConfig.serial.baudrate_index[port_id - 1];

        msg.control.print(F("Serial"));
        msg.control.print(port_id);
        msg.control.print(F(": "));

        if (enabled) {
            msg.control.print(F("ENABLED @ "));
            msg.control.print(getBaudRateString(baud_idx));
            msg.control.print(F(" baud"));
        } else {
            msg.control.print(F("disabled"));
        }

        msg.control.print(F(" (RX="));
        msg.control.print(getDefaultSerialRX(port_id));
        msg.control.print(F(", TX="));
        msg.control.print(getDefaultSerialTX(port_id));
        msg.control.println(F(")"));
    }
    msg.control.println();
}

void displaySerialPortStatus(uint8_t port_id) {
    if (port_id < 1 || port_id > NUM_SERIAL_PORTS) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port_id);
        msg.control.println(F(" not available on this platform"));
        return;
    }

    bool enabled = isSerialPortEnabled(&systemConfig.serial, port_id);
    uint8_t baud_idx = systemConfig.serial.baudrate_index[port_id - 1];

    msg.control.println();
    msg.control.print(F("Serial"));
    msg.control.print(port_id);
    msg.control.println(F(":"));
    msg.control.print(F("  Status: "));
    msg.control.println(enabled ? F("ENABLED") : F("disabled"));
    msg.control.print(F("  Baud:   "));
    msg.control.print(getBaudRateString(baud_idx));
    msg.control.println(F(" bps"));
    msg.control.print(F("  RX pin: "));
    msg.control.println(getDefaultSerialRX(port_id));
    msg.control.print(F("  TX pin: "));
    msg.control.println(getDefaultSerialTX(port_id));
}
