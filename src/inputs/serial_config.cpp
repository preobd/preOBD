/*
 * serial_config.cpp - Serial Command Interface Implementation
 * NO String class - uses char* and fixed buffers for minimal RAM usage
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static/compile-time mode)
 */

#include "../config.h"
#include "../version.h"

#ifndef USE_STATIC_CONFIG

#include "serial_config.h"
#include "input_manager.h"
#include "input.h"
#include "../lib/system_mode.h"
#include <string.h>
#include <ctype.h>

// Command buffer - fixed size, no String class
#define CMD_BUFFER_SIZE 80
static char commandBuffer[CMD_BUFFER_SIZE];
static uint8_t cmdIndex = 0;

// Helper: trim whitespace in-place
static void trim(char* str) {
    if (!str || *str == '\0') return;

    // Trim leading
    char* start = str;
    while (*start && isspace(*start)) start++;

    // Trim trailing
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';

    // Move trimmed string to start
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Helper: convert to uppercase in-place
static void toUpper(char* str) {
    while (*str) {
        *str = toupper(*str);
        str++;
    }
}

// Helper: case-insensitive string compare
static bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(*a) != toupper(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

void initSerialConfig() {
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("  openEMS Serial Configuration"));
    Serial.println(F("  Type 'HELP' for commands"));
    Serial.println(F("========================================"));
    Serial.println();
    cmdIndex = 0;
    commandBuffer[0] = '\0';
}

void processSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (cmdIndex > 0) {
                commandBuffer[cmdIndex] = '\0';
                trim(commandBuffer);
                if (commandBuffer[0] != '\0') {
                    handleSerialCommand(commandBuffer);
                }
                cmdIndex = 0;
                commandBuffer[0] = '\0';
                Serial.print(F("\n> "));
            }
        } else if (cmdIndex < CMD_BUFFER_SIZE - 1) {
            commandBuffer[cmdIndex++] = c;
        }
    }
}

/**
 * Parse a pin string into a pin number.
 * Accepts "A0"-"A15" for analog pins, or numeric strings for digital pins.
 */
static uint8_t parsePin(const char* pinStr) {
    if (!pinStr) return 0;

    if (toupper(pinStr[0]) == 'A') {
        return A0 + atoi(pinStr + 1);
    }
    return atoi(pinStr);
}

// Parse Application enum from string
static Application parseApplication(const char* appStr) {
    if (!appStr) return APP_NONE;

    if (streq(appStr, "CHT")) return CHT;
    if (streq(appStr, "EGT")) return EGT;
    if (streq(appStr, "COOLANT_TEMP")) return COOLANT_TEMP;
    if (streq(appStr, "OIL_TEMP")) return OIL_TEMP;
    if (streq(appStr, "TCASE_TEMP")) return TCASE_TEMP;
    if (streq(appStr, "OIL_PRESSURE")) return OIL_PRESSURE;
    if (streq(appStr, "BOOST_PRESSURE")) return BOOST_PRESSURE;
    if (streq(appStr, "FUEL_PRESSURE")) return FUEL_PRESSURE;
    if (streq(appStr, "PRIMARY_BATTERY")) return PRIMARY_BATTERY;
    if (streq(appStr, "AUXILIARY_BATTERY")) return AUXILIARY_BATTERY;
    if (streq(appStr, "COOLANT_LEVEL")) return COOLANT_LEVEL;
    if (streq(appStr, "AMBIENT_TEMP")) return AMBIENT_TEMP;
    if (streq(appStr, "BAROMETRIC_PRESSURE")) return BAROMETRIC_PRESSURE;
    if (streq(appStr, "HUMIDITY")) return HUMIDITY;
    if (streq(appStr, "ELEVATION")) return ELEVATION;
    if (streq(appStr, "ENGINE_RPM")) return ENGINE_RPM;

    return APP_NONE;
}

