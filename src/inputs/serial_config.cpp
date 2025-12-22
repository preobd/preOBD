/*
 * serial_config.cpp - Serial Command Interface Implementation
 * NO String class - uses char* and fixed buffers for minimal RAM usage
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#include "../config.h"
#include "../version.h"

#ifndef USE_STATIC_CONFIG

#include "serial_config.h"
#include "input_manager.h"
#include "input.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/platform.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/units_registry.h"
#include "../lib/json_config.h"
#include "../outputs/output_base.h"
#include <string.h>
#include <ctype.h>

// Display control functions
extern void enableLCD();
extern void disableLCD();

// Display manager functions
#include "../lib/display_manager.h"

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

// Helper function to print system configuration details
static void printSystemConfig() {
    Serial.println(F("=== System Configuration ==="));
    extern uint8_t numActiveInputs;
    Serial.print(F("Active Inputs: "));
    Serial.println(numActiveInputs);
    Serial.print(F("System Voltage: "));
    Serial.print(SYSTEM_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("ADC Reference: "));
    Serial.print(AREF_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("ADC Resolution: "));
    Serial.print(ADC_RESOLUTION);
    Serial.println(F(" bits"));
    Serial.print(F("ADC Max Value: "));
    Serial.println(ADC_MAX_VALUE);
    Serial.print(F("Sea Level Pressure: "));
    Serial.print(systemConfig.seaLevelPressure);
    Serial.println(F(" hPa"));
    Serial.print(F("Intervals: Sensor="));
    Serial.print(systemConfig.sensorReadInterval);
    Serial.print(F("ms, Alarm="));
    Serial.print(systemConfig.alarmCheckInterval);
    Serial.print(F("ms, LCD="));
    Serial.print(systemConfig.lcdUpdateInterval);
    Serial.println(F("ms"));
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
 * Accepts "A0"-"A15" for analog pins, numeric strings for digital pins,
 * or "I2C" for I2C sensors (BME280, etc).
 */
static uint8_t parsePin(const char* pinStr, bool* isValid) {
    if (!pinStr) {
        if (isValid) *isValid = false;
        return 0;
    }

    if (isValid) *isValid = true;

    // Handle "I2C" keyword for I2C sensors (BME280, etc)
    // Use a virtual pin counter to allow multiple I2C sensors
    // Virtual pins start at 0xF0 (240) - well above any real pin number
    if (streq(pinStr, "I2C")) {
        static uint8_t i2cVirtualPinCounter = 0xF0;

        // Check if we've exceeded the virtual pin range
        if (i2cVirtualPinCounter >= 0xFE) {
            Serial.println(F("ERROR: Too many I2C sensors configured (max 14)"));
            if (isValid) *isValid = false;
            return 0;
        }

        return i2cVirtualPinCounter++;
    }

    // Handle "I2C:n" format for referencing existing I2C sensors (e.g., "I2C:0", "I2C:1")
    if (strncmp(pinStr, "I2C:", 4) == 0 || strncmp(pinStr, "i2c:", 4) == 0) {
        const char* numStr = pinStr + 4;
        int i2cIndex = atoi(numStr);

        // Validate I2C index range (0-13 for 14 total I2C sensors)
        if (i2cIndex < 0 || i2cIndex > 13) {
            Serial.print(F("ERROR: I2C index "));
            Serial.print(i2cIndex);
            Serial.println(F(" out of range (valid: 0-13)"));
            if (isValid) *isValid = false;
            return 0;
        }

        // Convert I2C index to virtual pin number
        uint8_t virtualPin = 0xF0 + i2cIndex;
        return virtualPin;
    }

    // Analog pins
    if (toupper(pinStr[0]) == 'A') {
        int analogNum = atoi(pinStr + 1);
        uint8_t pin = A0 + analogNum;

        // Validate against platform capabilities
        #if defined(__IMXRT1062__)  // Teensy 4.x
            #if defined(ARDUINO_TEENSY41)
                if (analogNum < 0 || analogNum > 17) {
                    if (isValid) *isValid = false;
                    Serial.print(F("ERROR: Pin A"));
                    Serial.print(analogNum);
                    Serial.println(F(" not available (Teensy 4.1 supports A0-A17)"));
                    return 0;
                }
            #else  // Teensy 4.0
                if (analogNum < 0 || analogNum > 13) {
                    if (isValid) *isValid = false;
                    Serial.print(F("ERROR: Pin A"));
                    Serial.print(analogNum);
                    Serial.println(F(" not available (Teensy 4.0 supports A0-A13)"));
                    return 0;
                }
            #endif
        #elif defined(__AVR_ATmega2560__)  // Arduino Mega
            if (analogNum < 0 || analogNum > 15) {
                if (isValid) *isValid = false;
                Serial.print(F("ERROR: Pin A"));
                Serial.print(analogNum);
                Serial.println(F(" not available (Arduino Mega supports A0-A15)"));
                return 0;
            }
        #elif defined(__AVR_ATmega328P__)  // Arduino Uno
            if (analogNum < 0 || analogNum > 5) {
                if (isValid) *isValid = false;
                Serial.print(F("ERROR: Pin A"));
                Serial.print(analogNum);
                Serial.println(F(" not available (Arduino Uno supports A0-A5)"));
                return 0;
            }
        #else
            // Generic validation using MAX_INPUTS from platform.h
            if (analogNum < 0 || analogNum >= MAX_INPUTS) {
                if (isValid) *isValid = false;
                Serial.print(F("ERROR: Pin A"));
                Serial.print(analogNum);
                Serial.println(F(" not available on this platform"));
                return 0;
            }
        #endif

        return pin;
    }

    // Digital pins
    uint8_t pin = atoi(pinStr);

    // Validate digital pin range
    #if defined(__IMXRT1062__)  // Teensy 4.x
        #if defined(ARDUINO_TEENSY41)
            if (pin > 54) {  // Teensy 4.1: pins 0-54
                if (isValid) *isValid = false;
                Serial.print(F("ERROR: Digital pin "));
                Serial.print(pin);
                Serial.println(F(" not available (Teensy 4.1 supports 0-54)"));
                return 0;
            }
        #else  // Teensy 4.0
            if (pin > 39) {  // Teensy 4.0: pins 0-39
                if (isValid) *isValid = false;
                Serial.print(F("ERROR: Digital pin "));
                Serial.print(pin);
                Serial.println(F(" not available (Teensy 4.0 supports 0-39)"));
                return 0;
            }
        #endif
    #elif defined(__AVR_ATmega2560__)  // Mega has 54 digital pins
        if (pin > 53) {
            if (isValid) *isValid = false;
            Serial.print(F("ERROR: Digital pin "));
            Serial.print(pin);
            Serial.println(F(" not available (Arduino Mega supports 0-53)"));
            return 0;
        }
    #elif defined(__AVR_ATmega328P__)  // Uno has 14 digital pins
        if (pin > 13) {
            if (isValid) *isValid = false;
            Serial.print(F("ERROR: Digital pin "));
            Serial.print(pin);
            Serial.println(F(" not available (Arduino Uno supports 0-13)"));
            return 0;
        }
    #else
        // For unknown platforms, allow any pin (no validation)
        // This prevents breaking support for new platforms
    #endif

    return pin;
}

// Parse Application index from string (registry-based)
static uint8_t parseApplication(const char* appStr) {
    return getApplicationIndexByName(appStr);
}

