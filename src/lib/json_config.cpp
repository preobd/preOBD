/*
 * json_config.cpp - JSON Configuration Export/Import Implementation
 *
 * NOTE: This entire file is excluded from static config builds to save memory.
 *       JSON features are only available in EEPROM mode (runtime config).
 */

#include "../config.h"

// Only compile JSON features for EEPROM mode (not static config)
#ifndef USE_STATIC_CONFIG

#include "json_config.h"
#include "system_config.h"
#include "bus_defaults.h"
#include "serial_manager.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "units_registry.h"
#include "sensor_library.h"
#include "application_presets.h"
#include "../version.h"
#include "message_api.h"
#include "log_tags.h"
#include "pin_registry.h"
#include "watchdog.h"

// Use Arduino SD library for consistency across platforms
#include <SD.h>
#include "sd_manager.h"

// SD object is provided globally by SD.h

// External references
extern Input inputs[MAX_INPUTS];
extern SystemConfig systemConfig;
extern uint8_t numActiveInputs;

// Get current timestamp (seconds since epoch, if available)
// For now, we'll use millis() / 1000 as a placeholder
static uint32_t getCurrentTimestamp() {
    return millis() / 1000;
}

// Get platform string
static const char* getPlatformString() {
#if defined(TEENSY_40)
    return "TEENSY40";
#elif defined(TEENSY_41)
    return "TEENSY41";
#elif defined(TEENSY_36)
    return "TEENSY36";
#elif defined(TEENSY_35)
    return "TEENSY35";
#elif defined(TEENSY_32)
    return "TEENSY32";
#elif defined(TEENSY_31)
    return "TEENSY31";
#elif defined(ARDUINO_MEGA)
    return "MEGA2560";
#elif defined(ARDUINO_UNO)
    return "UNO";
#elif defined(ARDUINO_DUE)
    return "DUE";
#elif defined(ESP32)
    return "ESP32";
#else
    return "UNKNOWN";
#endif
}

// Get calibration type string
static const char* getCalibrationType(const Input* input) {
    switch (input->calibrationType) {
        case CAL_NONE: return "NONE";
        case CAL_THERMISTOR_STEINHART: return "THERMISTOR_STEINHART";
        case CAL_THERMISTOR_BETA: return "THERMISTOR_BETA";
        case CAL_THERMISTOR_TABLE: return "THERMISTOR_TABLE";
        case CAL_PRESSURE_TABLE: return "PRESSURE_TABLE";
        case CAL_PRESSURE_POLYNOMIAL: return "PRESSURE_POLYNOMIAL";
        case CAL_LINEAR: return "LINEAR";
        case CAL_VOLTAGE_DIVIDER: return "VOLTAGE_DIVIDER";
        case CAL_RPM: return "RPM";
        default: return "UNKNOWN";
    }
}

// Export calibration parameters to JSON
static void exportCalibration(JsonObject& calObj, const Input* input) {
    calObj["type"] = getCalibrationType(input);

    if (!input->flags.useCustomCalibration) {
        calObj["source"] = "PRESET";
        return;
    }

    calObj["source"] = "CUSTOM";
    JsonObject params = calObj["params"].to<JsonObject>();

    switch (input->calibrationType) {
        case CAL_THERMISTOR_STEINHART:
            params["biasResistor"] = input->customCalibration.steinhart.bias_resistor;
            params["steinhartA"] = input->customCalibration.steinhart.steinhart_a;
            params["steinhartB"] = input->customCalibration.steinhart.steinhart_b;
            params["steinhartC"] = input->customCalibration.steinhart.steinhart_c;
            break;

        case CAL_THERMISTOR_BETA:
            params["biasResistor"] = input->customCalibration.beta.bias_resistor;
            params["beta"] = input->customCalibration.beta.beta;
            params["r0"] = input->customCalibration.beta.r0;
            params["t0"] = input->customCalibration.beta.t0;
            break;

        case CAL_THERMISTOR_TABLE:
            params["biasResistor"] = input->customCalibration.lookup.bias_resistor;
            break;

        case CAL_PRESSURE_TABLE:
            // Pressure tables use same union member as thermistor lookup (only bias resistor is customizable)
            params["biasResistor"] = input->customCalibration.lookup.bias_resistor;
            break;

        case CAL_LINEAR:
            params["voltageMin"] = input->customCalibration.pressureLinear.voltage_min;
            params["voltageMax"] = input->customCalibration.pressureLinear.voltage_max;
            params["outputMin"] = input->customCalibration.pressureLinear.output_min;
            params["outputMax"] = input->customCalibration.pressureLinear.output_max;
            break;

        case CAL_PRESSURE_POLYNOMIAL:
            params["biasResistor"] = input->customCalibration.pressurePolynomial.bias_resistor;
            params["polyA"] = input->customCalibration.pressurePolynomial.poly_a;
            params["polyB"] = input->customCalibration.pressurePolynomial.poly_b;
            params["polyC"] = input->customCalibration.pressurePolynomial.poly_c;
            break;

        case CAL_VOLTAGE_DIVIDER:
            params["r1"] = input->customCalibration.voltageDivider.r1;
            params["r2"] = input->customCalibration.voltageDivider.r2;
            params["correction"] = input->customCalibration.voltageDivider.correction;
            params["offset"] = input->customCalibration.voltageDivider.offset;
            break;

        case CAL_RPM:
            params["poles"] = input->customCalibration.rpm.poles;
            params["pulleyRatio"] = input->customCalibration.rpm.pulley_ratio;
            params["calibrationMult"] = input->customCalibration.rpm.calibration_mult;
            params["timeoutMs"] = input->customCalibration.rpm.timeout_ms;
            params["minRPM"] = input->customCalibration.rpm.min_rpm;
            params["maxRPM"] = input->customCalibration.rpm.max_rpm;
            break;

        default:
            break;
    }
}