// Parse Sensor enum from string
static Sensor parseSensor(const char* sensorStr) {
    if (!sensorStr) return SENSOR_NONE;

    if (streq(sensorStr, "MAX6675")) return MAX6675;
    if (streq(sensorStr, "MAX31855")) return MAX31855;
    if (streq(sensorStr, "VDO_120C_LOOKUP")) return VDO_120C_LOOKUP;
    if (streq(sensorStr, "VDO_150C_LOOKUP")) return VDO_150C_LOOKUP;
    if (streq(sensorStr, "VDO_120C_STEINHART")) return VDO_120C_STEINHART;
    if (streq(sensorStr, "VDO_150C_STEINHART")) return VDO_150C_STEINHART;
    if (streq(sensorStr, "VDO_2BAR")) return VDO_2BAR;
    if (streq(sensorStr, "VDO_5BAR")) return VDO_5BAR;
    if (streq(sensorStr, "VOLTAGE_DIVIDER")) return VOLTAGE_DIVIDER;
    if (streq(sensorStr, "W_PHASE_RPM")) return W_PHASE_RPM;
    if (streq(sensorStr, "BME280_TEMP")) return BME280_TEMP;
    if (streq(sensorStr, "BME280_PRESSURE")) return BME280_PRESSURE;
    if (streq(sensorStr, "BME280_HUMIDITY")) return BME280_HUMIDITY;
    if (streq(sensorStr, "BME280_ELEVATION")) return BME280_ELEVATION;
    if (streq(sensorStr, "FLOAT_SWITCH")) return FLOAT_SWITCH;

    return SENSOR_NONE;
}

// Parse Units enum from string
static Units parseUnits(const char* unitsStr) {
    if (!unitsStr) return CELSIUS;

    if (streq(unitsStr, "CELSIUS") || streq(unitsStr, "C")) return CELSIUS;
    if (streq(unitsStr, "FAHRENHEIT") || streq(unitsStr, "F")) return FAHRENHEIT;
    if (streq(unitsStr, "PSI")) return PSI;
    if (streq(unitsStr, "BAR")) return BAR;
    if (streq(unitsStr, "KPA")) return KPA;
    if (streq(unitsStr, "VOLTS") || streq(unitsStr, "V")) return VOLTS;
    if (streq(unitsStr, "RPM")) return RPM;
    if (streq(unitsStr, "PERCENT") || streq(unitsStr, "%")) return PERCENT;
    if (streq(unitsStr, "METERS") || streq(unitsStr, "M")) return METERS;
    if (streq(unitsStr, "FEET") || streq(unitsStr, "FT")) return FEET;

    return CELSIUS;  // Default
}