// Parse Sensor index from string (registry-based)
static uint8_t parseSensor(const char* sensorStr) {
    return getSensorIndexByName(sensorStr);
}

// Parse Units index from string (registry-based)
static uint8_t parseUnits(const char* unitsStr) {
    return getUnitsIndexByName(unitsStr);
}

void handleSerialCommand(char* cmd) {
    if (!cmd) return;

    trim(cmd);
    toUpper(cmd);

    // Parse command for multi-word commands (find first space)
    char* firstSpace = strchr(cmd, ' ');

    // ===== MODE COMMANDS (always available to prevent deadlock) =====
    if (streq(cmd, "CONFIG") || strncmp(cmd, "CONFIG ", 7) == 0) {
        // Check for CONFIG SAVE or CONFIG LOAD subcommands
        if (firstSpace) {
            char* subCmd = firstSpace + 1;
            toUpper(subCmd);

            if (strncmp(subCmd, "SAVE", 4) == 0) {
                // CONFIG SAVE [filename]
                char* filename = nullptr;
                char* filenameStart = subCmd + 4;
                while (*filenameStart == ' ') filenameStart++;  // Skip spaces
                if (*filenameStart != '\0') {
                    filename = filenameStart;
                }

                Serial.println();
                if (saveConfigToSD(filename)) {
                    Serial.println(F("Configuration saved successfully"));
                } else {
                    Serial.println(F("ERROR: Failed to save configuration"));
                }
                Serial.println();
                return;
            }

            if (strncmp(subCmd, "LOAD", 4) == 0) {
                // CONFIG LOAD <filename>
                char* filename = subCmd + 4;
                while (*filename == ' ') filename++;  // Skip spaces

                if (*filename == '\0') {
                    Serial.println(F("ERROR: Filename required for CONFIG LOAD"));
                    return;
                }

                Serial.println();
                if (loadConfigFromSD(filename)) {
                    Serial.println(F("Configuration loaded successfully"));
                    Serial.println(F("Type SAVE to persist to EEPROM"));
                } else {
                    Serial.println(F("ERROR: Failed to load configuration"));
                }
                Serial.println();
                return;
            }
        }

        // No subcommand or unknown subcommand - enter CONFIG mode
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
        Serial.println(F("  SET <pin> <app> <sensor>  - Combined config (e.g., SET 6 CHT MAX6675)"));
        Serial.println(F("  SET <pin> APPLICATION <application>  - Set measurement type"));
        Serial.println(F("  SET <pin> SENSOR <sensor>  - Set hardware sensor"));
        Serial.println(F("  SET <pin> NAME <name>  - Set abbreviated name (8 chars)"));
        Serial.println(F("  SET <pin> DISPLAY_NAME <name>  - Set full name (32 chars)"));
        Serial.println(F("  SET <pin> UNITS <units>  - Override display units"));
        Serial.println(F("  SET <pin> ALARM <min> <max>  - Set alarm thresholds"));
        Serial.println(F("  SET <pin> ALARM ENABLE  - Enable alarm for input"));
        Serial.println(F("  SET <pin> ALARM DISABLE  - Disable alarm for input"));
        Serial.println(F("  SET <pin> WARMUP <ms>  - Override alarm warmup time"));
        Serial.println(F("  SET <pin> PERSIST <ms>  - Override alarm persistence time"));
        Serial.println();
        Serial.println(F("Calibration Commands:"));
        Serial.println(F("  SET <pin> CALIBRATION PRESET  - Clear custom, use preset"));
        Serial.println(F("  SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
        Serial.println(F("  SET <pin> BIAS <resistor>  - Set bias resistor (Ohms)"));
        Serial.println(F("  SET <pin> STEINHART <bias> <a> <b> <c>  - Steinhart-Hart"));
        Serial.println(F("  SET <pin> BETA <bias> <beta> <r0> <t0>  - Beta equation"));
        Serial.println(F("  SET <pin> PRESSURE_POLY <bias> <a> <b> <c>  - VDO polynomial"));
        Serial.println(F("  INFO <pin> CALIBRATION  - Show calibration details"));
        Serial.println();
        Serial.println(F("Control Commands:"));
        Serial.println(F("  ENABLE <pin>  - Enable input reading"));
        Serial.println(F("  DISABLE <pin>  - Disable input reading"));
        Serial.println(F("  CLEAR <pin>  - Reset input to unconfigured"));
        Serial.println(F("  INFO <pin>  - Show detailed pin info"));
        Serial.println(F("  INFO <pin> ALARM  - Show alarm status and configuration"));
        Serial.println();
        Serial.println(F("Output Commands:"));
        Serial.println(F("  OUTPUT LIST  - Show all output modules"));
        Serial.println(F("  OUTPUT <name> ENABLE  - Enable output (CAN, RealDash, Serial, SD_Log)"));
        Serial.println(F("  OUTPUT <name> DISABLE  - Disable output"));
        Serial.println(F("  OUTPUT <name> INTERVAL <ms>  - Set output interval"));
        Serial.println();
        Serial.println(F("Display Commands:"));
        Serial.println(F("  DISPLAY STATUS  - Show display configuration"));
        Serial.println(F("  DISPLAY ENABLE  - Enable display"));
        Serial.println(F("  DISPLAY DISABLE  - Disable display"));
        Serial.println(F("  DISPLAY TYPE <LCD|OLED|NONE>  - Set display type"));
        Serial.println(F("  DISPLAY LCD ADDRESS <hex>  - Set I2C address (e.g., 0x27)"));
        Serial.println(F("  DISPLAY UNITS TEMP <C|F>  - Default temperature units"));
        Serial.println(F("  DISPLAY UNITS PRESSURE <BAR|PSI|KPA>  - Default pressure units"));
        Serial.println(F("  DISPLAY UNITS ELEVATION <M|FT>  - Default elevation units"));
        Serial.println();
        Serial.println(F("System Commands (Advanced):"));
        Serial.println(F("  SYSTEM STATUS  - Show system configuration"));
        Serial.println(F("  SYSTEM SEA_LEVEL <hPa>  - Sea level pressure"));
        Serial.println(F("  SYSTEM INTERVAL SENSOR <ms>  - Sensor read interval"));
        Serial.println(F("  SYSTEM INTERVAL ALARM <ms>  - Alarm check interval"));
        Serial.println(F("  SYSTEM INTERVAL LCD <ms>  - LCD update interval"));
        Serial.println();
        Serial.println(F("Config Commands:"));
        Serial.println(F("  SAVE  - Save config to EEPROM"));
        Serial.println(F("  LOAD  - Load config from EEPROM"));
        Serial.println(F("  RESET  - Clear all configuration"));
        Serial.println();
        Serial.println(F("System Commands:"));
        Serial.println(F("  CONFIG  - Enter configuration mode (unlock config)"));
        Serial.println(F("  RUN  - Enter run mode (lock config, resume sensors)"));
        Serial.println(F("  VERSION  - Display firmware and EEPROM version"));
        Serial.println(F("  DUMP  - Show full configuration (human-readable)"));
        Serial.println(F("  DUMP JSON  - Export configuration as JSON"));
        Serial.println(F("  CONFIG SAVE [filename]  - Save configuration to SD card"));
        Serial.println(F("  CONFIG LOAD <filename>  - Load configuration from SD card"));
        Serial.println(F("  RELOAD  - Trigger watchdog reset (system reboot)"));
        Serial.println();
        Serial.println(F("Examples:"));
        Serial.println(F("  SET 6 CHT MAX6675  (combined syntax)"));
        Serial.println(F("  SET A2 APPLICATION COOLANT_TEMP"));
        Serial.println(F("  SET A2 SENSOR VDO_120C_STEINHART"));
        Serial.println(F("  SET I2C AMBIENT_TEMP BME280_TEMP  (new I2C sensor)"));
        Serial.println(F("  SET I2C:0 ALARM 10 50  (modify existing I2C sensor)"));
        Serial.println(F("  INFO I2C:1  (query I2C sensor)"));
        Serial.println(F("  SET A1 PRESSURE_LINEAR 0.5 4.5 0 7  (custom pressure)"));
        Serial.println(F("  SET A0 BIAS 4700  (change bias resistor)"));
        Serial.println(F("  SET A2 ALARM 50 120  (set alarm thresholds)"));
        Serial.println(F("  SET A2 ALARM ENABLE  (enable alarm)"));
        Serial.println(F("  SET A2 WARMUP 30000  (30 second warmup)"));
        Serial.println(F("  SET A2 PERSIST 2000  (2 second persistence)"));
        Serial.println(F("  INFO A2 ALARM  (show alarm status)"));
        Serial.println(F("  ENABLE A2"));
        Serial.println(F("  OUTPUT CAN ENABLE"));
        Serial.println(F("  OUTPUT CAN INTERVAL 100"));
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

        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;

        // Try combined syntax: SET <pin> <application> <sensor>
        // Example: SET 6 CHT MAX6675
        char* firstToken = fieldAndValue;
        char* secondSpace = strchr(firstToken, ' ');
        if (secondSpace) {
            *secondSpace = '\0';
            char* secondToken = secondSpace + 1;
            trim(secondToken);

            uint8_t appIndex = parseApplication(firstToken);
            uint8_t sensorIndex = parseSensor(secondToken);

            if (appIndex != 0 && sensorIndex != 0) {  // 0 = NONE
                // Valid combined command
                // Check sensor/application compatibility
                MeasurementType sensorMeasType = getSensorMeasurementType(sensorIndex);
                MeasurementType appMeasType = getApplicationExpectedMeasurementType(appIndex);

                if (sensorMeasType != appMeasType) {
                    Serial.print(F("ERROR: Sensor/application type mismatch - "));
                    Serial.print(secondToken);
                    Serial.print(F(" measures "));
                    // Print measurement type names
                    switch(sensorMeasType) {
                        case MEASURE_TEMPERATURE: Serial.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: Serial.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: Serial.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: Serial.print(F("RPM")); break;
                        case MEASURE_HUMIDITY: Serial.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: Serial.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: Serial.print(F("DIGITAL")); break;
                    }
                    Serial.print(F(" but "));
                    Serial.print(firstToken);
                    Serial.print(F(" expects "));
                    switch(appMeasType) {
                        case MEASURE_TEMPERATURE: Serial.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: Serial.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: Serial.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: Serial.print(F("RPM")); break;
                        case MEASURE_HUMIDITY: Serial.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: Serial.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: Serial.print(F("DIGITAL")); break;
                    }
                    Serial.println();
                    return;  // Reject the configuration
                }

                // First set application (which also calls setInputSensor with preset sensor)
                if (setInputApplication(pin, appIndex)) {
                    // Then override sensor if different from preset
                    Input* input = getInputByPin(pin);
                    if (input && input->sensorIndex != sensorIndex) {
                        setInputSensor(pin, sensorIndex);
                    }

                    Serial.print(F("Input "));
                    Serial.print(pinStr);
                    Serial.print(F(" configured as "));
                    Serial.print(firstToken);
                    Serial.print(F(" with "));
                    Serial.println(secondToken);
                }
                return;
            }

            // Restore space for fallthrough to existing handlers
            *secondSpace = ' ';
        }

        // SET <pin> APPLICATION <application>
        if (strncmp(fieldAndValue, "APPLICATION ", 12) == 0) {
            char* appStr = fieldAndValue + 12;
            trim(appStr);
            uint8_t appIndex = parseApplication(appStr);
            if (appIndex == 0) {  // 0 = APP_NONE
                Serial.print(F("ERROR: Unknown application '"));
                Serial.print(appStr);
                Serial.println(F("'"));
                Serial.println(F("  Hint: Use 'LIST APPLICATIONS' to see valid options"));
                return;
            }
            if (setInputApplication(pin, appIndex)) {
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
            uint8_t sensorIndex = parseSensor(sensorStr);
            if (sensorIndex == 0) {  // 0 = SENSOR_NONE
                Serial.print(F("ERROR: Unknown sensor '"));
                Serial.print(sensorStr);
                Serial.println(F("'"));
                Serial.println(F("  Hint: Use 'LIST SENSORS' to see valid options"));
                return;
            }
            if (setInputSensor(pin, sensorIndex)) {
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
            uint8_t unitsIndex = parseUnits(unitsStr);
            if (setInputUnits(pin, unitsIndex)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" units set to "));
                Serial.println(unitsStr);
            }
            return;
        }

        // SET <pin> ALARM ENABLE
        if (strncmp(fieldAndValue, "ALARM ENABLE", 12) == 0) {
            if (enableInputAlarm(pin, true)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.println(F(" alarm enabled"));
            }
            return;
        }

        // SET <pin> ALARM DISABLE
        if (strncmp(fieldAndValue, "ALARM DISABLE", 13) == 0) {
            if (enableInputAlarm(pin, false)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.println(F(" alarm disabled"));
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

        // SET <pin> CALIBRATION PRESET
        // Clears custom calibration and reverts to sensor library preset
        if (strncmp(fieldAndValue, "CALIBRATION PRESET", 18) == 0) {
            Input* input = getInputByPin(pin);
            if (!input) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Clear custom calibration
            input->flags.useCustomCalibration = false;
            memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

            Serial.print(F("Cleared custom calibration for pin "));
            Serial.println(pinStr);
            Serial.println(F("Using preset calibration from sensor library"));
            return;
        }

        // SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
        // Supports 5 parameters (mult defaults to 1.0) or 6 parameters (custom mult)
        if (strncmp(fieldAndValue, "RPM ", 4) == 0) {
            char* params = fieldAndValue + 4;
            trim(params);

            // Count tokens to determine if calibration_mult is provided
            char paramsCopy[80];
            strncpy(paramsCopy, params, sizeof(paramsCopy) - 1);
            paramsCopy[sizeof(paramsCopy) - 1] = '\0';

            int tokenCount = 0;
            char* token = strtok(paramsCopy, " ");
            while (token != nullptr && tokenCount < 7) {
                tokenCount++;
                token = strtok(nullptr, " ");
            }

            if (tokenCount != 5 && tokenCount != 6) {
                Serial.println(F("ERROR: RPM requires 5 or 6 parameters"));
                Serial.println(F("  Usage: SET <pin> RPM <poles> <ratio> <timeout> <min> <max>"));
                Serial.println(F("     or: SET <pin> RPM <poles> <ratio> <mult> <timeout> <min> <max>"));
                Serial.println(F("  Example: SET 5 RPM 12 3.0 2000 100 8000"));
                Serial.println(F("       or: SET 5 RPM 12 3.0 1.02 2000 100 8000"));
                return;
            }

            // Parse parameters
            byte poles;
            float pulley_ratio;
            float calibration_mult = 1.0;  // Default
            uint16_t timeout_ms;
            uint16_t min_rpm;
            uint16_t max_rpm;

            token = strtok(params, " ");
            if (!token) { Serial.println(F("ERROR: Missing poles")); return; }
            poles = (byte)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { Serial.println(F("ERROR: Missing pulley_ratio")); return; }
            pulley_ratio = atof(token);

            if (tokenCount == 6) {
                // 6 parameters: custom calibration_mult provided
                token = strtok(nullptr, " ");
                if (!token) { Serial.println(F("ERROR: Missing calibration_mult")); return; }
                calibration_mult = atof(token);
            }

            token = strtok(nullptr, " ");
            if (!token) { Serial.println(F("ERROR: Missing timeout_ms")); return; }
            timeout_ms = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { Serial.println(F("ERROR: Missing min_rpm")); return; }
            min_rpm = (uint16_t)atoi(token);

            token = strtok(nullptr, " ");
            if (!token) { Serial.println(F("ERROR: Missing max_rpm")); return; }
            max_rpm = (uint16_t)atoi(token);

            // Validate parameters
            if (poles < 2 || poles > 32) {
                Serial.println(F("ERROR: Poles must be between 2 and 32"));
                return;
            }
            if (pulley_ratio < 0.5 || pulley_ratio > 10.0) {
                Serial.println(F("ERROR: Pulley ratio must be between 0.5 and 10.0"));
                return;
            }
            if (calibration_mult < 0.5 || calibration_mult > 2.0) {
                Serial.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
                return;
            }
            if (timeout_ms < 100 || timeout_ms > 10000) {
                Serial.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
                return;
            }
            if (min_rpm >= max_rpm) {
                Serial.println(F("ERROR: min_rpm must be less than max_rpm"));
                return;
            }

            // Apply custom calibration
            Input* input = getInputByPin(pin);
            if (!input) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Set custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.rpm.poles = poles;
            input->customCalibration.rpm.pulley_ratio = pulley_ratio;
            input->customCalibration.rpm.calibration_mult = calibration_mult;
            input->customCalibration.rpm.timeout_ms = timeout_ms;
            input->customCalibration.rpm.min_rpm = min_rpm;
            input->customCalibration.rpm.max_rpm = max_rpm;

            Serial.print(F("RPM calibration set for pin "));
            Serial.println(pinStr);
            Serial.print(F("  Poles: "));
            Serial.println(poles);
            Serial.print(F("  Pulley Ratio: "));
            Serial.print(pulley_ratio, 2);
            Serial.println(F(":1"));
            Serial.print(F("  Calibration Mult: "));
            Serial.println(calibration_mult, 4);
            Serial.print(F("  Timeout: "));
            Serial.print(timeout_ms);
            Serial.println(F(" ms"));
            Serial.print(F("  Valid Range: "));
            Serial.print(min_rpm);
            Serial.print(F("-"));
            Serial.print(max_rpm);
            Serial.println(F(" RPM"));
            float effective_ppr = (poles / 2.0) * pulley_ratio * calibration_mult;
            Serial.print(F("  Effective: "));
            Serial.print(effective_ppr, 2);
            Serial.println(F(" pulses/engine-rev"));
            return;
        }

        // SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
        if (strncmp(fieldAndValue, "PRESSURE_LINEAR ", 16) == 0) {
            char* params = fieldAndValue + 16;
            trim(params);

            // Parse 4 float parameters
            float vmin, vmax, pmin, pmax;
            char* token = strtok(params, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                Serial.println(F("  Usage: SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
                Serial.println(F("  Example: SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 7.0"));
                return;
            }
            vmin = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            vmax = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            pmin = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
                return;
            }
            pmax = atof(token);

            // Validate parameters
            if (vmin >= vmax) {
                Serial.println(F("ERROR: vmin must be less than vmax"));
                return;
            }
            if (vmin < 0.0 || vmax > SYSTEM_VOLTAGE) {
                Serial.print(F("ERROR: Voltage range must be 0.0-"));
                Serial.print(SYSTEM_VOLTAGE);
                Serial.println(F("V for this platform"));
                return;
            }
            if (pmin >= pmax) {
                Serial.println(F("ERROR: pmin must be less than pmax"));
                return;
            }
            if (pmin < 0.0) {
                Serial.println(F("ERROR: pmin must be >= 0.0"));
                return;
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.pressureLinear.voltage_min = vmin;
            input->customCalibration.pressureLinear.voltage_max = vmax;
            input->customCalibration.pressureLinear.output_min = pmin;
            input->customCalibration.pressureLinear.output_max = pmax;

            Serial.print(F("Pressure Linear calibration set for pin "));
            Serial.println(pinStr);
            Serial.print(F("  Voltage Range: "));
            Serial.print(vmin, 2);
            Serial.print(F("-"));
            Serial.print(vmax, 2);
            Serial.println(F(" V"));
            Serial.print(F("  Pressure Range: "));
            Serial.print(pmin, 2);
            Serial.print(F("-"));
            Serial.print(pmax, 2);
            Serial.println(F(" bar"));
            return;
        }

        // SET <pin> BIAS <resistor>
        // Generic bias resistor command - works with Steinhart-Hart, Lookup, and Pressure Polynomial
        if (strncmp(fieldAndValue, "BIAS ", 5) == 0) {
            char* biasStr = fieldAndValue + 5;
            trim(biasStr);
            float bias = atof(biasStr);

            // Get input
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Validate calibration type supports bias resistor
            if (input->calibrationType != CAL_THERMISTOR_STEINHART &&
                input->calibrationType != CAL_THERMISTOR_LOOKUP &&
                input->calibrationType != CAL_THERMISTOR_BETA &&
                input->calibrationType != CAL_PRESSURE_POLYNOMIAL) {
                Serial.print(F("ERROR: Calibration type "));
                Serial.print(input->calibrationType);
                Serial.println(F(" does not use bias resistor"));
                Serial.println(F("  BIAS works with: Thermistor (Steinhart-Hart), Thermistor (Beta), Thermistor (Lookup), Pressure (Polynomial)"));
                return;
            }

            // Validate value (10Ω to 10MΩ covers all practical thermistors)
            #define BIAS_R_MIN 10.0
            #define BIAS_R_MAX 10000000.0
            if (bias < BIAS_R_MIN || bias > BIAS_R_MAX) {
                Serial.print(F("ERROR: Bias resistor ("));
                Serial.print(bias, 1);
                Serial.println(F("Ω) must be between 10Ω and 10MΩ"));
                return;
            }

            // Apply to appropriate union member based on type
            input->flags.useCustomCalibration = true;

            if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
                input->customCalibration.steinhart.bias_resistor = bias;
            } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
                input->customCalibration.beta.bias_resistor = bias;
            } else if (input->calibrationType == CAL_THERMISTOR_LOOKUP) {
                input->customCalibration.lookup.bias_resistor = bias;
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
                input->customCalibration.pressurePolynomial.bias_resistor = bias;
            }

            Serial.print(F("Bias resistor set for pin "));
            Serial.print(pinStr);
            Serial.print(F(": "));
            Serial.print(bias, 1);
            Serial.println(F(" Ω"));
            return;
        }

        // SET <pin> STEINHART <bias_r> <a> <b> <c>
        if (strncmp(fieldAndValue, "STEINHART ", 10) == 0) {
            char* params = fieldAndValue + 10;
            trim(params);

            // Parse 4 float parameters
            float bias_r, a, b, c;
            char* token = strtok(params, " ");
            if (!token) {
                Serial.println(F("ERROR: STEINHART requires 4 parameters"));
                Serial.println(F("  Usage: SET <pin> STEINHART <bias_r> <a> <b> <c>"));
                Serial.println(F("  Example: SET A0 STEINHART 10000 0.001129 0.0002341 0.00000008775"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            a = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            b = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: STEINHART requires 4 parameters"));
                return;
            }
            c = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                Serial.println(F("ERROR: bias_r must be > 0"));
                return;
            }
            if (a == 0 || b == 0 || c == 0) {
                Serial.println(F("WARNING: Zero coefficient detected - may indicate error"));
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.steinhart.bias_resistor = bias_r;
            input->customCalibration.steinhart.steinhart_a = a;
            input->customCalibration.steinhart.steinhart_b = b;
            input->customCalibration.steinhart.steinhart_c = c;

            Serial.print(F("Steinhart-Hart calibration set for pin "));
            Serial.println(pinStr);
            Serial.print(F("  Bias Resistor: "));
            Serial.print(bias_r, 1);
            Serial.println(F(" Ω"));
            Serial.print(F("  A: "));
            Serial.println(a, 10);
            Serial.print(F("  B: "));
            Serial.println(b, 10);
            Serial.print(F("  C: "));
            Serial.println(c, 10);
            return;
        }

        // SET <pin> BETA <bias_r> <beta> <r0> <t0>
        if (strncmp(fieldAndValue, "BETA ", 5) == 0) {
            char* params = fieldAndValue + 5;
            trim(params);

            // Parse 4 float parameters
            float bias_r, beta, r0, t0;
            char* token = strtok(params, " ");
            if (!token) {
                Serial.println(F("ERROR: BETA requires 4 parameters"));
                Serial.println(F("  Usage: SET <pin> BETA <bias_r> <beta> <r0> <t0>"));
                Serial.println(F("  Example: SET A0 BETA 10000 3950 10000 25"));
                Serial.println(F("  Where: bias_r=bias resistor (Ω), beta=β coefficient (K),"));
                Serial.println(F("         r0=ref resistance (Ω), t0=ref temp (°C, typically 25)"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            beta = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            r0 = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: BETA requires 4 parameters"));
                return;
            }
            t0 = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                Serial.println(F("ERROR: bias_r must be > 0"));
                return;
            }
            if (beta < 1000 || beta > 10000) {
                Serial.println(F("WARNING: Beta typically 2000-6000K. Value may be incorrect."));
            }
            if (r0 <= 0) {
                Serial.println(F("ERROR: r0 must be > 0"));
                return;
            }
            if (t0 < -40 || t0 > 150) {
                Serial.println(F("WARNING: t0 typically 25°C. Value may be incorrect."));
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->calibrationType = CAL_THERMISTOR_BETA;
            input->customCalibration.beta.bias_resistor = bias_r;
            input->customCalibration.beta.beta = beta;
            input->customCalibration.beta.r0 = r0;
            input->customCalibration.beta.t0 = t0;

            Serial.print(F("Beta calibration set for pin "));
            Serial.println(pinStr);
            Serial.print(F("  Bias Resistor: "));
            Serial.print(bias_r, 1);
            Serial.println(F(" Ω"));
            Serial.print(F("  Beta: "));
            Serial.print(beta, 1);
            Serial.println(F(" K"));
            Serial.print(F("  R0: "));
            Serial.print(r0, 1);
            Serial.println(F(" Ω"));
            Serial.print(F("  T0: "));
            Serial.print(t0, 1);
            Serial.println(F(" °C"));
            return;
        }

        // SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>
        if (strncmp(fieldAndValue, "PRESSURE_POLY ", 14) == 0) {
            char* params = fieldAndValue + 14;
            trim(params);

            // Parse 4 float parameters
            float bias_r, a, b, c;
            char* token = strtok(params, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                Serial.println(F("  Usage: SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>"));
                Serial.println(F("  Example: SET A1 PRESSURE_POLY 184 -6.75e-4 2.54e-6 1.87e-9"));
                return;
            }
            bias_r = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            a = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            b = atof(token);

            token = strtok(nullptr, " ");
            if (!token) {
                Serial.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
                return;
            }
            c = atof(token);

            // Validate parameters
            if (bias_r <= 0) {
                Serial.println(F("ERROR: bias_r must be > 0"));
                return;
            }

            // Get input and apply calibration
            Input* input = getInputByPin(pin);
            if (!input || !input->flags.isEnabled) {
                Serial.println(F("ERROR: Input not configured"));
                return;
            }

            // Apply custom calibration
            input->flags.useCustomCalibration = true;
            input->customCalibration.pressurePolynomial.bias_resistor = bias_r;
            input->customCalibration.pressurePolynomial.poly_a = a;
            input->customCalibration.pressurePolynomial.poly_b = b;
            input->customCalibration.pressurePolynomial.poly_c = c;

            Serial.print(F("Pressure Polynomial calibration set for pin "));
            Serial.println(pinStr);
            Serial.print(F("  Bias Resistor: "));
            Serial.print(bias_r, 1);
            Serial.println(F(" Ω"));
            Serial.print(F("  A: "));
            Serial.println(a, 10);
            Serial.print(F("  B: "));
            Serial.println(b, 10);
            Serial.print(F("  C: "));
            Serial.println(c, 10);
            return;
        }

        // SET <pin> WARMUP <ms>
        if (strncmp(fieldAndValue, "WARMUP ", 7) == 0) {
            char* valueStr = fieldAndValue + 7;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value > 300000) {  // Max 5 minutes
                Serial.println(F("ERROR: Warmup time must be 0-300000ms"));
                return;
            }
            if (setInputAlarmWarmup(pin, value)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" alarm warmup set to "));
                Serial.print(value);
                Serial.println(F("ms"));
            }
            return;
        }

        // SET <pin> PERSIST <ms>
        if (strncmp(fieldAndValue, "PERSIST ", 8) == 0) {
            char* valueStr = fieldAndValue + 8;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value > 60000) {  // Max 60 seconds
                Serial.println(F("ERROR: Persistence time must be 0-60000ms"));
                return;
            }
            if (setInputAlarmPersist(pin, value)) {
                Serial.print(F("Input "));
                Serial.print(pinStr);
                Serial.print(F(" alarm persistence set to "));
                Serial.print(value);
                Serial.println(F("ms"));
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
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
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
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
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
        bool pinValid = false;
        uint8_t pin = parsePin(pinStr, &pinValid);
        if (!pinValid) return;
        if (clearInput(pin)) {
            Serial.print(F("Input "));
            Serial.print(pinStr);
            Serial.println(F(" cleared"));
        }
        return;
    }

    // ===== OUTPUT COMMANDS =====
    if (strncmp(cmd, "OUTPUT ", 7) == 0) {
        char* rest = cmd + 7;
        trim(rest);

        // OUTPUT LIST / OUTPUT STATUS
        if (streq(rest, "LIST") || streq(rest, "STATUS")) {
            listOutputs();
            return;
        }

        // Parse: OUTPUT <name> <action> [value]
        char* firstSpace = strchr(rest, ' ');
        if (!firstSpace) {
            Serial.println(F("ERROR: Invalid OUTPUT syntax"));
            Serial.println(F("  Usage: OUTPUT <name> <ENABLE|DISABLE|INTERVAL> [ms]"));
            return;
        }

        *firstSpace = '\0';
        char* outputName = rest;
        char* action = firstSpace + 1;
        trim(action);

        OutputModule* output = getOutputByName(outputName);
        if (!output) {
            Serial.print(F("ERROR: Unknown output '"));
            Serial.print(outputName);
            Serial.println(F("'"));
            Serial.println(F("  Hint: Use 'OUTPUT LIST' to see available outputs"));
            return;
        }

        // OUTPUT <name> ENABLE
        if (streq(action, "ENABLE")) {
            if (setOutputEnabled(outputName, true)) {
                Serial.print(output->name);
                Serial.println(F(" output enabled"));
            }
            return;
        }

        // OUTPUT <name> DISABLE
        if (streq(action, "DISABLE")) {
            if (setOutputEnabled(outputName, false)) {
                Serial.print(output->name);
                Serial.println(F(" output disabled"));
            }
            return;
        }

        // OUTPUT <name> INTERVAL <ms>
        if (strncmp(action, "INTERVAL ", 9) == 0) {
            char* intervalStr = action + 9;
            trim(intervalStr);
            uint16_t interval = atoi(intervalStr);

            if (interval < 10 || interval > 60000) {
                Serial.println(F("ERROR: Interval must be 10-60000ms"));
                return;
            }

            if (setOutputInterval(outputName, interval)) {
                Serial.print(output->name);
                Serial.print(F(" interval set to "));
                Serial.print(interval);
                Serial.println(F("ms"));
            }
            return;
        }

        Serial.println(F("ERROR: Unknown OUTPUT action"));
        Serial.println(F("  Valid actions: ENABLE, DISABLE, INTERVAL <ms>"));
        return;
    }

    // ===== DISPLAY COMMANDS =====
    if (strncmp(cmd, "DISPLAY ", 8) == 0) {
        char* rest = cmd + 8;
        trim(rest);

        // DISPLAY STATUS
        if (streq(rest, "STATUS")) {
            Serial.println(F("=== Display Configuration ==="));
            Serial.print(F("Status: "));
            Serial.println(systemConfig.displayEnabled ? F("Enabled") : F("Disabled"));
            Serial.print(F("Type: "));
            switch (systemConfig.displayType) {
                case DISPLAY_NONE: Serial.println(F("None")); break;
                case DISPLAY_LCD: Serial.println(F("LCD")); break;
                case DISPLAY_OLED: Serial.println(F("OLED")); break;
                default: Serial.println(F("Unknown")); break;
            }
            Serial.print(F("LCD I2C Address: 0x"));
            Serial.println(systemConfig.lcdI2CAddress, HEX);
            Serial.print(F("Temperature Units: "));
            Serial.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
            Serial.print(F("Pressure Units: "));
            Serial.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
            Serial.print(F("Elevation Units: "));
            Serial.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
            return;
        }

        // DISPLAY ENABLE
        if (streq(rest, "ENABLE")) {
            systemConfig.displayEnabled = 1;    // Update config (for SAVE)
            setDisplayRuntime(true);             // Update runtime immediately
            Serial.println(F("✓ Display enabled (use SAVE to persist)"));
            return;
        }

        // DISPLAY DISABLE
        if (streq(rest, "DISABLE")) {
            systemConfig.displayEnabled = 0;    // Update config (for SAVE)
            setDisplayRuntime(false);            // Update runtime immediately
            Serial.println(F("✓ Display disabled (use SAVE to persist)"));
            return;
        }

        // DISPLAY TYPE <LCD|OLED|NONE>
        if (strncmp(rest, "TYPE ", 5) == 0) {
            char* typeStr = rest + 5;
            trim(typeStr);
            if (streq(typeStr, "LCD")) {
                systemConfig.displayType = DISPLAY_LCD;
                Serial.println(F("Display type set to LCD"));
            } else if (streq(typeStr, "OLED")) {
                systemConfig.displayType = DISPLAY_OLED;
                Serial.println(F("Display type set to OLED"));
            } else if (streq(typeStr, "NONE")) {
                systemConfig.displayType = DISPLAY_NONE;
                Serial.println(F("Display disabled"));
            } else {
                Serial.println(F("ERROR: Invalid display type. Valid: LCD, OLED, NONE"));
            }
            return;
        }

        // DISPLAY LCD ADDRESS <hex>
        if (strncmp(rest, "LCD ADDRESS ", 12) == 0) {
            char* addrStr = rest + 12;
            trim(addrStr);
            // Parse hex address (e.g., "0x27" or "27")
            char* endptr;
            long addr = strtol(addrStr, &endptr, 16);
            if (addr >= 0x03 && addr <= 0x77) {
                systemConfig.lcdI2CAddress = (uint8_t)addr;
                Serial.print(F("LCD I2C address set to 0x"));
                Serial.println(systemConfig.lcdI2CAddress, HEX);
            } else {
                Serial.println(F("ERROR: Invalid I2C address (valid range: 0x03-0x77)"));
            }
            return;
        }

        // DISPLAY UNITS TEMP <C|F|CELSIUS|FAHRENHEIT>
        if (strncmp(rest, "UNITS TEMP ", 11) == 0) {
            char* unitStr = rest + 11;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_TEMPERATURE) {
                systemConfig.defaultTempUnits = index;
                Serial.print(F("Default temperature units set to "));
                Serial.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                Serial.println(F("ERROR: Invalid units. Valid: C, F, CELSIUS, FAHRENHEIT"));
            }
            return;
        }

        // DISPLAY UNITS PRESSURE <BAR|PSI|KPA>
        if (strncmp(rest, "UNITS PRESSURE ", 15) == 0) {
            char* unitStr = rest + 15;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_PRESSURE) {
                systemConfig.defaultPressUnits = index;
                Serial.print(F("Default pressure units set to "));
                Serial.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                Serial.println(F("ERROR: Invalid units. Valid: BAR, PSI, KPA, INHG"));
            }
            return;
        }

        // DISPLAY UNITS ELEVATION <M|FT|METERS|FEET>
        if (strncmp(rest, "UNITS ELEVATION ", 16) == 0) {
            char* unitStr = rest + 16;
            trim(unitStr);
            uint8_t index = getUnitsIndexByName(unitStr);
            const UnitsInfo* info = getUnitsByIndex(index);
            if (info && pgm_read_byte(&info->measurementType) == MEASURE_ELEVATION) {
                systemConfig.defaultElevUnits = index;
                Serial.print(F("Default elevation units set to "));
                Serial.println((__FlashStringHelper*)getUnitStringByIndex(index));
            } else {
                Serial.println(F("ERROR: Invalid units. Valid: M, FT, METERS, FEET"));
            }
            return;
        }

        Serial.println(F("ERROR: Unknown DISPLAY command"));
        Serial.println(F("  Valid commands:"));
        Serial.println(F("    DISPLAY STATUS"));
        Serial.println(F("    DISPLAY ENABLE"));
        Serial.println(F("    DISPLAY DISABLE"));
        Serial.println(F("    DISPLAY TYPE <LCD|OLED|NONE>"));
        Serial.println(F("    DISPLAY LCD ADDRESS <hex>"));
        Serial.println(F("    DISPLAY UNITS TEMP <C|F>"));
        Serial.println(F("    DISPLAY UNITS PRESSURE <BAR|PSI|KPA>"));
        Serial.println(F("    DISPLAY UNITS ELEVATION <M|FT>"));
        return;
    }

    // ===== SYSTEM COMMANDS =====
    if (strncmp(cmd, "SYSTEM ", 7) == 0) {
        char* rest = cmd + 7;
        trim(rest);

        // SYSTEM STATUS
        if (streq(rest, "STATUS")) {
            // Platform identification
            Serial.println(F("=== Platform Information ==="));
            Serial.print(F("Platform: "));
            Serial.println(F(PLATFORM_NAME));
            Serial.print(F("I2C Clock: "));
            Serial.println(F(I2C_CLOCK_SPEED));
            Serial.print(F("Features: Serial"));
            #ifdef USE_BME280
                Serial.print(F(", BME280"));
            #endif
            #ifdef ENABLE_LCD
                Serial.print(F(", LCD"));
            #endif
            #ifdef ENABLE_TEST_MODE
                Serial.print(F(", Test Mode"));
            #endif
            #ifdef ENABLE_CAN
                Serial.print(F(", CAN"));
            #endif
            Serial.println();
            Serial.println();

            printSystemConfig();
            Serial.println();
            Serial.println(F("Compile-Time Defaults:"));
            Serial.print(F("  Default Bias Resistor: "));
            Serial.print(DEFAULT_BIAS_RESISTOR);
            Serial.println(F(" Ω"));
            Serial.println();
            Serial.println(F("Hardware Pins:"));
            Serial.print(F("  Mode Button: "));
            Serial.println(systemConfig.modeButtonPin);
            Serial.print(F("  Buzzer: "));
            Serial.println(systemConfig.buzzerPin);
            Serial.print(F("  CAN CS: "));
            Serial.println(systemConfig.canCSPin);
            Serial.print(F("  CAN INT: "));
            Serial.println(systemConfig.canIntPin);
            Serial.print(F("  SD CS: "));
            Serial.println(systemConfig.sdCSPin);
            if (systemConfig.testModePin != 0xFF) {
                Serial.print(F("  Test Mode Pin: "));
                Serial.println(systemConfig.testModePin);
            }
            return;
        }

        // SYSTEM SEA_LEVEL <hPa>
        if (strncmp(rest, "SEA_LEVEL ", 10) == 0) {
            char* valueStr = rest + 10;
            trim(valueStr);
            float value = atof(valueStr);
            if (value >= 800 && value <= 1200) {
                systemConfig.seaLevelPressure = value;
                Serial.print(F("Sea level pressure set to "));
                Serial.print(value);
                Serial.println(F(" hPa"));
            } else {
                Serial.println(F("ERROR: Sea level pressure must be 800-1200 hPa"));
            }
            return;
        }

        // SYSTEM INTERVAL SENSOR <ms>
        if (strncmp(rest, "INTERVAL SENSOR ", 16) == 0) {
            char* valueStr = rest + 16;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.sensorReadInterval = value;
                Serial.print(F("Sensor read interval set to "));
                Serial.print(value);
                Serial.println(F("ms"));
            } else {
                Serial.println(F("ERROR: Sensor interval must be 10-10000ms"));
            }
            return;
        }

        // SYSTEM INTERVAL ALARM <ms>
        if (strncmp(rest, "INTERVAL ALARM ", 15) == 0) {
            char* valueStr = rest + 15;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.alarmCheckInterval = value;
                Serial.print(F("Alarm check interval set to "));
                Serial.print(value);
                Serial.println(F("ms"));
            } else {
                Serial.println(F("ERROR: Alarm interval must be 10-10000ms"));
            }
            return;
        }

        // SYSTEM INTERVAL LCD <ms>
        if (strncmp(rest, "INTERVAL LCD ", 13) == 0) {
            char* valueStr = rest + 13;
            trim(valueStr);
            uint16_t value = atoi(valueStr);
            if (value >= 10 && value <= 10000) {
                systemConfig.lcdUpdateInterval = value;
                Serial.print(F("LCD update interval set to "));
                Serial.print(value);
                Serial.println(F("ms"));
            } else {
                Serial.println(F("ERROR: LCD interval must be 10-10000ms"));
            }
            return;
        }

        Serial.println(F("ERROR: Unknown SYSTEM command"));
        Serial.println(F("  Valid commands:"));
        Serial.println(F("    SYSTEM STATUS"));
        Serial.println(F("    SYSTEM SEA_LEVEL <hPa>"));
        Serial.println(F("    SYSTEM INTERVAL SENSOR <ms>"));
        Serial.println(F("    SYSTEM INTERVAL ALARM <ms>"));
        Serial.println(F("    SYSTEM INTERVAL LCD <ms>"));
        return;
    }

    // ===== INFO COMMAND =====
    if (strncmp(cmd, "INFO ", 5) == 0) {
        char* rest = cmd + 5;
        trim(rest);

        // Check for INFO <pin> CALIBRATION
        char* spacePos = strchr(rest, ' ');
        if (spacePos) {
            *spacePos = '\0';
            char* pinStr = rest;
            char* subcommand = spacePos + 1;
            trim(subcommand);

            if (streq(subcommand, "ALARM")) {
                bool pinValid = false;
                uint8_t pin = parsePin(pinStr, &pinValid);
                if (!pinValid) return;
                Input* input = getInputByPin(pin);
                if (!input || !input->flags.isEnabled) {
                    Serial.println(F("ERROR: Input not configured"));
                    return;
                }

                Serial.print(F("Pin "));
                Serial.print(pinStr);
                Serial.println(F(" Alarm Configuration:"));

                Serial.print(F("  Alarm Enabled: "));
                Serial.println(input->flags.alarm ? F("YES") : F("NO"));

                Serial.print(F("  Alarm State: "));
                switch (input->alarmContext.state) {
                    case ALARM_DISABLED: Serial.println(F("DISABLED")); break;
                    case ALARM_INIT: Serial.println(F("INIT")); break;
                    case ALARM_WARMUP: Serial.println(F("WARMUP")); break;
                    case ALARM_READY: Serial.println(F("READY")); break;
                    case ALARM_ACTIVE: Serial.println(F("ACTIVE")); break;
                    default: Serial.println(F("UNKNOWN")); break;
                }

                Serial.print(F("  Currently In Alarm: "));
                Serial.println(input->flags.isInAlarm ? F("YES") : F("NO"));

                Serial.print(F("  Warmup Time: "));
                Serial.print(input->alarmContext.warmupTime_ms);
                Serial.println(F("ms"));

                Serial.print(F("  Persistence Time: "));
                Serial.print(input->alarmContext.persistTime_ms);
                Serial.println(F("ms"));

                Serial.print(F("  Thresholds: "));
                Serial.print(input->minValue);
                Serial.print(F(" - "));
                Serial.print(input->maxValue);
                Serial.print(F(" "));
                Serial.println((__FlashStringHelper*)getUnitStringByIndex(input->unitsIndex));

                return;
            }

            if (streq(subcommand, "CALIBRATION")) {
                bool pinValid = false;
                uint8_t pin = parsePin(pinStr, &pinValid);
                if (!pinValid) return;
                Input* input = getInputByPin(pin);
                if (!input || !input->flags.isEnabled) {
                    Serial.println(F("ERROR: Input not configured"));
                    return;
                }

                Serial.print(F("Pin "));
                Serial.print(pinStr);
                Serial.println(F(" Calibration:"));
                Serial.print(F("  Type: "));
                switch (input->calibrationType) {
                    case CAL_THERMISTOR_STEINHART: Serial.println(F("THERMISTOR_STEINHART")); break;
                    case CAL_THERMISTOR_BETA: Serial.println(F("THERMISTOR_BETA")); break;
                    case CAL_THERMISTOR_LOOKUP: Serial.println(F("THERMISTOR_LOOKUP")); break;
                    case CAL_PRESSURE_POLYNOMIAL: Serial.println(F("PRESSURE_POLYNOMIAL")); break;
                    case CAL_LINEAR: Serial.println(F("PRESSURE_LINEAR")); break;
                    case CAL_VOLTAGE_DIVIDER: Serial.println(F("VOLTAGE_DIVIDER")); break;
                    case CAL_RPM: Serial.println(F("RPM")); break;
                    default: Serial.println(F("NONE")); break;
                }

                Serial.print(F("  Source: "));
                if (input->flags.useCustomCalibration) {
                    Serial.println(F("Custom (RAM)"));

                    // Print custom calibration values based on type
                    if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
                        Serial.print(F("  Bias Resistor: "));
                        Serial.print(input->customCalibration.steinhart.bias_resistor, 1);
                        Serial.println(F(" Ω"));
                        Serial.print(F("  A: "));
                        Serial.println(input->customCalibration.steinhart.steinhart_a, 10);
                        Serial.print(F("  B: "));
                        Serial.println(input->customCalibration.steinhart.steinhart_b, 10);
                        Serial.print(F("  C: "));
                        Serial.println(input->customCalibration.steinhart.steinhart_c, 10);
                    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
                        Serial.print(F("  Bias Resistor: "));
                        Serial.print(input->customCalibration.beta.bias_resistor, 1);
                        Serial.println(F(" Ω"));
                        Serial.print(F("  Beta: "));
                        Serial.print(input->customCalibration.beta.beta, 1);
                        Serial.println(F(" K"));
                        Serial.print(F("  R0: "));
                        Serial.print(input->customCalibration.beta.r0, 1);
                        Serial.println(F(" Ω"));
                        Serial.print(F("  T0: "));
                        Serial.print(input->customCalibration.beta.t0, 1);
                        Serial.println(F(" °C"));
                    } else if (input->calibrationType == CAL_THERMISTOR_LOOKUP) {
                        Serial.print(F("  Bias Resistor: "));
                        Serial.print(input->customCalibration.lookup.bias_resistor, 1);
                        Serial.println(F(" Ω"));
                    } else if (input->calibrationType == CAL_LINEAR) {
                        Serial.print(F("  Voltage Range: "));
                        Serial.print(input->customCalibration.pressureLinear.voltage_min, 2);
                        Serial.print(F("-"));
                        Serial.print(input->customCalibration.pressureLinear.voltage_max, 2);
                        Serial.println(F(" V"));
                        Serial.print(F("  Pressure Range: "));
                        Serial.print(input->customCalibration.pressureLinear.output_min, 2);
                        Serial.print(F("-"));
                        Serial.print(input->customCalibration.pressureLinear.output_max, 2);
                        Serial.println(F(" bar"));
                    } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
                        Serial.print(F("  Bias Resistor: "));
                        Serial.print(input->customCalibration.pressurePolynomial.bias_resistor, 1);
                        Serial.println(F(" Ω"));
                        Serial.print(F("  A: "));
                        Serial.println(input->customCalibration.pressurePolynomial.poly_a, 10);
                        Serial.print(F("  B: "));
                        Serial.println(input->customCalibration.pressurePolynomial.poly_b, 10);
                        Serial.print(F("  C: "));
                        Serial.println(input->customCalibration.pressurePolynomial.poly_c, 10);
                    } else if (input->calibrationType == CAL_RPM) {
                        Serial.print(F("  Poles: "));
                        Serial.println(input->customCalibration.rpm.poles);
                        Serial.print(F("  Pulley Ratio: "));
                        Serial.println(input->customCalibration.rpm.pulley_ratio, 2);
                        Serial.print(F("  Calibration Mult: "));
                        Serial.println(input->customCalibration.rpm.calibration_mult, 4);
                        Serial.print(F("  Timeout: "));
                        Serial.print(input->customCalibration.rpm.timeout_ms);
                        Serial.println(F(" ms"));
                        Serial.print(F("  RPM Range: "));
                        Serial.print(input->customCalibration.rpm.min_rpm);
                        Serial.print(F("-"));
                        Serial.println(input->customCalibration.rpm.max_rpm);
                    }
                } else {
                    Serial.println(F("Preset (PROGMEM)"));
                }
                return;
            }

            // Restore space for unknown subcommand
            *spacePos = ' ';
        }

        // Default: INFO <pin>
        bool pinValid = false;
        uint8_t pin = parsePin(rest, &pinValid);
        if (!pinValid) return;
        printInputInfo(pin);
        return;
    }

    // ===== PERSISTENCE COMMANDS =====
    // EEPROM save/load/reset
    if (streq(cmd, "SAVE")) {
        bool success = true;
        success &= saveInputConfig();
        success &= saveSystemConfig();

        if (success) {
            Serial.println(F("✓ Configuration saved to EEPROM"));
        } else {
            Serial.println(F("ERROR: Failed to save configuration"));
        }
        return;
    }

    if (streq(cmd, "LOAD")) {
        bool inputsOK = loadInputConfig();
        bool systemOK = loadSystemConfig();

        if (inputsOK && systemOK) {
            Serial.println(F("✓ Configuration loaded from EEPROM"));
        } else if (inputsOK || systemOK) {
            Serial.println(F("WARNING: Partial configuration loaded"));
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
        resetSystemConfig();
        Serial.println(F("✓ All configuration reset to defaults"));
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
    if (streq(cmd, "DUMP") || strncmp(cmd, "DUMP ", 5) == 0) {
        // Check for "DUMP JSON" variant
        if (firstSpace) {
            char* subCmd = firstSpace + 1;
            if (streq(subCmd, "JSON")) {
                Serial.println();
                dumpConfigToJSON(Serial);
                Serial.println();
                return;
            }
        }

        // Regular DUMP (human-readable)
        Serial.println();
        Serial.println(F("========================================"));
        Serial.println(F("  Full Configuration Dump"));
        Serial.println(F("========================================"));
        Serial.println();

        // Show inputs
        listAllInputs();
        Serial.println();

        // Show outputs
        listOutputs();
        Serial.println();

        // Show display config
        Serial.println(F("=== Display Configuration ==="));
        Serial.print(F("Type: "));
        switch (systemConfig.displayType) {
            case DISPLAY_NONE: Serial.println(F("None")); break;
            case DISPLAY_LCD: Serial.println(F("LCD")); break;
            case DISPLAY_OLED: Serial.println(F("OLED")); break;
            default: Serial.println(F("Unknown")); break;
        }
        Serial.print(F("LCD I2C Address: 0x"));
        Serial.println(systemConfig.lcdI2CAddress, HEX);
        Serial.print(F("Default Units: Temp="));
        Serial.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
        Serial.print(F(", Press="));
        Serial.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
        Serial.print(F(", Elev="));
        Serial.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
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