// Export single input to JSON
void exportInputToJSON(JsonObject& inputObj, const Input* input) {
    if (!input || !input->flags.isEnabled) {
        return;
    }

    // Basic info
    inputObj["pin"] = input->pin;
    inputObj["abbr"] = input->abbrName;
    inputObj["name"] = input->displayName;

    // Application, sensor, units (use registry names)
    inputObj["app"] = reinterpret_cast<const char*>(getApplicationNameByIndex(input->applicationIndex));
    inputObj["applicationIndex"] = input->applicationIndex;
    inputObj["sensor"] = reinterpret_cast<const char*>(getSensorNameByIndex(input->sensorIndex));
    inputObj["sensorIndex"] = input->sensorIndex;
    inputObj["units"] = reinterpret_cast<const char*>(getUnitStringByIndex(input->unitsIndex));

    // Alarm thresholds
    JsonObject alarm = inputObj["alarm"].to<JsonObject>();
    alarm["min"] = input->minValue;
    alarm["max"] = input->maxValue;

    // Flags
    inputObj["enabled"] = input->flags.isEnabled;
    inputObj["alarmEnabled"] = input->flags.alarm;
    inputObj["displayEnabled"] = input->flags.display;

    // OBD2 (if applicable)
    if (input->obd2pid != 0) {
        JsonObject obd2 = inputObj["obd2"].to<JsonObject>();
        obd2["pid"] = input->obd2pid;
        obd2["length"] = input->obd2length;
    }

    // Calibration
    if (input->flags.useCustomCalibration) {
        JsonObject cal = inputObj["calibration"].to<JsonObject>();
        exportCalibration(cal, input);
    }
}

// Export all inputs to JSON array
void exportInputsToJSON(JsonArray& inputsArray) {
    for (uint8_t i = 0; i < numActiveInputs; i++) {
        Input* input = &inputs[i];
        if (input->flags.isEnabled) {
            JsonObject inputObj = inputsArray.add<JsonObject>();
            inputObj["idx"] = i;
            exportInputToJSON(inputObj, input);
        }
    }
}

// Export pin registry to JSON array
void exportPinRegistryToJSON(JsonArray& pinsArray) {
    uint8_t count = getPinRegistrySize();
    for (uint8_t i = 0; i < count; i++) {
        const PinUsage* pinUsage = getPinUsageByIndex(i);
        if (pinUsage && pinUsage->type != PIN_UNUSED) {
            JsonObject pinObj = pinsArray.add<JsonObject>();
            pinObj["pin"] = pinUsage->pin;
            pinObj["type"] = getPinUsageTypeName(pinUsage->type);
            if (pinUsage->description) {
                pinObj["description"] = pinUsage->description;
            }
        }
    }
}

