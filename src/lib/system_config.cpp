/*
 * system_config.cpp - System Configuration Implementation
 */

#include "system_config.h"
#include "../config.h"
#include "units_registry.h"
#include "message_router.h"  // For TransportID enum
#include "bus_defaults.h"
#include "message_api.h"
#include "log_tags.h"
#include "pin_registry.h"
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
        msg.debug.info(TAG_SYSTEM, "System config loaded from EEPROM");
        return;
    }

    // Fallback: Use defaults from config.h
    msg.debug.info(TAG_SYSTEM, "Using default system config");
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

    // Bus Configuration defaults (simplified "pick one" model)
    systemConfig.buses.active_i2c = DEFAULT_I2C_BUS;
    systemConfig.buses.i2c_clock = DEFAULT_I2C_CLOCK;
    systemConfig.buses.active_spi = DEFAULT_SPI_BUS;
    systemConfig.buses.spi_clock = DEFAULT_SPI_CLOCK;
    systemConfig.buses.active_can = DEFAULT_CAN_BUS;
    systemConfig.buses.can_baudrate = DEFAULT_CAN_BAUDRATE;
    systemConfig.buses.reserved[0] = 0;
    systemConfig.buses.reserved[1] = 0;

    // Serial Port Configuration defaults
    // USB Serial is always available; Serial1 enabled by default, others disabled
    systemConfig.serial.enabled_mask = 0x01;  // Bit 0 = Serial1 enabled
    for (int i = 0; i < 8; i++) {
        systemConfig.serial.baudrate_index[i] = BAUD_115200;  // Default 115200 baud
    }
    for (int i = 0; i < 7; i++) {
        systemConfig.serial.reserved[i] = 0;
    }

    // Log Filter Configuration defaults
    // Default to INFO level (show ERROR, WARN, INFO but not DEBUG)
    systemConfig.logFilter.control_level = 3;  // LOG_LEVEL_INFO
    systemConfig.logFilter.data_level = 3;     // LOG_LEVEL_INFO
    systemConfig.logFilter.debug_level = 3;    // LOG_LEVEL_INFO
    systemConfig.logFilter.enabledTags = 0xFFFFFFFF;  // All tags enabled
    for (int i = 0; i < 5; i++) {
        systemConfig.logFilter.reserved[i] = 0;
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

    msg.control.println(F("✓ System config saved to EEPROM"));
    return true;
}

/**
 * Register system pins in the pin registry
 * This reserves these pins and makes them visible in the registry export
 */
void registerSystemPins() {
    // Mode button
    if (systemConfig.modeButtonPin != 0xFF) {
        registerPin(systemConfig.modeButtonPin, PIN_BUTTON, "Mode Button");
    }

    // Buzzer
    if (systemConfig.buzzerPin != 0xFF) {
        registerPin(systemConfig.buzzerPin, PIN_BUZZER, "Buzzer");
    }

    // CAN chip select (only for external MCP2515)
    if (systemConfig.canCSPin != 0xFF) {
        registerPin(systemConfig.canCSPin, PIN_CS, "CAN CS");
    }

    // CAN interrupt (only for external MCP2515)
    if (systemConfig.canIntPin != 0xFF) {
        registerPin(systemConfig.canIntPin, PIN_RESERVED, "CAN INT");
    }

    // SD card chip select
    if (systemConfig.sdCSPin != 0xFF) {
        registerPin(systemConfig.sdCSPin, PIN_CS, "SD CS");
    }

    // Test mode pin (optional)
    if (systemConfig.testModePin != 0xFF) {
        registerPin(systemConfig.testModePin, PIN_BUTTON, "Test Mode Trigger");
    }
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

    // Check version
    if (temp.version != SYSTEM_CONFIG_VERSION) {
        msg.debug.warn(TAG_SYSTEM, "System config version mismatch (expected %d, got %d) - ignoring", SYSTEM_CONFIG_VERSION, temp.version);
        return false;
    }

    // Verify checksum
    uint8_t calculatedChecksum = calculateChecksum(&temp);
    if (temp.checksum != calculatedChecksum) {
        msg.debug.warn(TAG_SYSTEM, "System config checksum failed - ignoring");
        return false;
    }

    // Valid config - copy to global
    memcpy(&systemConfig, &temp, sizeof(SystemConfig));
    return true;
}
