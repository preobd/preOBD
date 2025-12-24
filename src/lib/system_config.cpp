/*
 * system_config.cpp - System Configuration Implementation
 */

#include "system_config.h"
#include "../config.h"
#include "units_registry.h"
#include <EEPROM.h>

// Global system config instance
SystemConfig systemConfig;

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
    #ifdef ENABLE_CAN
    systemConfig.outputEnabled[OUTPUT_CAN] = 1;
    systemConfig.outputInterval[OUTPUT_CAN] = CAN_OUTPUT_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;
    systemConfig.outputInterval[OUTPUT_CAN] = 100;
    #endif

    #ifdef ENABLE_REALDASH
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 1;
    systemConfig.outputInterval[OUTPUT_REALDASH] = REALDASH_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;
    systemConfig.outputInterval[OUTPUT_REALDASH] = 100;
    #endif

    #ifdef ENABLE_SERIAL_OUTPUT
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 1;
    systemConfig.outputInterval[OUTPUT_SERIAL] = SERIAL_CSV_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;
    systemConfig.outputInterval[OUTPUT_SERIAL] = 1000;
    #endif

    #ifdef ENABLE_SD_LOGGING
    systemConfig.outputEnabled[OUTPUT_SD] = 1;
    systemConfig.outputInterval[OUTPUT_SD] = SD_LOG_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SD] = 0;
    systemConfig.outputInterval[OUTPUT_SD] = 5000;
    #endif

    #ifdef ENABLE_ALARMS
    systemConfig.outputEnabled[OUTPUT_ALARM] = 1;
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;  // 10Hz check rate
    #else
    systemConfig.outputEnabled[OUTPUT_ALARM] = 0;
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;
    #endif

    // Display defaults
    #ifdef ENABLE_LCD
    systemConfig.displayEnabled = 1;
    systemConfig.displayType = DISPLAY_LCD;
    #else
    systemConfig.displayEnabled = 0;
    systemConfig.displayType = DISPLAY_NONE;
    #endif

    systemConfig.lcdI2CAddress = 0x27;

    // Resolve unit names to indices at boot time
    systemConfig.defaultTempUnits = getUnitsIndexByName(DEFAULT_TEMPERATURE_UNITS);
    systemConfig.defaultPressUnits = getUnitsIndexByName(DEFAULT_PRESSURE_UNITS);
    systemConfig.defaultElevUnits = getUnitsIndexByName(DEFAULT_ELEVATION_UNITS);

    // Timing intervals
    systemConfig.sensorReadInterval = SENSOR_READ_INTERVAL_MS;
    systemConfig.alarmCheckInterval = ALARM_CHECK_INTERVAL_MS;
    systemConfig.lcdUpdateInterval = LCD_UPDATE_INTERVAL_MS;
    systemConfig.reserved1 = 0;

    // Hardware pins
    systemConfig.modeButtonPin = MODE_BUTTON;
    systemConfig.buzzerPin = BUZZER;
    systemConfig.canCSPin = CAN_CS;
    systemConfig.canIntPin = CAN_INT;
    systemConfig.sdCSPin = SD_CS_PIN;

    #ifdef TEST_MODE_TRIGGER_PIN
    systemConfig.testModePin = TEST_MODE_TRIGGER_PIN;
    #else
    systemConfig.testModePin = 0xFF;  // Disabled
    #endif

    systemConfig.reserved2 = 0;

    // Physical constants
    systemConfig.seaLevelPressure = SEA_LEVEL_PRESSURE_HPA;

    // Reserved space
    for (int i = 0; i < 6; i++) {
        systemConfig.reserved[i] = 0;
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

    // Check version
    if (temp.version != SYSTEM_CONFIG_VERSION) {
        Serial.println(F("System config version mismatch - ignoring"));
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