// Export system configuration to JSON
void exportSystemConfigToJSON(JsonObject& systemObj) {
    // Output modules
    JsonObject outputs = systemObj["outputs"].to<JsonObject>();

    const char* outputNames[] = {"can", "realdash", "serial", "sd", "alarm", "relay"};
    for (uint8_t i = 0; i < NUM_OUTPUTS; i++) {
        JsonObject output = outputs[outputNames[i]].to<JsonObject>();
        output["enabled"] = (bool)systemConfig.outputEnabled[i];
        output["interval"] = systemConfig.outputInterval[i];
    }

    // Display settings
    JsonObject display = systemObj["display"].to<JsonObject>();
    display["enabled"] = (bool)systemConfig.displayEnabled;

    const char* displayTypeStr = "NONE";
    switch (systemConfig.displayType) {
        case DISPLAY_LCD: displayTypeStr = "LCD"; break;
        case DISPLAY_OLED: displayTypeStr = "OLED"; break;
        default: displayTypeStr = "NONE"; break;
    }
    display["type"] = displayTypeStr;

    char addrBuf[5];
    snprintf(addrBuf, sizeof(addrBuf), "0x%02X", systemConfig.lcdI2CAddress);
    display["address"] = addrBuf;

    display["updateInterval"] = systemConfig.lcdUpdateInterval;

    JsonObject defaultUnits = display["defaultUnits"].to<JsonObject>();
    defaultUnits["temperature"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultTempUnits));
    defaultUnits["pressure"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultPressUnits));
    defaultUnits["elevation"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultElevUnits));
    defaultUnits["speed"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultSpeedUnits));

    // Timing intervals
    JsonObject timing = systemObj["timing"].to<JsonObject>();
    timing["sensorRead"] = systemConfig.sensorReadInterval;
    timing["alarmCheck"] = systemConfig.alarmCheckInterval;

    // Export all registered pins from pin registry
    // This includes system pins (button, buzzer, chip selects), bus pins, and any other registered pins
    JsonArray pins = systemObj["pins"].to<JsonArray>();
    exportPinRegistryToJSON(pins);

    // Physical constants
    JsonObject constants = systemObj["constants"].to<JsonObject>();
    constants["seaLevelPressure"] = systemConfig.seaLevelPressure;

    // Bus Configuration
    JsonObject buses = systemObj["buses"].to<JsonObject>();
    buses["i2c"] = systemConfig.buses.active_i2c;
    buses["i2cClock"] = systemConfig.buses.i2c_clock;
    buses["spi"] = systemConfig.buses.active_spi;
    buses["spiClock"] = systemConfig.buses.spi_clock;
    buses["can"] = systemConfig.buses.active_can;
    buses["canBaudrate"] = systemConfig.buses.can_baudrate;

    // Serial Port Configuration
    JsonObject serial = systemObj["serial"].to<JsonObject>();
    serial["enabledMask"] = systemConfig.serial.enabled_mask;
    JsonArray serialPorts = serial["ports"].to<JsonArray>();
    for (uint8_t i = 0; i < NUM_SERIAL_PORTS; i++) {
        JsonObject port = serialPorts.add<JsonObject>();
        uint8_t port_id = i + 1;
        port["port"] = port_id;
        port["enabled"] = (bool)(systemConfig.serial.enabled_mask & (1 << i));
        port["baudrate"] = getBaudRateFromIndex(systemConfig.serial.baudrate_index[i]);
    }

    // Log Filter Configuration
    JsonObject logFilter = systemObj["logFilter"].to<JsonObject>();
    const char* levelNames[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG"};
    logFilter["controlLevel"] = systemConfig.logFilter.control_level <= 4 ? levelNames[systemConfig.logFilter.control_level] : "UNKNOWN";
    logFilter["dataLevel"] = systemConfig.logFilter.data_level <= 4 ? levelNames[systemConfig.logFilter.data_level] : "UNKNOWN";
    logFilter["debugLevel"] = systemConfig.logFilter.debug_level <= 4 ? levelNames[systemConfig.logFilter.debug_level] : "UNKNOWN";

    char tagsBuf[11];  // "0xFFFFFFFF" + null terminator
    snprintf(tagsBuf, sizeof(tagsBuf), "0x%08lX", (unsigned long)systemConfig.logFilter.enabledTags);
    logFilter["enabledTags"] = tagsBuf;
}

// JSON Schema Version
// Increment when making backward-incompatible changes to JSON structure
// Version history:
//   1 - Initial release (v0.4.1-alpha)
#define JSON_SCHEMA_VERSION 1

