/*
 * system_config.h - System-Wide Configuration Management
 *
 * Manages runtime configuration for outputs, display, timing intervals,
 * and other system-wide settings. Persisted to EEPROM alongside input configs.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>
#include "sensor_types.h"  // For Units enum (CELSIUS, BAR, FEET, etc.)

// EEPROM memory layout constants
#define SYSTEM_CONFIG_MAGIC 0x5343      // "SC" in ASCII
#define SYSTEM_CONFIG_VERSION 1         // Increment when struct changes
#define SYSTEM_CONFIG_ADDRESS 0x03F0    // Address in EEPROM (after inputs)
#define SYSTEM_CONFIG_SIZE sizeof(SystemConfig)

// Output module IDs
enum OutputID {
    OUTPUT_CAN = 0,
    OUTPUT_REALDASH = 1,
    OUTPUT_SERIAL = 2,
    OUTPUT_SD = 3,
    NUM_OUTPUTS = 4
};

// Display types
enum DisplayType {
    DISPLAY_NONE = 0,
    DISPLAY_LCD = 1,
    DISPLAY_OLED = 2
};

// System configuration structure (48 bytes)
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 validation
    uint8_t version;             // Schema version
    uint8_t checksum;            // XOR checksum

    // Output Modules (12 bytes)
    uint8_t outputEnabled[NUM_OUTPUTS];    // 4 bytes (bool per output)
    uint16_t outputInterval[NUM_OUTPUTS];  // 8 bytes (interval ms)

    // Display Settings (5 bytes)
    uint8_t displayType;         // LCD/OLED/None (DisplayType enum)
    uint8_t lcdI2CAddress;       // I2C address (default 0x27)
    Units defaultTempUnits;      // CELSIUS/FAHRENHEIT (Units enum)
    Units defaultPressUnits;     // BAR/PSI/KPA (Units enum)
    Units defaultElevUnits;      // METERS/FEET (Units enum)

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

    uint8_t reserved[7];         // Future expansion (increased from 3 to 7)
};

// Global system config instance
extern SystemConfig systemConfig;

// Initialization and persistence
void initSystemConfig();         // Load from EEPROM or set defaults
bool saveSystemConfig();         // Save to EEPROM
bool loadSystemConfig();         // Load from EEPROM
void resetSystemConfig();        // Reset to compile-time defaults
uint8_t calculateChecksum(SystemConfig* cfg);

#endif // SYSTEM_CONFIG_H
