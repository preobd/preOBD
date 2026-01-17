/*
 * system_config.cpp - System Configuration Implementation
 */

#include "system_config.h"
#include "../config.h"
#include "units_registry.h"
#include "message_router.h"  // For TransportID enum
#include "bus_defaults.h"
#include "message_api.h"
#include <EEPROM.h>

// Global system config instance
SystemConfig systemConfig;

void printSystemStatus() {
    msg.control.println(F("=== System Configuration ==="));

    msg.control.print(F("Sea Level Pressure: "));
    msg.control.print(systemConfig.seaLevelPressure);
    msg.control.println(F(" hPa"));

    msg.control.print(F("Global Intervals: Sensor="));
    msg.control.print(systemConfig.sensorReadInterval);
    msg.control.print(F("ms, Alarm="));
    msg.control.print(systemConfig.alarmCheckInterval);
    msg.control.println(F("ms"));

    msg.control.print(F("Default Units: Temp="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
    msg.control.print(F(", Pressure="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
    msg.control.print(F(", Elevation="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
    msg.control.print(F(", Speed="));
    msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultSpeedUnits));
}

/**
 * Calculate XOR checksum for SystemConfig structure
 */
uint8_t calculateChecksum(SystemConfig* cfg) {
    uint8_t checksum = 0;
    uint8_t* bytes = (uint8_t*)cfg;

    // XOR all bytes except the checksum field itself
    for (size_t i = 0; i < sizeof(SystemConfig); i++) {
        if (i != offsetof(SystemConfig, checksum)) {
            checksum ^= bytes[i];
        }
    }

    return checksum;
}

/**
 * Initialize system configuration
 * Try loading from EEPROM, fallback to defaults
 */
void initSystemConfig() {
    // Try loading from EEPROM
    if (loadSystemConfig()) {
        Serial.println(F("✓ System config loaded from EEPROM"));
        return;
    }

    // Fallback: Use defaults from config.h
    Serial.println(F("Using default system config"));
    resetSystemConfig();
}

/**
 * Reset system configuration to defaults
 */
void resetSystemConfig() {
    systemConfig.magic = SYSTEM_CONFIG_MAGIC;
    systemConfig.version = SYSTEM_CONFIG_VERSION;

    // Output defaults (check config.h for #define ENABLE_*)
    // Data outputs default OFF to keep USB clean - user must explicitly enable
    // Safety outputs default ON when compiled in

    #ifdef ENABLE_CAN
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_CAN] = CAN_OUTPUT_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;
    systemConfig.outputInterval[OUTPUT_CAN] = 100;
    #endif

    #ifdef ENABLE_REALDASH
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_REALDASH] = REALDASH_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;
    systemConfig.outputInterval[OUTPUT_REALDASH] = 100;
    #endif

    #ifdef ENABLE_SERIAL_OUTPUT
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;  // OFF - clogs USB serial
    systemConfig.outputInterval[OUTPUT_SERIAL] = SERIAL_CSV_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;
    systemConfig.outputInterval[OUTPUT_SERIAL] = 1000;
    #endif

    #ifdef ENABLE_SD_LOGGING
    systemConfig.outputEnabled[OUTPUT_SD] = 0;  // OFF - requires SD card hardware
    systemConfig.outputInterval[OUTPUT_SD] = SD_LOG_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SD] = 0;
    systemConfig.outputInterval[OUTPUT_SD] = 5000;
    #endif

    #ifdef ENABLE_ALARMS
    systemConfig.outputEnabled[OUTPUT_ALARM] = 1;  // ON - safety critical
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;  // 10Hz check rate
    #else
    systemConfig.outputEnabled[OUTPUT_ALARM] = 0;
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;
    #endif

    #ifdef ENABLE_RELAY_OUTPUT
    systemConfig.outputEnabled[OUTPUT_RELAY] = 1;  // ON - user-configured relay control
    systemConfig.outputInterval[OUTPUT_RELAY] = 100;  // 10Hz check rate
    #endif

    // Display defaults (only one display type should be defined in platformio.ini)
    #if defined(ENABLE_LCD)
        systemConfig.displayEnabled = 1;
        systemConfig.displayType = DISPLAY_LCD;
    #elif defined(ENABLE_OLED)
        systemConfig.displayEnabled = 1;
        systemConfig.displayType = DISPLAY_OLED;
    #else
        systemConfig.displayEnabled = 0;
        systemConfig.displayType = DISPLAY_NONE;
    #endif

    systemConfig.lcdI2CAddress = 0x27;

    // Resolve unit names to indices at boot time
    systemConfig.defaultTempUnits = getUnitsIndexByName(DEFAULT_TEMPERATURE_UNITS);
    systemConfig.defaultPressUnits = getUnitsIndexByName(DEFAULT_PRESSURE_UNITS);
    systemConfig.defaultElevUnits = getUnitsIndexByName(DEFAULT_ELEVATION_UNITS);
    systemConfig.defaultSpeedUnits = getUnitsIndexByName(DEFAULT_SPEED_UNITS);

    // Timing intervals
    systemConfig.sensorReadInterval = SENSOR_READ_INTERVAL_MS;
    systemConfig.alarmCheckInterval = ALARM_CHECK_INTERVAL_MS;
    systemConfig.lcdUpdateInterval = LCD_UPDATE_INTERVAL_MS;
    systemConfig.reserved1 = 0;

    // Hardware pins
    systemConfig.modeButtonPin = MODE_BUTTON;
    systemConfig.buzzerPin = BUZZER;

    #ifndef USE_FLEXCAN_NATIVE
    systemConfig.canCSPin = CAN_CS;
    systemConfig.canIntPin = CAN_INT;
    #else
    systemConfig.canCSPin = 0xFF;  // Not used with native FlexCAN
    systemConfig.canIntPin = 0xFF; // Not used with native FlexCAN
    #endif

    systemConfig.sdCSPin = SD_CS_PIN;

    #ifdef TEST_MODE_TRIGGER_PIN
    systemConfig.testModePin = TEST_MODE_TRIGGER_PIN;
    #else
    systemConfig.testModePin = 0xFF;  // Disabled
    #endif

    systemConfig.reserved2 = 0;

    // Physical constants
    systemConfig.seaLevelPressure = SEA_LEVEL_PRESSURE_HPA;

    // Transport Router Configuration (NEW in v4)
    // Default: All planes → USB Serial
    systemConfig.router.control_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.control_secondary = TRANSPORT_NONE;
    systemConfig.router.data_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.data_secondary = TRANSPORT_NONE;
    systemConfig.router.debug_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.debug_secondary = TRANSPORT_NONE;

    // Bluetooth defaults (disabled)
    systemConfig.router.bt_type = 0;  // BT_TYPE_NONE
    systemConfig.router.bt_auth_required = 0;  // Disabled by default
    systemConfig.router.bt_pin = 0;  // Not set

    // Reserved router space
    for (int i = 0; i < 6; i++) {
        systemConfig.router.reserved_router[i] = 0;
    }

#ifdef ENABLE_RELAY_OUTPUT
    // Relay defaults (NEW in v5)
    for (int i = 0; i < MAX_RELAYS; i++) {
        systemConfig.relays[i].outputPin = 0xFF;       // Unconfigured
        systemConfig.relays[i].inputIndex = 0xFF;      // Unassigned
        systemConfig.relays[i].mode = RELAY_DISABLED;
        systemConfig.relays[i].reserved = 0;
        systemConfig.relays[i].thresholdOn = 0.0;
        systemConfig.relays[i].thresholdOff = 0.0;
        systemConfig.relays[i].reserved2 = 0;
    }
#endif

    // Bus Configuration defaults (NEW)
    // I2C buses: Only Wire (bus 0) enabled by default
    for (int i = 0; i < 3; i++) {
        systemConfig.buses.i2c[i].enabled = (i == 0) ? 1 : 0;
        systemConfig.buses.i2c[i].sda_pin = BUS_PIN_DEFAULT;  // Use platform default
        systemConfig.buses.i2c[i].scl_pin = BUS_PIN_DEFAULT;
        systemConfig.buses.i2c[i].clock_speed = 400;  // 400kHz
        for (int j = 0; j < 4; j++) {
            systemConfig.buses.i2c[i].reserved[j] = 0;
        }
    }

    // SPI buses: Only SPI (bus 0) enabled by default
    for (int i = 0; i < 3; i++) {
        systemConfig.buses.spi[i].enabled = (i == 0) ? 1 : 0;
        systemConfig.buses.spi[i].mosi_pin = BUS_PIN_DEFAULT;
        systemConfig.buses.spi[i].miso_pin = BUS_PIN_DEFAULT;
        systemConfig.buses.spi[i].sck_pin = BUS_PIN_DEFAULT;
        systemConfig.buses.spi[i].clock_speed = 4000000;  // 4MHz
        for (int j = 0; j < 4; j++) {
            systemConfig.buses.spi[i].reserved[j] = 0;
        }
    }

    // CAN buses: Only CAN1 (bus 0) enabled by default
    for (int i = 0; i < 3; i++) {
        systemConfig.buses.can[i].enabled = (i == 0) ? 1 : 0;
        systemConfig.buses.can[i].tx_pin = BUS_PIN_DEFAULT;
        systemConfig.buses.can[i].rx_pin = BUS_PIN_DEFAULT;

        #ifdef USE_FLEXCAN_NATIVE
        systemConfig.buses.can[i].use_external = 0;  // Use native FlexCAN
        systemConfig.buses.can[i].cs_pin = 0xFF;     // Not used
        systemConfig.buses.can[i].int_pin = 0xFF;    // Not used
        #else
        systemConfig.buses.can[i].use_external = 1;  // Use MCP2515
        systemConfig.buses.can[i].cs_pin = (i == 0) ? CAN_CS : 0xFF;
        systemConfig.buses.can[i].int_pin = (i == 0) ? CAN_INT : 0xFF;
        #endif

        systemConfig.buses.can[i].baudrate = 500000;  // 500kbps
        systemConfig.buses.can[i].reserved[0] = 0;
        systemConfig.buses.can[i].reserved[1] = 0;
    }

    // Bus active bitmasks
    systemConfig.buses.active_i2c_mask = 0b00000001;  // Wire enabled
    systemConfig.buses.active_spi_mask = 0b00000001;  // SPI enabled
    systemConfig.buses.active_can_mask = 0b00000001;  // CAN1 enabled

    // Reserved bytes
    for (int i = 0; i < 5; i++) {
        systemConfig.buses.reserved[i] = 0;
    }

    // Calculate checksum
    systemConfig.checksum = calculateChecksum(&systemConfig);
}

/**
 * Save system configuration to EEPROM
 * @return true if successful
 */
bool saveSystemConfig() {
    // Update checksum before saving
    systemConfig.checksum = calculateChecksum(&systemConfig);

    // Write to EEPROM
    EEPROM.put(SYSTEM_CONFIG_ADDRESS, systemConfig);

    Serial.println(F("✓ System config saved to EEPROM"));
    return true;
}

/**
 * Load system configuration from EEPROM
 * @return true if successful (valid config found)
 */
bool loadSystemConfig() {
    SystemConfig temp;
    EEPROM.get(SYSTEM_CONFIG_ADDRESS, temp);

    // Validate magic number
    if (temp.magic != SYSTEM_CONFIG_MAGIC) {
        return false;
    }

#ifdef ENABLE_RELAY_OUTPUT
    // Handle migration from v4 to v5 (relay feature added)
    if (temp.version == 4) {
        Serial.println(F("Migrating system config from v4 to v5"));

        // Load old 64-byte config (v4)
        // Copy first 64 bytes to systemConfig
        uint8_t* src = (uint8_t*)&temp;
        uint8_t* dst = (uint8_t*)&systemConfig;
        memcpy(dst, src, 64);  // Copy v4 data

        // Initialize new relay fields with defaults
        for (int i = 0; i < MAX_RELAYS; i++) {
            systemConfig.relays[i].outputPin = 0xFF;
            systemConfig.relays[i].inputIndex = 0xFF;
            systemConfig.relays[i].mode = RELAY_DISABLED;
            systemConfig.relays[i].reserved = 0;
            systemConfig.relays[i].thresholdOn = 0.0;
            systemConfig.relays[i].thresholdOff = 0.0;
            systemConfig.relays[i].reserved2 = 0;
        }

        // Update version number
        systemConfig.version = SYSTEM_CONFIG_VERSION;

        // Save migrated config
        saveSystemConfig();
        Serial.println(F("✓ Migration complete"));
        return true;
    }
#endif

    // Check version
    if (temp.version != SYSTEM_CONFIG_VERSION) {
        Serial.print(F("System config version mismatch (expected "));
        Serial.print(SYSTEM_CONFIG_VERSION);
        Serial.print(F(", got "));
        Serial.print(temp.version);
        Serial.println(F(") - ignoring"));
        return false;
    }

    // Verify checksum
    uint8_t calculatedChecksum = calculateChecksum(&temp);
    if (temp.checksum != calculatedChecksum) {
        Serial.println(F("System config checksum failed - ignoring"));
        return false;
    }

    // Valid config - copy to global
    memcpy(&systemConfig, &temp, sizeof(SystemConfig));
    return true;
}