void handleSerialCommand(char* cmd) {
    if (!cmd) return;

    trim(cmd);
    toUpper(cmd);

    // ===== MODE COMMANDS (always available to prevent deadlock) =====
    if (streq(cmd, "CONFIG")) {
        setMode(MODE_CONFIG);
        return;
    }

    if (streq(cmd, "RUN")) {
        setMode(MODE_RUN);
        return;
    }

    // ===== COMMAND GATING (based on current mode) =====
    // In RUN mode, only allow read-only commands
    if (isInRunMode()) {
        // Read-only commands allowed in RUN mode
        bool isReadOnly =
            streq(cmd, "HELP") ||
            streq(cmd, "?") ||
            streq(cmd, "VERSION") ||
            streq(cmd, "DUMP") ||
            streq(cmd, "INFO") ||
            strncmp(cmd, "LIST", 4) == 0;  // LIST, LIST INPUTS, etc.

        if (!isReadOnly) {
            Serial.println();
            Serial.println(F("========================================"));
            Serial.println(F("  ERROR: Configuration locked in RUN mode"));
            Serial.println(F("  Type CONFIG to enter configuration mode"));
            Serial.println(F("========================================"));
            Serial.println();
            return;
        }
    }

    // ===== HELP & INFO COMMANDS =====
    if (streq(cmd, "HELP") || streq(cmd, "?")) {
        Serial.println();
        Serial.println(F("Available Commands:"));
        Serial.println();
        Serial.println(F("LIST Commands:"));
        Serial.println(F("  LIST INPUTS         - Show all configured inputs"));
        Serial.println(F("  LIST APPLICATIONS   - Show available Type presets"));
        Serial.println(F("  LIST SENSORS        - Show available Sensor Types"));
        Serial.println();
        Serial.println(F("SET Commands:"));
        Serial.println(F("  SET <pin> APPLICATION <application>"));
        Serial.println(F("  SET <pin> SENSOR <sensor>"));
        Serial.println(F("  SET <pin> NAME <name>"));
        Serial.println(F("  SET <pin> DISPLAY_NAME <name>"));
        Serial.println(F("  SET <pin> UNITS <units>"));
        Serial.println(F("  SET <pin> ALARM <min> <max>"));
        Serial.println();
        Serial.println(F("Control Commands:"));
        Serial.println(F("  ENABLE <pin>"));
        Serial.println(F("  DISABLE <pin>"));
        Serial.println(F("  CLEAR <pin>"));
        Serial.println(F("  INFO <pin>"));
        Serial.println();
        Serial.println(F("Config Commands:"));
        Serial.println(F("  SAVE    - Save config to EEPROM"));
        Serial.println(F("  LOAD    - Load config from EEPROM"));
        Serial.println(F("  RESET   - Clear all configuration"));
        Serial.println();
        Serial.println(F("System Commands:"));
        Serial.println(F("  CONFIG  - Enter configuration mode (unlock config)"));
        Serial.println(F("  RUN     - Enter run mode (lock config, resume sensors)"));
        Serial.println(F("  VERSION - Display firmware and EEPROM version"));
        Serial.println(F("  DUMP    - Show full configuration"));
        Serial.println(F("  RELOAD  - Trigger watchdog reset (system reboot)"));
        Serial.println();
        Serial.println(F("Examples:"));
        Serial.println(F("  SET A2 APPLICATION CHT"));
        Serial.println(F("  SET A2 SENSOR MAX6675"));
        Serial.println(F("  SET A2 UNITS CELSIUS"));
        Serial.println(F("  ENABLE A2"));
        Serial.println(F("  SAVE"));
        return;
    }

    // ===== LIST COMMANDS =====
    // Query available presets and show configured inputs
    if (streq(cmd, "LIST INPUTS")) {
        listAllInputs();
        return;
    }

    if (streq(cmd, "LIST APPLICATIONS")) {
        listApplicationPresets();
        return;
    }

    if (streq(cmd, "LIST SENSORS")) {
        listSensors();
        return;
    }

    // ===== SET COMMANDS =====
    // Modify input configuration - Syntax: SET <pin> <field> <value>
    if (strncmp(cmd, "SET ", 4) == 0) {
        char* rest = cmd + 4;
        trim(rest);

        // Find first space to separate pin from field
        char* firstSpace = strchr(rest, ' ');
        if (!firstSpace) {
            Serial.println(F("ERROR: Invalid SET syntax"));
            Serial.println(F("  Usage: SET <pin> <field> <value>"));
            Serial.println(F("  Example: SET A0 APPLICATION CHT"));
            return;
        }

        // Extract pin string
        *firstSpace = '\0';
        char* pinStr = rest;
        char* fieldAndValue = firstSpace + 1;
        trim(fieldAndValue);

        uint8_t pin = parsePin(pinStr);

        // SET <pin> APPLICATION <application>
        if (strncmp(fieldAndValue, "APPLICATION ", 12) == 0) {
            char* appStr = fieldAndValue + 12;
            trim(appStr);
            Application app = parseApplication(appStr);
            if (app == APP_NONE) {
                Serial.print(F("ERROR: Unknown application '"));
                Serial.print(appStr);
                Serial.println(F("'"));
                Serial.println(F("  Hint: Use 'LIST APPLICATIONS' to see valid options"));
                return;
            }
            if (setInputApplication(pin, app)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" configured as "));
                Serial.println(appStr);
            }
            return;
        }

        // SET <pin> SENSOR <sensor>
        if (strncmp(fieldAndValue, "SENSOR ", 7) == 0) {
            char* sensorStr = fieldAndValue + 7;
            trim(sensorStr);
            Sensor sensor = parseSensor(sensorStr);
            if (sensor == SENSOR_NONE) {
                Serial.print(F("ERROR: Unknown sensor '"));
                Serial.print(sensorStr);
                Serial.println(F("'"));
                Serial.println(F("  Hint: Use 'LIST SENSORS' to see valid options"));
                return;
            }
            if (setInputSensor(pin, sensor)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" sensor set to "));
                Serial.println(sensorStr);
            }
            return;
        }

        // SET <pin> NAME <name>
        if (strncmp(fieldAndValue, "NAME ", 5) == 0) {
            char* name = fieldAndValue + 5;
            trim(name);
            if (setInputName(pin, name)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" name set to "));
                Serial.println(name);
            }
            return;
        }

        // SET <pin> DISPLAY_NAME <name>
        if (strncmp(fieldAndValue, "DISPLAY_NAME ", 13) == 0) {
            char* name = fieldAndValue + 13;
            trim(name);
            if (setInputDisplayName(pin, name)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" display name set to "));
                Serial.println(name);
            }
            return;
        }

        // SET <pin> UNITS <units>
        if (strncmp(fieldAndValue, "UNITS ", 6) == 0) {
            char* unitsStr = fieldAndValue + 6;
            trim(unitsStr);
            Units units = parseUnits(unitsStr);
            if (setInputUnits(pin, units)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" units set to "));
                Serial.println(unitsStr);
            }
            return;
        }

        // SET <pin> ALARM <min> <max>
        if (strncmp(fieldAndValue, "ALARM ", 6) == 0) {
            char* values = fieldAndValue + 6;
            trim(values);
            char* spacePos = strchr(values, ' ');
            if (!spacePos) {
                Serial.println(F("ERROR: ALARM requires min and max values"));
                return;
            }
            *spacePos = '\0';
            float minVal = atof(values);
            float maxVal = atof(spacePos + 1);
            if (setInputAlarmRange(pin, minVal, maxVal)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" alarm range set to "));
                Serial.print(minVal);
                Serial.print(F(" - "));
                Serial.println(maxVal);
            }
            return;
        }

        Serial.println(F("ERROR: Unknown SET field"));
        return;
    }

    // ===== ENABLE/DISABLE COMMANDS =====
    // Control input active state
    if (strncmp(cmd, "ENABLE ", 7) == 0) {
        char* pinStr = cmd + 7;
        trim(pinStr);
        uint8_t pin = parsePin(pinStr);
        if (enableInput(pin, true)) {
            Serial.print(F("Input "));
            Serial.print(pinStr);
            Serial.println(F(" enabled"));
        }
        return;
    }

    if (strncmp(cmd, "DISABLE ", 8) == 0) {
        char* pinStr = cmd + 8;
        trim(pinStr);
        uint8_t pin = parsePin(pinStr);
        if (enableInput(pin, false)) {
            Serial.print(F("Input "));
            Serial.print(pinStr);
            Serial.println(F(" disabled"));
        }
        return;
    }

    // ===== CLEAR COMMAND =====
    if (strncmp(cmd, "CLEAR ", 6) == 0) {
        char* pinStr = cmd + 6;
        trim(pinStr);
        uint8_t pin = parsePin(pinStr);
        if (clearInput(pin)) {
            Serial.print(F("Input "));
            Serial.print(pinStr);
            Serial.println(F(" cleared"));
        }
        return;
    }

    // ===== INFO COMMAND =====
    if (strncmp(cmd, "INFO ", 5) == 0) {
        char* pinStr = cmd + 5;
        trim(pinStr);
        uint8_t pin = parsePin(pinStr);
        printInputInfo(pin);
        return;
    }

    // ===== PERSISTENCE COMMANDS =====
    // EEPROM save/load/reset
    if (streq(cmd, "SAVE")) {
        if (saveInputConfig()) {
            Serial.println(F("Configuration saved to EEPROM"));
        } else {
            Serial.println(F("ERROR: Failed to save configuration"));
        }
        return;
    }

    if (streq(cmd, "LOAD")) {
        if (loadInputConfig()) {
            Serial.println(F("Configuration loaded from EEPROM"));
        } else {
            Serial.println(F("ERROR: Failed to load configuration"));
        }
        return;
    }

    if (streq(cmd, "RESET")) {
        Serial.println(F("WARNING: This will erase all configuration!"));
        Serial.println(F("Type RESET CONFIRM to proceed"));
        return;
    }

    if (streq(cmd, "RESET CONFIRM")) {
        resetInputConfig();
        Serial.println(F("Configuration reset"));
        return;
    }

    // ===== VERSION COMMAND =====
    if (streq(cmd, "VERSION")) {
        Serial.println();
        Serial.println(F("========================================"));
        Serial.print(F("  Firmware: "));
        Serial.println(FIRMWARE_VERSION);
        Serial.print(F("  EEPROM Version: "));
        Serial.println(EEPROM_VERSION);
        Serial.print(F("  Active Inputs: "));
        extern uint8_t numActiveInputs;
        Serial.print(numActiveInputs);
        Serial.print(F("/"));
        Serial.println(MAX_INPUTS);
        Serial.println(F("========================================"));
        Serial.println();
        return;
    }

    // ===== DUMP COMMAND =====
    if (streq(cmd, "DUMP")) {
        Serial.println();
        Serial.println(F("========================================"));
        Serial.println(F("  Full Configuration Dump"));
        Serial.println(F("========================================"));
        Serial.println();
        listAllInputs();
        Serial.println();
        Serial.println(F("To save this configuration to EEPROM, type: SAVE"));
        Serial.println();
        return;
    }

    // ===== RELOAD COMMAND =====
    if (streq(cmd, "RELOAD")) {
        Serial.println();
        Serial.println(F("Triggering watchdog reset..."));
        Serial.println(F("System will reload in 2 seconds."));
        Serial.flush();

        // Enable watchdog if not already enabled (e.g., in CONFIG mode)
        extern void watchdogEnable(uint16_t);
        watchdogEnable(2000);

        // Infinite loop to trigger watchdog
        while (true) {
            // Do nothing - watchdog will reset the system
        }
        return;
    }

    // Unknown command
    Serial.print(F("ERROR: Unknown command '"));
    Serial.print(cmd);
    Serial.println(F("'"));
    Serial.println(F("Type HELP for available commands"));
}

#endif // USE_STATIC_CONFIG
