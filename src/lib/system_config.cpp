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

// Boot diagnostics buffer — loadSystemConfig() runs before the message router is
// up, so msg.* calls would be silently dropped. Store the events here and flush
// them via flushSystemConfigBootDiagnostics() once router.begin() has been called.
static struct {
    enum : uint8_t {
        DIAG_NONE             = 0x00,
        DIAG_LOADED_OK        = 0x01,
        DIAG_USED_DEFAULTS    = 0x02,
        DIAG_BAD_MAGIC        = 0x04,
        DIAG_VERSION_MISMATCH = 0x08,
        DIAG_CHECKSUM_FAILED  = 0x10,
    };
    uint8_t flags            = 0;
    uint8_t loadedVersion    = 0;
    uint8_t storedChecksum   = 0;
    uint8_t calcChecksum     = 0;
} bootDiag;

void flushSystemConfigBootDiagnostics() {
    if (bootDiag.flags & bootDiag.DIAG_BAD_MAGIC) {
        msg.debug.warn(TAG_SYSTEM, "System config: bad magic, used defaults");
    }
    if (bootDiag.flags & bootDiag.DIAG_VERSION_MISMATCH) {
        msg.debug.warn(TAG_SYSTEM, "System config: version mismatch (got %u, need %u), used defaults",
                       bootDiag.loadedVersion, SYSTEM_CONFIG_VERSION);
    }
    if (bootDiag.flags & bootDiag.DIAG_CHECKSUM_FAILED) {
        msg.debug.warn(TAG_SYSTEM, "System config: checksum failed (got 0x%02X, calc 0x%02X), used defaults",
                       bootDiag.storedChecksum, bootDiag.calcChecksum);
    }
    if (bootDiag.flags & bootDiag.DIAG_LOADED_OK) {
        msg.debug.info(TAG_SYSTEM, "System config loaded from EEPROM (v%u)", systemConfig.version);
    }
    if (bootDiag.flags & bootDiag.DIAG_USED_DEFAULTS) {
        msg.debug.info(TAG_SYSTEM, "Using default system config");
    }
    bootDiag.flags = 0;
}

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
    if (loadSystemConfig()) {
        bootDiag.flags |= bootDiag.DIAG_LOADED_OK;
        return;
    }
    bootDiag.flags |= bootDiag.DIAG_USED_DEFAULTS;
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

    #if ENABLE_CAN
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_CAN] = CAN_OUTPUT_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;
    systemConfig.outputInterval[OUTPUT_CAN] = 100;
    #endif

    #if ENABLE_REALDASH
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_REALDASH] = REALDASH_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;
    systemConfig.outputInterval[OUTPUT_REALDASH] = 100;
    #endif

    #if ENABLE_SERIAL_OUTPUT
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;  // OFF - clogs USB serial
    systemConfig.outputInterval[OUTPUT_SERIAL] = SERIAL_CSV_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;
    systemConfig.outputInterval[OUTPUT_SERIAL] = 1000;
    #endif

    #if ENABLE_SD_LOGGING
    systemConfig.outputEnabled[OUTPUT_SD] = 0;  // OFF - requires SD card hardware
    systemConfig.outputInterval[OUTPUT_SD] = SD_LOG_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SD] = 0;
    systemConfig.outputInterval[OUTPUT_SD] = 5000;
    #endif

    #if ENABLE_ALARMS
    systemConfig.outputEnabled[OUTPUT_ALARM] = 1;  // ON - safety critical
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;  // 10Hz check rate
    #else
    systemConfig.outputEnabled[OUTPUT_ALARM] = 0;
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;
    #endif

    #if ENABLE_RELAY_OUTPUT
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