// Main export function - dump entire config to JSON
void dumpConfigToJSON(Print& output) {
    // Allocate JSON document (ArduinoJson v7 handles sizing automatically)
    JsonDocument doc;

    // Schema version (for future migration support)
    doc["schemaVersion"] = JSON_SCHEMA_VERSION;
    doc["mode"] = "runtime";

    // Firmware info
    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = firmwareVersionString();
    firmware["major"] = FW_MAJOR;
    firmware["minor"] = FW_MINOR;
    firmware["patch"] = FW_PATCH;
    firmware["prerelease"] = FW_PRERELEASE;
    firmware["build"] = firmwareVersion();
    firmware["gitHash"] = FW_GIT_HASH;
    firmware["platform"] = getPlatformString();
    firmware["timestamp"] = getCurrentTimestamp();
    firmware["maxInputs"] = MAX_INPUTS;
    firmware["activeInputs"] = numActiveInputs;

    // System configuration
    JsonObject system = doc["system"].to<JsonObject>();
    exportSystemConfigToJSON(system);

    // Inputs
    JsonArray inputsArray = doc["inputs"].to<JsonArray>();
    exportInputsToJSON(inputsArray);

    // Serialize to output
    serializeJsonPretty(doc, output);
    output.println();
}

// Import calibration from JSON
static bool importCalibration(const Input* input, JsonObject& calObj, uint8_t index) {
    (void)input;  // Unused for now
    (void)index;  // Unused for now

    const char* source = calObj["source"];

    if (strcmp(source, "PRESET") == 0) {
        // Use preset calibration - clear custom flag
        // This will be handled by the input configuration
        return true;
    }

    if (!calObj["params"].isNull() == false) {
        return false;
    }

    // Use existing serial command infrastructure for applying calibrations
    // This will be implemented in the next phase
    // For now, just validate the structure
    return true;
}

// Import single input from JSON
bool importInputFromJSON(JsonObject& inputObj, uint8_t index) {
    if (index >= MAX_INPUTS) {
        return false;
    }

    // Extract values
    uint8_t pin = inputObj["pin"];

    // Support both "app" (runtime) and "application" (static/legacy) field names
    const char* appName = inputObj["app"];
    if (appName == nullptr) {
        appName = inputObj["application"];
    }

    const char* sensorName = inputObj["sensor"];
    const char* unitsName = inputObj["units"];

    msg.debug.debug(TAG_JSON, "Processing input %d (pin %d): app=%s, sensor=%s, units=%s",
                    index, pin,
                    appName ? appName : "NULL",
                    sensorName ? sensorName : "NULL",
                    unitsName ? unitsName : "NULL");

    // Validate required fields are present
    if (!appName || !sensorName || !unitsName) {
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.println(F(") - missing required fields"));

        if (!appName) {
            msg.debug.error(TAG_JSON, "Missing application field");
        }
        if (!sensorName) {
            msg.debug.error(TAG_JSON, "Missing sensor field");
        }
        if (!unitsName) {
            msg.debug.error(TAG_JSON, "Missing units field");
        }

        return false;
    }

    // Find indices in registries
    uint8_t appIdx = getApplicationIndexByName(appName);
    uint8_t sensorIdx = getSensorIndexByName(sensorName);
    uint8_t unitsIdx = getUnitsIndexByName(unitsName);

    msg.debug.debug(TAG_JSON, "Registry indices: app=%d, sensor=%d, units=%d", appIdx, sensorIdx, unitsIdx);

    // Validate registry lookups (index 0 = NONE for app/sensor, but CELSIUS for units)
    // Note: Units index 0 is CELSIUS (valid), so we can't use 0 to detect lookup failure
    // Instead, we rely on the NULL check above to ensure unitsName exists
    if (appIdx == 0 || sensorIdx == 0) {
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.println(F(")"));

        if (appIdx == 0) {
            msg.debug.error(TAG_JSON, "Invalid application: %s", appName);
        }
        if (sensorIdx == 0) {
            msg.debug.error(TAG_JSON, "Invalid sensor: %s", sensorName);
        }

        return false;  // Invalid registry entry (0 = NONE for app/sensor)
    }

    // Use input manager functions to properly configure the input
    // This ensures all function pointers and calibration are wired correctly
    extern bool setInputApplication(uint8_t pin, uint8_t appIndex);
    extern bool setInputSensor(uint8_t pin, uint8_t sensorIndex);
    extern bool setInputUnits(uint8_t pin, uint8_t unitsIndex);
    extern bool setInputName(uint8_t pin, const char* name);
    extern bool setInputDisplayName(uint8_t pin, const char* displayName);
    extern bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue);
    extern bool enableInput(uint8_t pin, bool enable);
    extern bool enableInputAlarm(uint8_t pin, bool enable);
    extern bool enableInputDisplay(uint8_t pin, bool enable);
    extern bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length);

    // Get pointer to input struct and set pin
    Input* input = &inputs[index];
    input->pin = pin;

    // Apply configuration using input manager functions
    setInputApplication(pin, appIdx);
    setInputSensor(pin, sensorIdx);
    setInputUnits(pin, unitsIdx);

    // Set names
    if (inputObj["abbr"].isNull() == false) {
        setInputName(pin, inputObj["abbr"]);
    }
    if (inputObj["name"].isNull() == false) {
        setInputDisplayName(pin, inputObj["name"]);
    }

    // Set alarm thresholds
    if (inputObj["alarm"].isNull() == false) {
        JsonObject alarm = inputObj["alarm"];
        setInputAlarmRange(pin, alarm["min"], alarm["max"]);
    }

    // Set flags (use || for default value, not | which is bitwise OR)
    enableInput(pin, inputObj["enabled"] | true);  // Default to true if missing
    enableInputAlarm(pin, inputObj["alarmEnabled"] | false);  // Default to false if missing
    enableInputDisplay(pin, inputObj["displayEnabled"] | true);  // Default to true if missing

    // OBD2
    if (inputObj["obd2"].isNull() == false) {
        JsonObject obd2 = inputObj["obd2"];
        setInputOBD(pin, obd2["pid"], obd2["length"]);
    }

    // Calibration (if custom)
    if (inputObj["calibration"].isNull() == false) {
        JsonObject cal = inputObj["calibration"];
        importCalibration(input, cal, index);
    }

    return true;
}

