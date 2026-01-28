/*
 * system_config.h - System-Wide Configuration Management
 *
 * Manages runtime configuration for outputs, display, timing intervals,
 * and other system-wide settings. Persisted to EEPROM alongside input configs.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>
#include "bus_config.h"

// EEPROM memory layout constants
#define SYSTEM_CONFIG_MAGIC 0x5343      // "SC" in ASCII
#define SYSTEM_CONFIG_VERSION 6         // Increment when struct changes (v6: added logFilter)
#define SYSTEM_CONFIG_ADDRESS 0x03F0    // Address in EEPROM (after inputs)
#define SYSTEM_CONFIG_SIZE sizeof(SystemConfig)

// Output module IDs
enum OutputID {
    OUTPUT_CAN = 0,
    OUTPUT_REALDASH = 1,
    OUTPUT_SERIAL = 2,
    OUTPUT_SD = 3,
    OUTPUT_ALARM = 4,
#ifdef ENABLE_RELAY_OUTPUT
    OUTPUT_RELAY = 5,
    NUM_OUTPUTS = 6
#else
    NUM_OUTPUTS = 5
#endif
};

// Display types
enum DisplayType {
    DISPLAY_NONE = 0,
    DISPLAY_LCD = 1,
    DISPLAY_OLED = 2
};

#ifdef ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif

// System configuration structure
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 validation
    uint8_t version;             // Schema version
    uint8_t checksum;            // XOR checksum

    // Output Modules (12 bytes)
    uint8_t outputEnabled[NUM_OUTPUTS];    // 5 bytes (bool per output)
    uint16_t outputInterval[NUM_OUTPUTS];  // 10 bytes (interval ms)

    // Display Settings (7 bytes)
    uint8_t displayEnabled;      // Display on/off (bool)
    uint8_t displayType;         // LCD/OLED/None (DisplayType enum)
    uint8_t lcdI2CAddress;       // I2C address (default 0x27)
    uint8_t defaultTempUnits;    // Unit index: 0=Celsius, 1=Fahrenheit
    uint8_t defaultPressUnits;   // Unit index: 2=Bar, 3=PSI, 4=kPa
    uint8_t defaultElevUnits;    // Unit index: 9=Meters, 10=Feet
    uint8_t defaultSpeedUnits;   // Unit index: 11=KPH, 12=MPH

    // Timing Intervals (8 bytes)
    uint16_t sensorReadInterval;
    uint16_t alarmCheckInterval;
    uint16_t lcdUpdateInterval;
    uint16_t reserved1;

    // Hardware Pins (8 bytes)
    uint8_t modeButtonPin;
    uint8_t buzzerPin;
    uint8_t canCSPin;
    uint8_t canIntPin;
    uint8_t sdCSPin;
    uint8_t testModePin;
    uint16_t reserved2;

    // Physical Constants (4 bytes)
    float seaLevelPressure;      // hPa for altitude

    // Transport Router Configuration (16 bytes) - NEW in v4
    struct {
        uint8_t control_primary;     // TransportID for CONTROL plane
        uint8_t control_secondary;   // Multi-cast target or TRANSPORT_NONE
        uint8_t data_primary;        // TransportID for DATA plane
        uint8_t data_secondary;      // Multi-cast target or TRANSPORT_NONE
        uint8_t debug_primary;       // TransportID for DEBUG plane
        uint8_t debug_secondary;     // Multi-cast target or TRANSPORT_NONE
        uint8_t bt_type;             // BluetoothType enum (0=none)
        uint8_t bt_auth_required;    // 0=disabled, 1=enabled
        uint16_t bt_pin;             // 4-digit PIN (0=not set)
        uint8_t reserved_router[6];  // Future expansion
    } router;

#ifdef ENABLE_RELAY_OUTPUT
    // Relay Configuration (32 bytes) - NEW in v5
    RelayConfig relays[MAX_RELAYS];  // 2 relays × 16 bytes = 32 bytes
#endif

    // Bus Configuration (16 bytes) - Simplified "pick one" model
    BusConfig buses;

    // Serial Port Configuration (16 bytes) - Which serial ports are enabled
    SerialPortConfig serial;

    // Log Filter Configuration (12 bytes) - NEW in v6
    struct {
        uint8_t control_level;    // LogLevel for CONTROL plane
        uint8_t data_level;       // LogLevel for DATA plane
        uint8_t debug_level;      // LogLevel for DEBUG plane (default: INFO)
        uint32_t enabledTags;     // 32-bit bitmap for tag filtering (all enabled by default)
        uint8_t reserved[5];      // Future expansion
    } logFilter;
};

// Global system config instance
extern SystemConfig systemConfig;

// Initialization and persistence
void initSystemConfig();         // Load from EEPROM or set defaults
bool saveSystemConfig();         // Save to EEPROM
bool loadSystemConfig();         // Load from EEPROM
void resetSystemConfig();        // Reset to defaults
uint8_t calculateChecksum(SystemConfig* cfg);
void printSystemStatus();
void registerSystemPins();       // Register system pins in pin registry

#endif // SYSTEM_CONFIG_H