#if ENABLE_RELAY_OUTPUT
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
    systemConfig.buses.active_i2c = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.i2c_clock = DEFAULT_I2C_CLOCK;
    systemConfig.buses.active_spi = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.spi_clock = DEFAULT_SPI_CLOCK;

    // CAN configuration - output enabled by default for backward compatibility
    systemConfig.buses.input_can_bus = 0xFF;  // Disabled by default
    systemConfig.buses.output_can_bus = DEFAULT_CAN_BUS;
    systemConfig.buses.can_input_baudrate = DEFAULT_CAN_BAUDRATE;
    systemConfig.buses.can_output_baudrate = DEFAULT_CAN_BAUDRATE;
    systemConfig.buses.can_input_mode = CAN_INPUT_OFF;  // Disabled by default
    systemConfig.buses.can_output_enabled = 1;  // Enabled by default
    systemConfig.buses.elm327_serial_port = 0xFF;  // Disabled by default

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
bool saveSystemConfig(bool verbose) {
    // Update checksum before saving
    systemConfig.checksum = calculateChecksum(&systemConfig);

    // Write to EEPROM
    EEPROM.put(SYSTEM_CONFIG_ADDRESS, systemConfig);

    if (verbose) {
        msg.control.print(F("✓ System config saved (addr=0x"));
        char buf[8];
        snprintf(buf, sizeof(buf), "%04X", (unsigned)SYSTEM_CONFIG_ADDRESS);
        msg.control.print(buf);
        msg.control.print(F(", v"));
        msg.control.print(systemConfig.version);
        msg.control.print(F(", cksum=0x"));
        snprintf(buf, sizeof(buf), "%02X", systemConfig.checksum);
        msg.control.println(buf);
    } else {
        msg.debug.info(TAG_SYSTEM, "Config saved (addr=0x%04X, v%u, cksum=0x%02X)",
                       (unsigned)SYSTEM_CONFIG_ADDRESS, systemConfig.version, systemConfig.checksum);
    }
    return true;
}

/**
 * Register system pins in the pin registry
 * This reserves these pins and makes them visible in the registry export
 */
void registerSystemPins() {
#if ENABLE_MODE_BUTTON
    registerPin(MODE_BUTTON_PIN, PIN_BUTTON, "Mode Button");
#endif

#if ENABLE_ALARMS
    #ifdef ALARMS_PIN
    registerPin(ALARMS_PIN, PIN_BUZZER, "Buzzer");
    #endif
#endif

#if ENABLE_CAN_HYBRID || !PROFILE_HAS_NATIVE_CAN
    #if defined(CAN_CS_0) && (CAN_CS_0 != 0xFF)
    registerPin(CAN_CS_0, PIN_CS, "CAN CS");
    #endif
    #if defined(CAN_INT_0) && (CAN_INT_0 != 0xFF)
    registerPin(CAN_INT_0, PIN_RESERVED, "CAN INT");
    #endif
#endif

#if SUPPORTS_SD
    #ifndef BUILTIN_SDCARD
        #define BUILTIN_SDCARD 254
    #endif
    #if SD_PIN != BUILTIN_SDCARD
    registerPin(SD_PIN, PIN_CS, "SD CS");
    #endif
#endif

#if ENABLE_TEST_MODE
    #ifdef TEST_MODE_PIN
    registerPin(TEST_MODE_PIN, PIN_BUTTON, "Test Mode Trigger");
    #endif
#endif
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
        bootDiag.flags |= bootDiag.DIAG_BAD_MAGIC;
        return false;
    }

    // Check version
    if (temp.version != SYSTEM_CONFIG_VERSION) {
        bootDiag.flags |= bootDiag.DIAG_VERSION_MISMATCH;
        bootDiag.loadedVersion = temp.version;
        return false;
    }

    // Verify checksum
    uint8_t calculatedChecksum = calculateChecksum(&temp);
    if (temp.checksum != calculatedChecksum) {
        bootDiag.flags |= bootDiag.DIAG_CHECKSUM_FAILED;
        bootDiag.storedChecksum = temp.checksum;
        bootDiag.calcChecksum   = calculatedChecksum;
        return false;
    }

    // Valid config - copy to global
    memcpy(&systemConfig, &temp, sizeof(SystemConfig));
    return true;
}