// Import all inputs from JSON
bool importInputsFromJSON(JsonArray& inputsArray) {
    uint8_t importedCount = 0;
    uint8_t totalInputs = inputsArray.size();

    msg.debug.info(TAG_JSON, "Processing %d inputs from JSON", totalInputs);

    for (JsonVariant v : inputsArray) {
        JsonObject inputObj = v.as<JsonObject>();
        uint8_t idx = inputObj["idx"];

        if (importInputFromJSON(inputObj, idx)) {
            importedCount++;
            msg.debug.debug(TAG_JSON, "Successfully imported input %d", idx);
        } else {
            msg.debug.warn(TAG_JSON, "Failed to import input %d", idx);
        }
    }

    msg.debug.info(TAG_JSON, "Import complete: %d of %d inputs imported", importedCount, totalInputs);

    numActiveInputs = importedCount;
    return importedCount > 0;
}

// Import system configuration from JSON
bool importSystemConfigFromJSON(JsonObject& systemObj) {
    // Output modules
    if (systemObj["outputs"].isNull() == false) {
        JsonObject outputs = systemObj["outputs"];
        const char* outputNames[] = {"can", "realdash", "serial", "sd", "alarm", "relay"};

        for (uint8_t i = 0; i < NUM_OUTPUTS; i++) {
            if (outputs[outputNames[i]].isNull() == false) {
                JsonObject output = outputs[outputNames[i]];
                systemConfig.outputEnabled[i] = output["enabled"];
                systemConfig.outputInterval[i] = output["interval"];
            }
        }
    }

    // Display settings
    if (systemObj["display"].isNull() == false) {
        JsonObject display = systemObj["display"];
        systemConfig.displayEnabled = display["enabled"];

        const char* displayType = display["type"];
        if (strcmp(displayType, "LCD") == 0) {
            systemConfig.displayType = DISPLAY_LCD;
        } else if (strcmp(displayType, "OLED") == 0) {
            systemConfig.displayType = DISPLAY_OLED;
        } else {
            systemConfig.displayType = DISPLAY_NONE;
        }

        // Parse I2C address (hex string like "0x27")
        if (display["address"].isNull() == false) {
            const char* addr = display["address"];
            if (addr[0] == '0' && (addr[1] == 'x' || addr[1] == 'X')) {
                systemConfig.lcdI2CAddress = strtol(addr + 2, nullptr, 16);
            } else {
                systemConfig.lcdI2CAddress = strtol(addr, nullptr, 16);
            }
        }

        // Display update interval
        if (display["updateInterval"].isNull() == false) {
            systemConfig.lcdUpdateInterval = display["updateInterval"];
        }

        // Default units
        if (display["defaultUnits"].isNull() == false) {
            JsonObject units = display["defaultUnits"];

            if (units["temperature"].isNull() == false) {
                uint8_t idx = getUnitsIndexByName(units["temperature"]);
                if (idx != 0) systemConfig.defaultTempUnits = idx;
            }
            if (units["pressure"].isNull() == false) {
                uint8_t idx = getUnitsIndexByName(units["pressure"]);
                if (idx != 0) systemConfig.defaultPressUnits = idx;
            }
            if (units["elevation"].isNull() == false) {
                uint8_t idx = getUnitsIndexByName(units["elevation"]);
                if (idx != 0) systemConfig.defaultElevUnits = idx;
            }
            if (units["speed"].isNull() == false) {
                uint8_t idx = getUnitsIndexByName(units["speed"]);
                if (idx != 0) systemConfig.defaultSpeedUnits = idx;
            }
        }
    }

    // Timing intervals
    if (systemObj["timing"].isNull() == false) {
        JsonObject timing = systemObj["timing"];
        systemConfig.sensorReadInterval = timing["sensorRead"];
        systemConfig.alarmCheckInterval = timing["alarmCheck"];

        // Backward compatibility: check for lcdUpdate in timing section (old location)
        if (timing["lcdUpdate"].isNull() == false) {
            systemConfig.lcdUpdateInterval = timing["lcdUpdate"];
        }
    }

    // Hardware pins - extract from pin registry array
    if (systemObj["pins"].isNull() == false && systemObj["pins"].is<JsonArray>()) {
        JsonArray pins = systemObj["pins"];
        for (JsonVariant v : pins) {
            JsonObject pin = v.as<JsonObject>();
            const char* desc = pin["description"];
            uint8_t pinNum = pin["pin"];

            if (desc != nullptr) {
                if (strcmp(desc, "Mode Button") == 0) {
                    systemConfig.modeButtonPin = pinNum;
                } else if (strcmp(desc, "Buzzer") == 0) {
                    systemConfig.buzzerPin = pinNum;
                } else if (strcmp(desc, "CAN CS") == 0) {
                    systemConfig.canCSPin = pinNum;
                } else if (strcmp(desc, "CAN INT") == 0) {
                    systemConfig.canIntPin = pinNum;
                } else if (strcmp(desc, "SD CS") == 0) {
                    systemConfig.sdCSPin = pinNum;
                } else if (strcmp(desc, "Test Mode Trigger") == 0) {
                    systemConfig.testModePin = pinNum;
                }
            }
        }
    }

    // Physical constants
    if (systemObj["constants"].isNull() == false) {
        JsonObject constants = systemObj["constants"];
        systemConfig.seaLevelPressure = constants["seaLevelPressure"];
    }

    // Bus Configuration (with backward-compatible defaults)
    if (systemObj["buses"].isNull() == false) {
        JsonObject buses = systemObj["buses"];
        systemConfig.buses.active_i2c = buses["i2c"] | DEFAULT_I2C_BUS;
        systemConfig.buses.i2c_clock = buses["i2cClock"] | DEFAULT_I2C_CLOCK;
        systemConfig.buses.active_spi = buses["spi"] | DEFAULT_SPI_BUS;
        systemConfig.buses.spi_clock = buses["spiClock"] | DEFAULT_SPI_CLOCK;
        systemConfig.buses.active_can = buses["can"] | DEFAULT_CAN_BUS;
        systemConfig.buses.can_baudrate = buses["canBaudrate"] | DEFAULT_CAN_BAUDRATE;
    } else {
        // No buses object - use defaults (backward compatibility with old configs)
        systemConfig.buses.active_i2c = DEFAULT_I2C_BUS;
        systemConfig.buses.i2c_clock = DEFAULT_I2C_CLOCK;
        systemConfig.buses.active_spi = DEFAULT_SPI_BUS;
        systemConfig.buses.spi_clock = DEFAULT_SPI_CLOCK;
        systemConfig.buses.active_can = DEFAULT_CAN_BUS;
        systemConfig.buses.can_baudrate = DEFAULT_CAN_BAUDRATE;
    }

    return true;
}

// Load configuration from JSON string
bool loadConfigFromJSON(const char* jsonString) {
    // Allocate JSON document (same size as export)
    JsonDocument doc;

    // Parse JSON
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error) {
        msg.control.print(F("ERROR: JSON parse failed: "));
        msg.control.println(error.c_str());
        return false;
    }

    // Check schema version for migration support
    uint8_t schemaVer = doc["schemaVersion"] | 1;  // Default to v1 if missing (old configs)

    if (schemaVer != JSON_SCHEMA_VERSION) {
        msg.control.print(F("ERROR: Only schemaVersion 1 is supported. Got: "));
        msg.control.println(schemaVer);
        return false;
    }

    // Validate mode field
    const char* mode = doc["mode"] | "runtime";
    if (strcmp(mode, "runtime") != 0) {
        msg.control.print(F("ERROR: Only mode='runtime' configs can be imported. Got: "));
        msg.control.println(mode);
        return false;
    }

    // Import system config
    if (doc["system"].isNull() == false) {
        JsonObject system = doc["system"];
        if (!importSystemConfigFromJSON(system)) {
            msg.control.println(F("ERROR: Failed to import system config"));
            return false;
        }
    }

    // Import inputs
    if (doc["inputs"].isNull() == false) {
        JsonArray inputsArray = doc["inputs"];
        if (!importInputsFromJSON(inputsArray)) {
            msg.control.println(F("ERROR: Failed to import inputs"));
            return false;
        }
    }

    msg.control.print(F("Successfully loaded config (schema v"));
    msg.control.print(schemaVer);
    msg.control.println(F(")"));

    return true;
}

// Save configuration to SD card
bool saveConfigToSD(const char* filename) {
    msg.debug.info(TAG_SD, "Starting save operation");

    // Check if SD card is initialized (done in main setup)
    if (!isSDInitialized()) {
        msg.control.println(F("ERROR: SD card not initialized"));
        msg.debug.warn(TAG_SD, "SD card not available");
        return false;
    }

    msg.debug.debug(TAG_SD, "SD card is ready");

    // Create config directory if it doesn't exist
    msg.debug.debug(TAG_SD, "Checking for config directory");
    if (!SD.exists("config")) {
        msg.debug.debug(TAG_SD, "Creating config directory");
        if (!SD.mkdir("config")) {
            msg.debug.error(TAG_SD, "Failed to create config directory");
        } else {
            msg.debug.debug(TAG_SD, "config directory created");
        }
    } else {
        msg.debug.debug(TAG_SD, "config directory exists");
    }

    // Generate filename if not provided
    char filepath[32];
    if (filename == nullptr) {
        snprintf(filepath, sizeof(filepath), "/config/backup_%lu.json", getCurrentTimestamp());
    } else {
        // Ensure filename is in config directory
        if (filename[0] == '/') {
            snprintf(filepath, sizeof(filepath), "/config%s", filename);
        } else {
            snprintf(filepath, sizeof(filepath), "/config/%s", filename);
        }
    }
    filepath[sizeof(filepath) - 1] = '\0';

    msg.debug.info(TAG_SD, "Opening file: %s", filepath);

    // If file exists, remove it first (FILE_WRITE appends, we want to replace)
    if (SD.exists(filepath)) {
        msg.debug.debug(TAG_SD, "File exists, removing");
        if (!SD.remove(filepath)) {
            msg.debug.warn(TAG_SD, "Failed to remove existing file");
        }
    }

    // Open file for writing
    File configFile = SD.open(filepath, FILE_WRITE);
    if (!configFile) {
        msg.debug.error(TAG_SD, "Failed to open file for writing");
        msg.control.print(F("ERROR: Failed to open file: "));
        msg.control.println(filepath);
        // Re-enable watchdog before returning
        watchdogEnable(2000);
        msg.debug.debug(TAG_SD, "Watchdog re-enabled");
        return false;
    }

    msg.debug.debug(TAG_SD, "File opened successfully");
    msg.debug.debug(TAG_SD, "Writing JSON...");

    // Write JSON to file
    dumpConfigToJSON(configFile);

    msg.debug.debug(TAG_SD, "JSON write complete");
    msg.debug.debug(TAG_SD, "Closing file...");
    configFile.close();
    msg.debug.debug(TAG_SD, "File closed");

    // Re-enable watchdog after SD operations complete
    watchdogEnable(2000);
    msg.debug.debug(TAG_SD, "Watchdog re-enabled");

    msg.control.print(F("Configuration saved to: "));
    msg.control.println(filepath);
    msg.debug.info(TAG_SD, "Save operation completed successfully");

    return true;
}

// Load configuration from SD card
bool loadConfigFromSD(const char* filename) {
    msg.debug.info(TAG_SD, "Starting load operation");

    // Check if SD card is initialized (done in main setup)
    if (!isSDInitialized()) {
        msg.control.println(F("ERROR: SD card not initialized"));
        msg.debug.warn(TAG_SD, "SD card not available");
        return false;
    }

    msg.debug.debug(TAG_SD, "SD card is ready");

    // Generate filename with same logic as save
    char filepath[32];
    if (filename == nullptr) {
        msg.control.println(F("ERROR: No filename provided"));
        msg.debug.error(TAG_SD, "No filename provided");
        return false;
    } else {
        // Ensure filename is in config directory
        if (filename[0] == '/') {
            snprintf(filepath, sizeof(filepath), "/config%s", filename);
        } else {
            snprintf(filepath, sizeof(filepath), "/config/%s", filename);
        }
    }
    filepath[sizeof(filepath) - 1] = '\0';

    msg.debug.info(TAG_SD, "Opening file: %s", filepath);

    // Open file for reading
    File configFile = SD.open(filepath, FILE_READ);
    if (!configFile) {
        msg.control.print(F("ERROR: Failed to open file: "));
        msg.control.println(filepath);
        msg.debug.error(TAG_SD, "File open FAILED");
        // Re-enable watchdog before returning
        watchdogEnable(2000);
        msg.debug.debug(TAG_SD, "Watchdog re-enabled");
        return false;
    }

    msg.debug.debug(TAG_SD, "File opened successfully");
    msg.debug.debug(TAG_SD, "Reading file...");

    // Read entire file into string
    String jsonString;
    size_t bytesRead = 0;
    while (configFile.available()) {
        jsonString += (char)configFile.read();
        bytesRead++;
    }

    msg.debug.debug(TAG_SD, "Read complete: %u bytes total", bytesRead);
    msg.debug.debug(TAG_SD, "Closing file...");
    configFile.close();
    msg.debug.debug(TAG_SD, "File closed");

    msg.debug.debug(TAG_SD, "Parsing JSON...");

    // Load configuration
    bool success = loadConfigFromJSON(jsonString.c_str());

    msg.debug.debug(TAG_SD, "JSON parsing complete");

    // Re-enable watchdog after SD operations complete
    watchdogEnable(2000);
    msg.debug.debug(TAG_SD, "Watchdog re-enabled");

    if (success) {
        msg.control.print(F("Configuration loaded from: "));
        msg.control.println(filepath);
        msg.debug.info(TAG_SD, "Load operation completed successfully");
    } else {
        msg.debug.error(TAG_SD, "Load operation FAILED");
    }

    return success;
}

/**
 * Save configuration to file with destination routing
 * @param destination Destination string ("SD", "USB", etc.)
 * @param filename Filename (without destination prefix)
 * @return true if successful
 */
bool saveConfigToFile(const char* destination, const char* filename) {
    // Route based on destination
    if (strcmp(destination, "SD") == 0) {
        return saveConfigToSD(filename);
    }

#ifdef ENABLE_USB_STORAGE
    else if (strcmp(destination, "USB") == 0) {
        // Future: USB storage implementation
        msg.control.println(F("ERROR: USB storage not yet implemented"));
        return false;
    }
#endif

#ifdef ENABLE_HTTP_STORAGE
    else if (strcmp(destination, "HTTP") == 0 || strcmp(destination, "HTTPS") == 0) {
        // Future: HTTP/HTTPS upload implementation
        msg.control.println(F("ERROR: HTTP storage not yet implemented"));
        return false;
    }
#endif

    else {
        msg.control.print(F("ERROR: Unknown destination '"));
        msg.control.print(destination);
        msg.control.println(F("'"));
        msg.control.println(F("  Supported destinations: SD"));
#ifdef ENABLE_USB_STORAGE
        msg.control.println(F("  Conditional: USB (if ENABLE_USB_STORAGE defined)"));
#endif
        return false;
    }
}

/**
 * Load configuration from file with destination routing
 * @param destination Destination string ("SD", "USB", etc.)
 * @param filename Filename (without destination prefix)
 * @return true if successful
 */
bool loadConfigFromFile(const char* destination, const char* filename) {
    // Route based on destination
    if (strcmp(destination, "SD") == 0) {
        return loadConfigFromSD(filename);
    }

#ifdef ENABLE_USB_STORAGE
    else if (strcmp(destination, "USB") == 0) {
        // Future: USB storage implementation
        msg.control.println(F("ERROR: USB storage not yet implemented"));
        return false;
    }
#endif

#ifdef ENABLE_HTTP_STORAGE
    else if (strcmp(destination, "HTTP") == 0 || strcmp(destination, "HTTPS") == 0) {
        // Future: HTTP download implementation
        msg.control.println(F("ERROR: HTTP storage not yet implemented"));
        return false;
    }
#endif

    else {
        msg.control.print(F("ERROR: Unknown destination '"));
        msg.control.print(destination);
        msg.control.println(F("'"));
        msg.control.println(F("  Supported destinations: SD"));
#ifdef ENABLE_USB_STORAGE
        msg.control.println(F("  Conditional: USB (if ENABLE_USB_STORAGE defined)"));
#endif
        return false;
    }
}

#endif // USE_STATIC_CONFIG
