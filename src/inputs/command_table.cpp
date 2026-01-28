/*
 * command_table.cpp - Table-driven command dispatch implementation
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#include "../config.h"

#ifndef USE_STATIC_CONFIG

#include "command_table.h"
#include "command_helpers.h"
#include "input_manager.h"
#include "../config.h"
#include "../version.h"
#include "../lib/system_mode.h"
#include "../lib/system_config.h"
#include "../lib/json_config.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include "../lib/log_filter.h"
#include "../lib/log_tags.h"
#include "../lib/units_registry.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/platform.h"
#include "../lib/bus_manager.h"
#include "../lib/bus_defaults.h"
#include "../lib/serial_manager.h"
#include "../lib/pin_registry.h"
#include "../outputs/output_base.h"
#include "../lib/display_manager.h"
#ifdef ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif
#ifdef ENABLE_TEST_MODE
#include "../test/test_mode.h"
#endif
#include <string.h>
#include <ctype.h>

// AVR watchdog for system reset
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    #include <avr/wdt.h>
#endif

// Forward declarations of command handlers
static int cmd_help(int argc, const char* const* argv);
static int cmd_list(int argc, const char* const* argv);
static int cmd_set(int argc, const char* const* argv);
static int cmd_enable(int argc, const char* const* argv);
static int cmd_disable(int argc, const char* const* argv);
static int cmd_clear(int argc, const char* const* argv);
static int cmd_info(int argc, const char* const* argv);
static int cmd_output(int argc, const char* const* argv);
static int cmd_display(int argc, const char* const* argv);
static int cmd_transport(int argc, const char* const* argv);
static int cmd_system(int argc, const char* const* argv);
static int cmd_save(int argc, const char* const* argv);
static int cmd_load(int argc, const char* const* argv);
static int cmd_config(int argc, const char* const* argv);
static int cmd_run(int argc, const char* const* argv);
static int cmd_version(int argc, const char* const* argv);
static int cmd_reboot(int argc, const char* const* argv);
static int cmd_bus(int argc, const char* const* argv);
static int cmd_log(int argc, const char* const* argv);
#ifdef ENABLE_RELAY_OUTPUT
static int cmd_relay(int argc, const char* const* argv);
#endif
#ifdef ENABLE_TEST_MODE
static int cmd_test(int argc, const char* const* argv);
#endif

// Platform-specific reboot helper (shared by REBOOT and SYSTEM REBOOT/RESET)
static void platformReboot() {
    delay(100);
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
        defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        wdt_enable(WDTO_15MS);
        while(1) {}
    #elif defined(__IMXRT1062__)  // Teensy 4.x
        SCB_AIRCR = 0x05FA0004;
        while(1) {}
    #elif defined(ESP32)
        ESP.restart();
    #else
        msg.control.println(F("ERROR: Reboot not supported on this platform"));
    #endif
}

// Command table
const Command COMMANDS[] = {
    // Mode commands (always available)
    {"CONFIG", cmd_config, "Enter configuration mode", false},
    {"RUN", cmd_run, "Enter run mode", false},

    // Query commands (read-only, available in both modes)
    {"HELP", cmd_help, "Show help", false},
    {"?", cmd_help, "Show help (alias)", false},
    {"LIST", cmd_list, "List inputs/applications/sensors", false},
    {"INFO", cmd_info, "Show input details", false},
    {"VERSION", cmd_version, "Show firmware version", false},

    // Configuration commands (CONFIG mode only)
    {"SET", cmd_set, "Configure input", true},
    {"ENABLE", cmd_enable, "Enable input", true},
    {"DISABLE", cmd_disable, "Disable input", true},
    {"CLEAR", cmd_clear, "Clear input", true},
    {"OUTPUT", cmd_output, "Configure outputs", true},
    {"DISPLAY", cmd_display, "Configure display", true},
    {"TRANSPORT", cmd_transport, "Configure message routing", true},
    {"SYSTEM", cmd_system, "System configuration", true},
    {"SAVE", cmd_save, "Save configuration", true},
    {"LOAD", cmd_load, "Load configuration", true},
    {"REBOOT", cmd_reboot, "", true},  // Undocumented alias for SYSTEM REBOOT
    {"BUS", cmd_bus, "Configure I2C/SPI/CAN buses", true},
    {"LOG", cmd_log, "Configure log levels and tags", false},

#ifdef ENABLE_RELAY_OUTPUT
    {"RELAY", cmd_relay, "Configure relay outputs", true},
#endif
#ifdef ENABLE_TEST_MODE
    {"TEST", cmd_test, "Test mode control", false},
#endif
};

const uint8_t NUM_COMMANDS = sizeof(COMMANDS) / sizeof(Command);

// Check if command is read-only (allowed in RUN mode)
bool isReadOnlyCommand(const char* cmdName) {
    return streq(cmdName, "HELP") ||
           streq(cmdName, "?") ||
           streq(cmdName, "VERSION") ||
           streq(cmdName, "INFO") ||
           streq(cmdName, "LIST") ||
           streq(cmdName, "CONFIG") ||
           streq(cmdName, "RUN") ||
           streq(cmdName, "SYSTEM") ||  // Allow SYSTEM STATUS and SYSTEM DUMP in RUN mode
           streq(cmdName, "LOG") ||     // Allow LOG STATUS, LOG TAGS in RUN mode (LEVEL/TAG require CONFIG)
#ifdef ENABLE_TEST_MODE
           streq(cmdName, "TEST") ||
#endif
           false;
}

// Main command dispatcher
int dispatchCommand(int argc, const char* const* argv) {
    if (argc == 0) return 0;

    // Convert command name to uppercase for comparison
    char cmdUpper[32];
    strncpy(cmdUpper, argv[0], sizeof(cmdUpper) - 1);
    cmdUpper[sizeof(cmdUpper) - 1] = '\0';
    for (char* p = cmdUpper; *p; p++) *p = toupper(*p);

    // Mode gating check (except for mode-switching commands)
    if (isInRunMode() && !isReadOnlyCommand(cmdUpper)) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  ERROR: Configuration locked in RUN mode"));
        msg.control.println(F("  Type CONFIG to enter configuration mode"));
        msg.control.println(F("========================================"));
        msg.control.println();
        return 1;
    }

    // Look up command in table and dispatch
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        if (streq(cmdUpper, COMMANDS[i].name)) {
            return COMMANDS[i].handler(argc, argv);
        }
    }

    // Command not found
    msg.control.print(F("ERROR: Unknown command '"));
    msg.control.print(argv[0]);
    msg.control.println(F("'"));
    msg.control.println(F("  Type HELP for available commands"));
    return 1;
}

//=============================================================================
// Command Handler Implementations
//=============================================================================

static int cmd_help(int argc, const char* const* argv) {
    if (argc == 1) {
        // No arguments - show overview
        printHelpOverview();
    } else if (argc == 2) {
        // Category argument provided
        if (streq(argv[1], "QUICK")) {
            printHelpQuick();
        } else {
            printHelpCategory(argv[1]);
        }
    } else {
        msg.control.println(F("ERROR: HELP takes 0 or 1 argument"));
        msg.control.println(F("  Usage: HELP [category]"));
    }
    return 0;
}

static int cmd_list(int argc, const char* const* argv) {
    if (argc == 1) {
        msg.control.println(F("ERROR: LIST requires a subcommand"));
        msg.control.println(F("  Usage: LIST INPUTS | APPLICATIONS | SENSORS | OUTPUTS | TRANSPORTS"));
        return 1;
    }

    if (streq(argv[1], "INPUTS")) {
        listAllInputs();
    } else if (streq(argv[1], "APPLICATIONS")) {
        listApplicationPresets();
    } else if (streq(argv[1], "SENSORS")) {
        // LIST SENSORS [category|filter]
        const char* filter = (argc >= 3) ? argv[2] : NULL;
        listSensors(filter);
    } else if (streq(argv[1], "OUTPUTS")) {
        listOutputModules();
    } else if (streq(argv[1], "TRANSPORTS")) {
        router.listAvailableTransports();
    } else {
        msg.control.print(F("ERROR: Unknown LIST subcommand '"));
        msg.control.print(argv[1]);
        msg.control.println(F("'"));
        msg.control.println(F("  Valid: INPUTS, APPLICATIONS, SENSORS, OUTPUTS, TRANSPORTS"));
        return 1;
    }
    return 0;
}

static int cmd_version(int argc, const char* const* argv) {
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.print(F("  Firmware: "));
    msg.control.println(firmwareVersionString());
    msg.control.print(F("  Build: "));
    msg.control.println(firmwareVersion());
    msg.control.print(F("  Git: "));
    msg.control.println(FW_GIT_HASH);
    msg.control.print(F("  EEPROM Version: "));
    msg.control.println(EEPROM_VERSION);
    msg.control.print(F("  Active Inputs: "));
    extern uint8_t numActiveInputs;
    msg.control.print(numActiveInputs);
    msg.control.print(F("/"));
    msg.control.println(MAX_INPUTS);
    msg.control.println(F("========================================"));
    msg.control.println();
    return 0;
}

static int cmd_config(int argc, const char* const* argv) {
    // CONFIG command now only enters configuration mode
    // CONFIG SAVE/LOAD have been removed - use SAVE FILE / LOAD FILE instead
    setMode(MODE_CONFIG);
    return 0;
}

static int cmd_run(int argc, const char* const* argv) {
    setMode(MODE_RUN);
    return 0;
}

static int cmd_save(int argc, const char* const* argv) {
    // Case 1: SAVE (bare) → EEPROM (backward compatible)
    if (argc == 1) {
        msg.control.println(F("Saving configuration to EEPROM..."));
        saveInputConfig();
        saveSystemConfig();
        msg.control.println(F("Configuration saved"));
        return 0;
    }

    // Case 2: SAVE EEPROM (explicit)
    if (argc == 2 && streq(argv[1], "EEPROM")) {
        msg.control.println(F("Saving configuration to EEPROM..."));
        saveInputConfig();
        saveSystemConfig();
        msg.control.println(F("Configuration saved"));
        return 0;
    }

    // Case 3: SAVE [destination:]filename (file path - anything that's not "EEPROM")
    if (argc >= 2) {
        // Parse file path
        FilePathComponents path = parseFilePath(argv[1]);
        if (!path.isValid) {
            msg.control.println(F("ERROR: Invalid file path"));
            return 1;
        }

        msg.control.println();
        msg.control.print(F("Saving configuration to "));
        msg.control.print(path.destination);
        msg.control.print(F(":"));
        msg.control.print(path.filename);
        msg.control.println(F("..."));

        if (saveConfigToFile(path.destination, path.filename)) {
            msg.control.println(F("Configuration saved successfully"));
        } else {
            msg.control.println(F("ERROR: Failed to save configuration"));
            return 1;
        }
        msg.control.println();
        return 0;
    }

    // Should never reach here, but provide helpful error message
    msg.control.println(F("ERROR: Invalid SAVE syntax"));
    msg.control.println(F("  Usage: SAVE [EEPROM | [destination:]filename]"));
    msg.control.println(F("  Examples:"));
    msg.control.println(F("    SAVE                    # Save to EEPROM"));
    msg.control.println(F("    SAVE EEPROM             # Save to EEPROM (explicit)"));
    msg.control.println(F("    SAVE config.json        # Save to SD card"));
    msg.control.println(F("    SAVE SD:mycar.json      # Save to SD card (explicit)"));
    return 1;
}

static int cmd_load(int argc, const char* const* argv) {
    // Case 1: LOAD (bare) → EEPROM (backward compatible)
    if (argc == 1) {
        msg.control.println(F("Loading configuration from EEPROM..."));
        loadInputConfig();
        loadSystemConfig();
        msg.control.println(F("Configuration loaded"));
        return 0;
    }

    // Case 2: LOAD EEPROM (explicit)
    if (argc == 2 && streq(argv[1], "EEPROM")) {
        msg.control.println(F("Loading configuration from EEPROM..."));
        loadInputConfig();
        loadSystemConfig();
        msg.control.println(F("Configuration loaded"));
        return 0;
    }

    // Case 3: LOAD [destination:]filename (file path - anything that's not "EEPROM")
    if (argc >= 2) {
        // Parse file path
        FilePathComponents path = parseFilePath(argv[1]);
        if (!path.isValid) {
            msg.control.println(F("ERROR: Invalid file path"));
            return 1;
        }

        msg.control.println();
        msg.control.print(F("Loading configuration from "));
        msg.control.print(path.destination);
        msg.control.print(F(":"));
        msg.control.print(path.filename);
        msg.control.println(F("..."));

        if (loadConfigFromFile(path.destination, path.filename)) {
            msg.control.println(F("Configuration loaded successfully"));
            msg.control.println(F("Type SAVE to persist to EEPROM"));
        } else {
            msg.control.println(F("ERROR: Failed to load configuration"));
            return 1;
        }
        msg.control.println();
        return 0;
    }

    // Should never reach here, but provide helpful error message
    msg.control.println(F("ERROR: Invalid LOAD syntax"));
    msg.control.println(F("  Usage: LOAD [EEPROM | [destination:]filename]"));
    msg.control.println(F("  Examples:"));
    msg.control.println(F("    LOAD                    # Load from EEPROM"));
    msg.control.println(F("    LOAD EEPROM             # Load from EEPROM (explicit)"));
    msg.control.println(F("    LOAD config.json        # Load from SD card"));
    msg.control.println(F("    LOAD SD:backup.json     # Load from SD card (explicit)"));
    return 1;
}

static int cmd_reboot(int argc, const char* const* argv) {
    msg.control.println(F("Rebooting system..."));
    platformReboot();
    return 0;
}

// Stub implementations for remaining commands
// These will be filled in next

static int cmd_set(int argc, const char* const* argv) {
    // SET <pin> <field> <value>
    // Also supports combined syntax: SET <pin> <application> <sensor>

    if (argc < 3) {
        msg.control.println(F("ERROR: Invalid SET syntax"));
        msg.control.println(F("  Usage: SET <pin> <field> <value>"));
        msg.control.println(F("  Example: SET A0 APPLICATION CHT"));
        msg.control.println(F("  Or combined: SET 7 EGT MAX31855"));
        return 1;
    }

    // Parse pin
    bool pinValid = false;
    uint8_t pin = parsePin(argv[1], &pinValid);
    if (!pinValid) return 1;

    const char* field = argv[2];

    // Try combined syntax: SET <pin> <application> <sensor>
    // Example: SET 6 CHT MAX6675
    if (argc == 4) {
        uint8_t appIndex = getApplicationIndexByName(field);
        uint8_t sensorIndex = getSensorIndexByName(argv[3]);

        // If both lookups succeed, treat as combined command
        if (appIndex != 0 && sensorIndex != 0) {
            // Check sensor/application measurement type compatibility
            const SensorInfo* sensorInfo = getSensorByIndex(sensorIndex);
            const ApplicationPreset* appPreset = getApplicationByIndex(appIndex);

            if (sensorInfo && appPreset) {
                MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensorInfo->measurementType);
                MeasurementType appMeasType = (MeasurementType)pgm_read_byte(&appPreset->expectedMeasurementType);

                if (sensorMeasType != appMeasType) {
                    msg.control.print(F("ERROR: Sensor/application type mismatch - "));
                    msg.control.print(argv[3]);
                    msg.control.print(F(" measures "));

                    // Print measurement type names
                    switch(sensorMeasType) {
                        case MEASURE_TEMPERATURE: msg.control.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: msg.control.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: msg.control.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: msg.control.print(F("RPM")); break;
                        case MEASURE_SPEED: msg.control.print(F("SPEED")); break;
                        case MEASURE_HUMIDITY: msg.control.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: msg.control.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: msg.control.print(F("DIGITAL")); break;
                    }
                    msg.control.print(F(" but "));
                    msg.control.print(field);
                    msg.control.print(F(" expects "));
                    switch(appMeasType) {
                        case MEASURE_TEMPERATURE: msg.control.print(F("TEMPERATURE")); break;
                        case MEASURE_PRESSURE: msg.control.print(F("PRESSURE")); break;
                        case MEASURE_VOLTAGE: msg.control.print(F("VOLTAGE")); break;
                        case MEASURE_RPM: msg.control.print(F("RPM")); break;
                        case MEASURE_SPEED: msg.control.print(F("SPEED")); break;
                        case MEASURE_HUMIDITY: msg.control.print(F("HUMIDITY")); break;
                        case MEASURE_ELEVATION: msg.control.print(F("ELEVATION")); break;
                        case MEASURE_DIGITAL: msg.control.print(F("DIGITAL")); break;
                    }
                    msg.control.println();
                    return 1;
                }

                // First set application (which also calls setInputSensor with preset sensor)
                if (setInputApplication(pin, appIndex)) {
                    // Then override sensor if different from preset
                    Input* input = getInputByPin(pin);
                    if (input && input->sensorIndex != sensorIndex) {
                        setInputSensor(pin, sensorIndex);
                    }

                    msg.control.print(F("Input "));
                    msg.control.print(argv[1]);
                    msg.control.print(F(" configured as "));
                    msg.control.print(field);
                    msg.control.print(F(" with "));
                    msg.control.println(argv[3]);
                    return 0;
                }
                return 1;
            }
        }
    }

    // SET <pin> APPLICATION <application>
    if (streq(field, "APPLICATION")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: APPLICATION requires an application name"));
            msg.control.println(F("  Hint: Use 'LIST APPLICATIONS' to see valid options"));
            return 1;
        }
        uint8_t appIndex = getApplicationIndexByName(argv[3]);
        if (appIndex == 0) {
            msg.control.print(F("ERROR: Unknown application '"));
            msg.control.print(argv[3]);
            msg.control.println(F("'"));
            msg.control.println(F("  Hint: Use 'LIST APPLICATIONS' to see valid options"));
            return 1;
        }
        if (setInputApplication(pin, appIndex)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" configured as "));
            msg.control.println(argv[3]);
            return 0;
        }
        return 1;
    }

    // SET <pin> SENSOR <category> <preset>  (two-layer syntax)
    // SET <pin> SENSOR <sensor>             (legacy flat syntax)
    if (streq(field, "SENSOR")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: SENSOR requires arguments"));
            msg.control.println(F("  Usage: SET <pin> SENSOR <category> <preset>"));
            msg.control.println(F("     or: SET <pin> SENSOR <sensor_name>"));
            msg.control.println(F("  Hint: Use 'LIST SENSORS' to see categories"));
            return 1;
        }

        uint8_t sensorIndex = 0;

        // Try two-layer syntax: SET <pin> SENSOR <category> <preset>
        if (argc >= 5) {
            SensorCategory cat = getCategoryByName(argv[3]);
            if (cat < CAT_COUNT) {
                sensorIndex = getSensorIndexByCategoryAndName(cat, argv[4]);
                if (sensorIndex == 0) {
                    msg.control.print(F("ERROR: Unknown sensor '"));
                    msg.control.print(argv[4]);
                    msg.control.print(F("' in category '"));
                    msg.control.print(argv[3]);
                    msg.control.println(F("'"));
                    msg.control.print(F("  Hint: Use 'LIST SENSORS "));
                    msg.control.print(argv[3]);
                    msg.control.println(F("' to see valid options"));
                    return 1;
                }
            }
        }

        // Fall back to legacy flat syntax: SET <pin> SENSOR <sensor>
        if (sensorIndex == 0) {
            sensorIndex = getSensorIndexByName(argv[3]);
        }

        if (sensorIndex == 0) {
            // Check if argv[3] is a category name (user forgot preset)
            SensorCategory cat = getCategoryByName(argv[3]);
            if (cat < CAT_COUNT) {
                msg.control.print(F("ERROR: Missing preset. Usage: SET "));
                msg.control.print(argv[1]);
                msg.control.print(F(" SENSOR "));
                msg.control.print(argv[3]);
                msg.control.println(F(" <preset>"));
                msg.control.print(F("  Hint: Use 'LIST SENSORS "));
                msg.control.print(argv[3]);
                msg.control.println(F("' to see available presets"));
            } else {
                msg.control.print(F("ERROR: Unknown sensor or category '"));
                msg.control.print(argv[3]);
                msg.control.println(F("'"));
                msg.control.println(F("  Hint: Use 'LIST SENSORS' to see categories"));
            }
            return 1;
        }

        if (setInputSensor(pin, sensorIndex)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" sensor set to "));
            // Print the actual sensor name from library
            const char* sensorName = getSensorNameByIndex(sensorIndex);
            if (sensorName) {
                msg.control.println((__FlashStringHelper*)sensorName);
            } else {
                msg.control.println(argv[argc >= 5 ? 4 : 3]);
            }
            return 0;
        }
        return 1;
    }

    // SET <pin> NAME <name>
    if (streq(field, "NAME")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: NAME requires a name string"));
            return 1;
        }
        if (setInputName(pin, argv[3])) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" name set to "));
            msg.control.println(argv[3]);
            return 0;
        }
        return 1;
    }

    // SET <pin> DISPLAY_NAME <name>
    if (streq(field, "DISPLAY_NAME")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: DISPLAY_NAME requires a name string"));
            return 1;
        }
        if (setInputDisplayName(pin, argv[3])) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" display name set to "));
            msg.control.println(argv[3]);
            return 0;
        }
        return 1;
    }

    // SET <pin> UNITS <units>
    if (streq(field, "UNITS")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: UNITS requires a unit name"));
            return 1;
        }
        uint8_t unitsIndex = getUnitsIndexByName(argv[3]);
        if (setInputUnits(pin, unitsIndex)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" units set to "));
            msg.control.println(argv[3]);
            return 0;
        }
        return 1;
    }

    // SET <pin> ALARM subcommands
    if (streq(field, "ALARM")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: ALARM requires ENABLE, DISABLE, WARMUP, PERSIST, or <min> <max>"));
            return 1;
        }

        // SET <pin> ALARM ENABLE
        if (streq(argv[3], "ENABLE")) {
            if (enableInputAlarm(pin, true)) {
                msg.control.print(F("Input "));
                msg.control.print(argv[1]);
                msg.control.println(F(" alarm enabled"));
                return 0;
            }
            return 1;
        }

        // SET <pin> ALARM DISABLE
        if (streq(argv[3], "DISABLE")) {
            if (enableInputAlarm(pin, false)) {
                msg.control.print(F("Input "));
                msg.control.print(argv[1]);
                msg.control.println(F(" alarm disabled"));
                return 0;
            }
            return 1;
        }

        // SET <pin> ALARM WARMUP <ms>
        if (streq(argv[3], "WARMUP")) {
            if (argc < 5) {
                msg.control.println(F("ERROR: ALARM WARMUP requires a time value in milliseconds"));
                return 1;
            }
            uint16_t value = atoi(argv[4]);
            if (value > 300000) {  // Max 5 minutes
                msg.control.println(F("ERROR: Alarm warmup time must be 0-300000ms"));
                return 1;
            }
            if (setInputAlarmWarmup(pin, value)) {
                msg.control.print(F("Input "));
                msg.control.print(argv[1]);
                msg.control.print(F(" alarm warmup set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
                return 0;
            }
            return 1;
        }

        // SET <pin> ALARM PERSIST <ms>
        if (streq(argv[3], "PERSIST")) {
            if (argc < 5) {
                msg.control.println(F("ERROR: ALARM PERSIST requires a time value in milliseconds"));
                return 1;
            }
            uint16_t value = atoi(argv[4]);
            if (value > 60000) {  // Max 60 seconds
                msg.control.println(F("ERROR: Alarm persistence time must be 0-60000ms"));
                return 1;
            }
            if (setInputAlarmPersist(pin, value)) {
                msg.control.print(F("Input "));
                msg.control.print(argv[1]);
                msg.control.print(F(" alarm persistence set to "));
                msg.control.print(value);
                msg.control.println(F("ms"));
                return 0;
            }
            return 1;
        }

        // SET <pin> ALARM <min> <max>
        if (argc < 5) {
            msg.control.println(F("ERROR: ALARM requires min and max values"));
            return 1;
        }
        float minVal = atof(argv[3]);
        float maxVal = atof(argv[4]);
        if (setInputAlarmRange(pin, minVal, maxVal)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.print(F(" alarm range set to "));
            msg.control.print(minVal);
            msg.control.print(F(" - "));
            msg.control.println(maxVal);
            return 0;
        }
        return 1;
    }

    // ===== CALIBRATION COMMANDS =====

    // SET <pin> CALIBRATION PRESET
    // Clears custom calibration and reverts to sensor library preset
    if (streq(field, "CALIBRATION")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: CALIBRATION requires PRESET"));
            return 1;
        }
        if (!streq(argv[3], "PRESET")) {
            msg.control.println(F("ERROR: Unknown CALIBRATION subcommand"));
            msg.control.println(F("  Use: SET <pin> CALIBRATION PRESET"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        // Clear custom calibration
        input->flags.useCustomCalibration = false;
        memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

        msg.control.print(F("Cleared custom calibration for pin "));
        msg.control.println(argv[1]);
        msg.control.println(F("Using preset calibration from sensor library"));
        return 0;
    }

    // SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
    // Supports 5 parameters (mult defaults to 1.0) or 6 parameters (custom mult)
    if (streq(field, "RPM")) {
        if (argc < 8 && argc < 9) {
            msg.control.println(F("ERROR: RPM requires 5 or 6 parameters"));
            msg.control.println(F("  Usage: SET <pin> RPM <poles> <ratio> <timeout> <min> <max>"));
            msg.control.println(F("     or: SET <pin> RPM <poles> <ratio> <mult> <timeout> <min> <max>"));
            msg.control.println(F("  Example: SET 5 RPM 12 3.0 2000 100 8000"));
            msg.control.println(F("       or: SET 5 RPM 12 3.0 1.02 2000 100 8000"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        byte poles;
        float pulley_ratio;
        float calibration_mult = 1.0;
        uint16_t timeout_ms;
        uint16_t min_rpm;
        uint16_t max_rpm;

        int paramStart = 3;
        bool hasCustomMult = (argc == 9);

        poles = (byte)atoi(argv[paramStart]);
        pulley_ratio = atof(argv[paramStart + 1]);

        if (hasCustomMult) {
            calibration_mult = atof(argv[paramStart + 2]);
            timeout_ms = (uint16_t)atoi(argv[paramStart + 3]);
            min_rpm = (uint16_t)atoi(argv[paramStart + 4]);
            max_rpm = (uint16_t)atoi(argv[paramStart + 5]);
        } else {
            timeout_ms = (uint16_t)atoi(argv[paramStart + 2]);
            min_rpm = (uint16_t)atoi(argv[paramStart + 3]);
            max_rpm = (uint16_t)atoi(argv[paramStart + 4]);
        }

        // Validate parameters
        if (poles < 2 || poles > 32) {
            msg.control.println(F("ERROR: Poles must be between 2 and 32"));
            return 1;
        }
        if (pulley_ratio < 0.5 || pulley_ratio > 10.0) {
            msg.control.println(F("ERROR: Pulley ratio must be between 0.5 and 10.0"));
            return 1;
        }
        if (calibration_mult < 0.5 || calibration_mult > 2.0) {
            msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
            return 1;
        }
        if (timeout_ms < 100 || timeout_ms > 10000) {
            msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
            return 1;
        }
        if (min_rpm >= max_rpm) {
            msg.control.println(F("ERROR: min_rpm must be less than max_rpm"));
            return 1;
        }

        // Set custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_RPM;
        input->customCalibration.rpm.poles = poles;
        input->customCalibration.rpm.pulley_ratio = pulley_ratio;
        input->customCalibration.rpm.calibration_mult = calibration_mult;
        input->customCalibration.rpm.timeout_ms = timeout_ms;
        input->customCalibration.rpm.min_rpm = min_rpm;
        input->customCalibration.rpm.max_rpm = max_rpm;

        msg.control.print(F("RPM calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Poles: "));
        msg.control.println(poles);
        msg.control.print(F("  Pulley Ratio: "));
        msg.control.print(pulley_ratio, 2);
        msg.control.println(F(":1"));
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  Valid Range: "));
        msg.control.print(min_rpm);
        msg.control.print(F("-"));
        msg.control.print(max_rpm);
        msg.control.println(F(" RPM"));
        float effective_ppr = (poles / 2.0) * pulley_ratio * calibration_mult;
        msg.control.print(F("  Effective: "));
        msg.control.print(effective_ppr, 2);
        msg.control.println(F(" pulses/engine-rev"));
        return 0;
    }

    // SET <pin> SPEED <pulses_per_rev> <tire_circ_mm> <drive_ratio> [<mult>] <timeout> <max_speed>
    // Supports 5 parameters (mult defaults to 1.0) or 6 parameters (custom mult)
    if (streq(field, "SPEED")) {
        if (argc < 8 && argc < 9) {
            msg.control.println(F("ERROR: SPEED requires 5 or 6 parameters"));
            msg.control.println(F("  Usage: SET <pin> SPEED <ppr> <tire_circ> <ratio> <timeout> <max_speed>"));
            msg.control.println(F("     or: SET <pin> SPEED <ppr> <tire_circ> <ratio> <mult> <timeout> <max_speed>"));
            msg.control.println(F("  Example: SET 2 SPEED 100 2008 3.73 2000 300"));
            msg.control.println(F("       or: SET 2 SPEED 100 2008 3.73 1.05 2000 300"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        uint8_t pulses_per_rev;
        uint16_t tire_circumference_mm;
        float final_drive_ratio;
        float calibration_mult = 1.0;
        uint16_t timeout_ms;
        uint16_t max_speed_kph;

        int paramStart = 3;
        bool hasCustomMult = (argc == 9);

        pulses_per_rev = (uint8_t)atoi(argv[paramStart]);
        tire_circumference_mm = (uint16_t)atoi(argv[paramStart + 1]);
        final_drive_ratio = atof(argv[paramStart + 2]);

        if (hasCustomMult) {
            calibration_mult = atof(argv[paramStart + 3]);
            timeout_ms = (uint16_t)atoi(argv[paramStart + 4]);
            max_speed_kph = (uint16_t)atoi(argv[paramStart + 5]);
        } else {
            timeout_ms = (uint16_t)atoi(argv[paramStart + 3]);
            max_speed_kph = (uint16_t)atoi(argv[paramStart + 4]);
        }

        // Validate parameters
        if (pulses_per_rev < 1 || pulses_per_rev > 250) {
            msg.control.println(F("ERROR: Pulses per rev must be between 1 and 250"));
            return 1;
        }
        if (tire_circumference_mm < 500 || tire_circumference_mm > 5000) {
            msg.control.println(F("ERROR: Tire circumference must be between 500 and 5000 mm"));
            return 1;
        }
        if (final_drive_ratio < 0.5 || final_drive_ratio > 10.0) {
            msg.control.println(F("ERROR: Drive ratio must be between 0.5 and 10.0"));
            return 1;
        }
        if (calibration_mult < 0.5 || calibration_mult > 2.0) {
            msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
            return 1;
        }
        if (timeout_ms < 100 || timeout_ms > 10000) {
            msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
            return 1;
        }
        if (max_speed_kph < 50 || max_speed_kph > 500) {
            msg.control.println(F("ERROR: Max speed must be between 50 and 500 km/h"));
            return 1;
        }

        // Set custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_SPEED;
        input->customCalibration.speed.pulses_per_rev = pulses_per_rev;
        input->customCalibration.speed.tire_circumference_mm = tire_circumference_mm;
        input->customCalibration.speed.final_drive_ratio = final_drive_ratio;
        input->customCalibration.speed.calibration_mult = calibration_mult;
        input->customCalibration.speed.timeout_ms = timeout_ms;
        input->customCalibration.speed.max_speed_kph = max_speed_kph;

        msg.control.print(F("Speed calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Pulses/Rev: "));
        msg.control.println(pulses_per_rev);
        msg.control.print(F("  Tire Circumference: "));
        msg.control.print(tire_circumference_mm);
        msg.control.println(F(" mm"));
        msg.control.print(F("  Drive Ratio: "));
        msg.control.print(final_drive_ratio, 2);
        msg.control.println(F(":1"));
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  Max Speed: "));
        msg.control.print(max_speed_kph);
        msg.control.println(F(" km/h"));
        return 0;
    }

    // SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
    if (streq(field, "PRESSURE_LINEAR")) {
        if (argc < 7) {
            msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
            msg.control.println(F("  Usage: SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
            msg.control.println(F("  Example: SET A1 PRESSURE_LINEAR 0.5 4.5 0.0 7.0"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input || !input->flags.isEnabled) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        float vmin = atof(argv[3]);
        float vmax = atof(argv[4]);
        float pmin = atof(argv[5]);
        float pmax = atof(argv[6]);

        // Validate parameters
        if (vmin >= vmax) {
            msg.control.println(F("ERROR: vmin must be less than vmax"));
            return 1;
        }
        if (vmin < 0.0 || vmax > SYSTEM_VOLTAGE) {
            msg.control.print(F("ERROR: Voltage range must be 0.0-"));
            msg.control.print(SYSTEM_VOLTAGE);
            msg.control.println(F("V for this platform"));
            return 1;
        }
        if (pmin >= pmax) {
            msg.control.println(F("ERROR: pmin must be less than pmax"));
            return 1;
        }
        if (pmin < 0.0) {
            msg.control.println(F("ERROR: pmin must be >= 0.0"));
            return 1;
        }

        // Apply custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_LINEAR;
        input->customCalibration.pressureLinear.voltage_min = vmin;
        input->customCalibration.pressureLinear.voltage_max = vmax;
        input->customCalibration.pressureLinear.output_min = pmin;
        input->customCalibration.pressureLinear.output_max = pmax;

        msg.control.print(F("Pressure Linear calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Voltage Range: "));
        msg.control.print(vmin, 2);
        msg.control.print(F("-"));
        msg.control.print(vmax, 2);
        msg.control.println(F(" V"));
        msg.control.print(F("  Pressure Range: "));
        msg.control.print(pmin, 2);
        msg.control.print(F("-"));
        msg.control.print(pmax, 2);
        msg.control.println(F(" bar"));
        return 0;
    }

    // SET <pin> BIAS <resistor>
    // Generic bias resistor command - works with Steinhart-Hart, Lookup, and Pressure Polynomial
    if (streq(field, "BIAS")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: BIAS requires a resistor value"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input || !input->flags.isEnabled) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        float bias = atof(argv[3]);

        // Validate calibration type supports bias resistor
        if (input->calibrationType != CAL_THERMISTOR_STEINHART &&
            input->calibrationType != CAL_THERMISTOR_TABLE &&
            input->calibrationType != CAL_THERMISTOR_BETA &&
            input->calibrationType != CAL_PRESSURE_POLYNOMIAL) {
            msg.control.print(F("ERROR: Calibration type "));
            msg.control.print(input->calibrationType);
            msg.control.println(F(" does not use bias resistor"));
            msg.control.println(F("  BIAS works with: Steinhart-Hart, Beta, Table, Pressure Polynomial"));
            return 1;
        }

        // Validate value (10Ω to 10MΩ covers all practical thermistors)
        #define BIAS_R_MIN 10.0
        #define BIAS_R_MAX 10000000.0
        if (bias < BIAS_R_MIN || bias > BIAS_R_MAX) {
            msg.control.print(F("ERROR: Bias resistor ("));
            msg.control.print(bias, 1);
            msg.control.println(F("Ω) must be between 10Ω and 10MΩ"));
            return 1;
        }

        // Apply to appropriate union member based on type
        input->flags.useCustomCalibration = true;

        if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
            input->customCalibration.steinhart.bias_resistor = bias;
        } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
            input->customCalibration.beta.bias_resistor = bias;
        } else if (input->calibrationType == CAL_THERMISTOR_TABLE) {
            input->customCalibration.lookup.bias_resistor = bias;
        } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
            input->customCalibration.pressurePolynomial.bias_resistor = bias;
        }

        msg.control.print(F("Bias resistor set for pin "));
        msg.control.print(argv[1]);
        msg.control.print(F(": "));
        msg.control.print(bias, 1);
        msg.control.println(F(" Ω"));
        return 0;
    }

    // SET <pin> STEINHART <bias_r> <a> <b> <c>
    if (streq(field, "STEINHART")) {
        if (argc < 7) {
            msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
            msg.control.println(F("  Usage: SET <pin> STEINHART <bias_r> <a> <b> <c>"));
            msg.control.println(F("  Example: SET A0 STEINHART 10000 0.001129 0.0002341 0.00000008775"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input || !input->flags.isEnabled) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        float bias_r = atof(argv[3]);
        float a = atof(argv[4]);
        float b = atof(argv[5]);
        float c = atof(argv[6]);

        // Validate parameters
        if (bias_r <= 0) {
            msg.control.println(F("ERROR: bias_r must be > 0"));
            return 1;
        }
        if (a == 0 || b == 0 || c == 0) {
            msg.control.println(F("WARNING: Zero coefficient detected - may indicate error"));
        }

        // Apply custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_THERMISTOR_STEINHART;
        input->customCalibration.steinhart.bias_resistor = bias_r;
        input->customCalibration.steinhart.steinhart_a = a;
        input->customCalibration.steinhart.steinhart_b = b;
        input->customCalibration.steinhart.steinhart_c = c;

        msg.control.print(F("Steinhart-Hart calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(bias_r, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(c, 10);
        return 0;
    }

    // SET <pin> BETA <bias_r> <beta> <r0> <t0>
    if (streq(field, "BETA")) {
        if (argc < 7) {
            msg.control.println(F("ERROR: BETA requires 4 parameters"));
            msg.control.println(F("  Usage: SET <pin> BETA <bias_r> <beta> <r0> <t0>"));
            msg.control.println(F("  Example: SET A0 BETA 10000 3950 10000 25"));
            msg.control.println(F("  Where: bias_r=bias resistor (Ω), beta=β coefficient (K),"));
            msg.control.println(F("         r0=ref resistance (Ω), t0=ref temp (°C, typically 25)"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input || !input->flags.isEnabled) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        float bias_r = atof(argv[3]);
        float beta = atof(argv[4]);
        float r0 = atof(argv[5]);
        float t0 = atof(argv[6]);

        // Validate parameters
        if (bias_r <= 0) {
            msg.control.println(F("ERROR: bias_r must be > 0"));
            return 1;
        }
        if (beta < 1000 || beta > 10000) {
            msg.control.println(F("WARNING: Beta typically 2000-6000K. Value may be incorrect."));
        }
        if (r0 <= 0) {
            msg.control.println(F("ERROR: r0 must be > 0"));
            return 1;
        }
        if (t0 < -40 || t0 > 150) {
            msg.control.println(F("WARNING: t0 typically 25°C. Value may be incorrect."));
        }

        // Apply custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_THERMISTOR_BETA;
        input->customCalibration.beta.bias_resistor = bias_r;
        input->customCalibration.beta.beta = beta;
        input->customCalibration.beta.r0 = r0;
        input->customCalibration.beta.t0 = t0;

        msg.control.print(F("Beta calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(bias_r, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  Beta: "));
        msg.control.print(beta, 1);
        msg.control.println(F(" K"));
        msg.control.print(F("  R0: "));
        msg.control.print(r0, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  T0: "));
        msg.control.print(t0, 1);
        msg.control.println(F(" °C"));
        return 0;
    }

    // SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>
    if (streq(field, "PRESSURE_POLY")) {
        if (argc < 7) {
            msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
            msg.control.println(F("  Usage: SET <pin> PRESSURE_POLY <bias_r> <a> <b> <c>"));
            msg.control.println(F("  Example: SET A1 PRESSURE_POLY 184 -6.75e-4 2.54e-6 1.87e-9"));
            return 1;
        }

        Input* input = getInputByPin(pin);
        if (!input || !input->flags.isEnabled) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }

        float bias_r = atof(argv[3]);
        float a = atof(argv[4]);
        float b = atof(argv[5]);
        float c = atof(argv[6]);

        // Validate parameters
        if (bias_r <= 0) {
            msg.control.println(F("ERROR: bias_r must be > 0"));
            return 1;
        }

        // Apply custom calibration
        input->flags.useCustomCalibration = true;
        input->calibrationType = CAL_PRESSURE_POLYNOMIAL;
        input->customCalibration.pressurePolynomial.bias_resistor = bias_r;
        input->customCalibration.pressurePolynomial.poly_a = a;
        input->customCalibration.pressurePolynomial.poly_b = b;
        input->customCalibration.pressurePolynomial.poly_c = c;

        msg.control.print(F("Pressure Polynomial calibration set for pin "));
        msg.control.println(argv[1]);
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(bias_r, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(c, 10);
        return 0;
    }

    // Unknown field
    msg.control.print(F("ERROR: Unknown SET field '"));
    msg.control.print(field);
    msg.control.println(F("'"));
    msg.control.println(F("  Type 'HELP SET' for usage information"));
    return 1;
}

static int cmd_enable(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: ENABLE requires a pin"));
        msg.control.println(F("  Usage: ENABLE <pin>"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    enableInput(pin, true);
    msg.control.print(F("Input "));
    msg.control.print(argv[1]);
    msg.control.println(F(" enabled"));
    return 0;
}

static int cmd_disable(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: DISABLE requires a pin"));
        msg.control.println(F("  Usage: DISABLE <pin>"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    enableInput(pin, false);
    msg.control.print(F("Input "));
    msg.control.print(argv[1]);
    msg.control.println(F(" disabled"));
    return 0;
}

static int cmd_clear(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: CLEAR requires a pin"));
        msg.control.println(F("  Usage: CLEAR <pin>"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    clearInput(pin);
    msg.control.print(F("Input "));
    msg.control.print(argv[1]);
    msg.control.println(F(" cleared"));
    return 0;
}

static int cmd_info(int argc, const char* const* argv) {
        if (argc < 2) {
        msg.control.println(F("ERROR: INFO requires a pin"));
        msg.control.println(F("  Usage: INFO <pin> [ALARM|CALIBRATION]"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    // Check for subcommands (ALARM, CALIBRATION)
    if (argc == 3) {
        if (streq(argv[2], "ALARM")) {
            printInputAlarmInfo(pin);
        } else if (streq(argv[2], "CALIBRATION")) {
            printInputCalibration(pin);
        } else {
            msg.control.print(F("ERROR: Unknown INFO subcommand '"));
            msg.control.print(argv[2]);
            msg.control.println(F("'"));
        }
    } else {
        // Default: INFO <pin>
        printInputInfo(pin);
    }

    return 0;
}

static int cmd_output(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: OUTPUT requires a subcommand"));
        msg.control.println(F("  Usage: OUTPUT STATUS | <name> ENABLE | DISABLE | INTERVAL <ms>"));
        return 1;
    }

    if (streq(argv[1], "STATUS")) {
        listOutputs();
        return 0;
    }

    // All other subcommands require a name
    if (argc < 3) {
        msg.control.println(F("ERROR: Subcommand requires an output name"));
        msg.control.println(F("  Usage: OUTPUT <name> ENABLE | DISABLE | INTERVAL <ms>"));
        return 1;
    }

    const char* outputName = argv[1];
    const char* subcommand = argv[2];

    if (streq(subcommand, "ENABLE")) {
        if (setOutputEnabled(outputName, true)) {
            msg.control.print(outputName);
            msg.control.println(F(" enabled"));
        } else {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
    } else if (streq(subcommand, "DISABLE")) {
        if (setOutputEnabled(outputName, false)) {
            msg.control.print(outputName);
            msg.control.println(F(" disabled"));
        } else {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
    } else if (streq(subcommand, "INTERVAL")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: INTERVAL requires a time in ms"));
            return 1;
        }
        uint16_t interval = atoi(argv[3]);
        if (setOutputInterval(outputName, interval)) {
            msg.control.print(outputName);
            msg.control.print(F(" interval set to "));
            msg.control.print(interval);
            msg.control.println(F("ms"));
        } else {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
    } else {
        msg.control.print(F("ERROR: Unknown subcommand '"));
        msg.control.print(subcommand);
        msg.control.println(F("'"));
        msg.control.println(F("Valid commands: STATUS, or <module> ENABLE|DISABLE|INTERVAL"));
        return 1;
    }

    return 0;
}

static int cmd_display(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: DISPLAY requires a subcommand"));
        msg.control.println(F("  Usage: DISPLAY STATUS | ENABLE | DISABLE | TYPE <type> | ADDRESS <hex> | INTERVAL <ms>"));
        return 1;
    }

    const char* subcommand = argv[1];

    // DISPLAY STATUS
    if (streq(subcommand, "STATUS")) {
        msg.control.println(F("=== Display Configuration ==="));
        msg.control.print(F("Status: "));
        msg.control.println(systemConfig.displayEnabled ? F("Enabled") : F("Disabled"));
        msg.control.print(F("Type: "));
        switch (systemConfig.displayType) {
            case DISPLAY_NONE: msg.control.println(F("None")); break;
            case DISPLAY_LCD: msg.control.println(F("LCD")); break;
            case DISPLAY_OLED: msg.control.println(F("OLED")); break;
            default: msg.control.println(F("Unknown")); break;
        }
        msg.control.print(F("LCD I2C Address: 0x"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
        msg.control.print(F("Update Interval: "));
        msg.control.print(systemConfig.lcdUpdateInterval);
        msg.control.println(F("ms"));
        return 0;
    }

    // DISPLAY ENABLE
    if (streq(subcommand, "ENABLE")) {
        systemConfig.displayEnabled = 1;
        setDisplayRuntime(true);
        msg.control.println(F("Display enabled (use SAVE to persist)"));
        return 0;
    }

    // DISPLAY DISABLE
    if (streq(subcommand, "DISABLE")) {
        systemConfig.displayEnabled = 0;
        setDisplayRuntime(false);
        msg.control.println(F("Display disabled (use SAVE to persist)"));
        return 0;
    }

    // DISPLAY TYPE <LCD|OLED|NONE>
    if (streq(subcommand, "TYPE")) {
        if (argc < 3) {
            msg.control.println(F("ERROR: TYPE requires a display type"));
            msg.control.println(F("  Valid types: LCD, OLED, NONE"));
            return 1;
        }
        const char* typeStr = argv[2];
        if (streq(typeStr, "LCD")) {
            systemConfig.displayType = DISPLAY_LCD;
            msg.control.println(F("Display type set to LCD"));
        } else if (streq(typeStr, "OLED")) {
            systemConfig.displayType = DISPLAY_OLED;
            msg.control.println(F("Display type set to OLED"));
        } else if (streq(typeStr, "NONE")) {
            systemConfig.displayType = DISPLAY_NONE;
            msg.control.println(F("Display type set to NONE"));
        } else {
            msg.control.print(F("ERROR: Invalid display type '"));
            msg.control.print(typeStr);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid types: LCD, OLED, NONE"));
            return 1;
        }
        return 0;
    }

    // DISPLAY ADDRESS <hex>
    if (streq(subcommand, "ADDRESS")) {
        if (argc < 3) {
            msg.control.println(F("ERROR: ADDRESS requires an I2C address"));
            msg.control.println(F("  Usage: DISPLAY ADDRESS <hex>"));
            return 1;
        }
        const char* addrStr = argv[2];
        char* endptr;
        long addr = strtol(addrStr, &endptr, 16);
        if (endptr == addrStr || addr < 0x03 || addr > 0x77) {
            msg.control.print(F("ERROR: Invalid I2C address '"));
            msg.control.print(addrStr);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid range: 0x03-0x77"));
            return 1;
        }
        systemConfig.lcdI2CAddress = (uint8_t)addr;
        msg.control.print(F("LCD I2C address set to 0x"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
        return 0;
    }

    // DISPLAY INTERVAL <ms>
    if (streq(subcommand, "INTERVAL")) {
        if (argc < 3) {
            msg.control.println(F("ERROR: INTERVAL requires a time in ms"));
            msg.control.println(F("  Usage: DISPLAY INTERVAL <ms>"));
            return 1;
        }
        uint16_t interval = atoi(argv[2]);
        systemConfig.lcdUpdateInterval = interval;
        msg.control.print(F("Display update interval set to "));
        msg.control.print(interval);
        msg.control.println(F(" ms"));
        return 0;
    }

    msg.control.print(F("ERROR: Unknown subcommand '"));
    msg.control.print(subcommand);
    msg.control.println(F("'"));
    msg.control.println(F("  Valid commands: STATUS, ENABLE, DISABLE, TYPE, ADDRESS, INTERVAL"));
    msg.control.println(F("  Note: Unit configuration moved to SYSTEM UNITS"));
    return 1;
}

static int cmd_transport(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: TRANSPORT requires a subcommand"));
        msg.control.println(F("  Usage: TRANSPORT STATUS | <plane> <transport>"));
        msg.control.println(F("  (Use LIST TRANSPORTS to see available transports)"));
        return 1;
    }

    if (streq(argv[1], "STATUS")) {
        router.printTransportStatus();
        return 0;
    }

    // All other subcommands require a plane and transport
    if (argc < 3) {
        msg.control.println(F("ERROR: Subcommand requires a plane and transport"));
        msg.control.println(F("  Usage: TRANSPORT <plane> <transport>"));
        return 1;
    }

    bool planeValid = false;
    MessagePlane plane = parsePlane(argv[1], &planeValid);
    if (!planeValid) {
        msg.control.print(F("ERROR: Unknown plane '"));
        msg.control.print(argv[1]);
        msg.control.println(F("'"));
        return 1;
    }

    bool transportValid = false;
    TransportID transport = parseTransport(argv[2], &transportValid);
    if (!transportValid) {
        msg.control.print(F("ERROR: Unknown transport '"));
        msg.control.print(argv[2]);
        msg.control.println(F("'"));
        return 1;
    }

    if (router.setTransport(plane, transport)) {
        msg.control.print(F("Set "));
        msg.control.print(argv[1]);
        msg.control.print(F(" → "));
        msg.control.println(argv[2]);

        // Sync router state to systemConfig (will be persisted on SAVE)
        router.syncConfig();
        msg.control.println(F("Use SAVE to persist"));
    } else {
        // Provide specific error for disabled serial ports
        if (transport >= TRANSPORT_SERIAL1 && transport <= TRANSPORT_SERIAL8) {
            uint8_t port_id = transport - TRANSPORT_SERIAL1 + 1;
            msg.control.print(F("ERROR: Serial"));
            msg.control.print(port_id);
            msg.control.println(F(" is not enabled"));
            msg.control.print(F("  Run: BUS SERIAL "));
            msg.control.print(port_id);
            msg.control.println(F(" ENABLE"));
        } else {
            msg.control.print(F("ERROR: Transport '"));
            msg.control.print(argv[2]);
            msg.control.println(F("' not available"));
        }
        return 1;
    }

    return 0;
}

static int cmd_system(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: SYSTEM requires a subcommand"));
        msg.control.println(F("  Usage: SYSTEM STATUS | DUMP | UNITS | SEA_LEVEL | INTERVAL | REBOOT | RESET"));
        return 1;
    }

    if (streq(argv[1], "STATUS")) {
        printSystemStatus();
        return 0;
    }

    // SYSTEM DUMP [JSON]
    if (streq(argv[1], "DUMP")) {
        // Check for "SYSTEM DUMP JSON" variant
        if (argc == 3 && streq(argv[2], "JSON")) {
            msg.control.println();
            dumpConfigToJSON(Serial);
            msg.control.println();
            return 0;
        }

        // Regular SYSTEM DUMP (human-readable)
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  Full Configuration Dump"));
        msg.control.println(F("========================================"));
        msg.control.println();

        // Show inputs
        listAllInputs();
        msg.control.println();

        // Show outputs
        extern void listOutputs();
        listOutputs();
        msg.control.println();

        // Show display config
        printDisplayConfig();

        // Show bus config
        displayI2CStatus();
        displaySPIStatus();
        displayCANStatus();
        displaySerialStatus();
        msg.control.println();

        // Show system config
        extern void printSystemConfig();
        printSystemConfig();
        msg.control.println();

        return 0;
    }

    if (streq(argv[1], "SEA_LEVEL")) {
        if (argc < 3) {
            msg.control.println(F("ERROR: SEA_LEVEL requires a pressure in hPa"));
            return 1;
        }
        systemConfig.seaLevelPressure = atof(argv[2]);
        msg.control.print(F("Sea level pressure set to "));
        msg.control.print(systemConfig.seaLevelPressure);
        msg.control.println(F(" hPa"));
        return 0;
    }

    // SYSTEM UNITS <type> <unit>
    if (streq(argv[1], "UNITS")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: UNITS requires type and unit"));
            msg.control.println(F("  Usage: SYSTEM UNITS TEMP <C|F>"));
            msg.control.println(F("  Usage: SYSTEM UNITS PRESSURE <BAR|PSI|KPA|INHG>"));
            msg.control.println(F("  Usage: SYSTEM UNITS ELEVATION <M|FT>"));
            msg.control.println(F("  Usage: SYSTEM UNITS SPEED <KPH|MPH>"));
            return 1;
        }

        const char* unitType = argv[2];
        const char* unitStr = argv[3];
        uint8_t index = getUnitsIndexByName(unitStr);
        const UnitsInfo* info = getUnitsByIndex(index);

        if (!info) {
            msg.control.print(F("ERROR: Unknown unit '"));
            msg.control.print(unitStr);
            msg.control.println(F("'"));
            return 1;
        }

        MeasurementType measurementType = (MeasurementType)pgm_read_byte(&info->measurementType);

        if (streq(unitType, "TEMP")) {
            if (measurementType != MEASURE_TEMPERATURE) {
                msg.control.print(F("ERROR: '"));
                msg.control.print(unitStr);
                msg.control.println(F("' is not a temperature unit"));
                msg.control.println(F("  Valid: C, F, CELSIUS, FAHRENHEIT"));
                return 1;
            }
            systemConfig.defaultTempUnits = index;
            msg.control.print(F("Default temperature units set to "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
        } else if (streq(unitType, "PRESSURE")) {
            if (measurementType != MEASURE_PRESSURE) {
                msg.control.print(F("ERROR: '"));
                msg.control.print(unitStr);
                msg.control.println(F("' is not a pressure unit"));
                msg.control.println(F("  Valid: BAR, PSI, KPA, INHG"));
                return 1;
            }
            systemConfig.defaultPressUnits = index;
            msg.control.print(F("Default pressure units set to "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
        } else if (streq(unitType, "ELEVATION")) {
            if (measurementType != MEASURE_ELEVATION) {
                msg.control.print(F("ERROR: '"));
                msg.control.print(unitStr);
                msg.control.println(F("' is not an elevation unit"));
                msg.control.println(F("  Valid: M, FT, METERS, FEET"));
                return 1;
            }
            systemConfig.defaultElevUnits = index;
            msg.control.print(F("Default elevation units set to "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
        } else if (streq(unitType, "SPEED")) {
            if (measurementType != MEASURE_SPEED) {
                msg.control.print(F("ERROR: '"));
                msg.control.print(unitStr);
                msg.control.println(F("' is not a speed unit"));
                msg.control.println(F("  Valid: KPH, MPH"));
                return 1;
            }
            systemConfig.defaultSpeedUnits = index;
            msg.control.print(F("Default speed units set to "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(index));
        } else {
            msg.control.print(F("ERROR: Unknown unit type '"));
            msg.control.print(unitType);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid types: TEMP, PRESSURE, ELEVATION, SPEED"));
            return 1;
        }
        return 0;
    }

    if (streq(argv[1], "INTERVAL")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: INTERVAL requires a type and time in ms"));
            msg.control.println(F("  Usage: SYSTEM INTERVAL <SENSOR|ALARM> <ms>"));
            return 1;
        }
        uint16_t interval = atoi(argv[3]);
        if (streq(argv[2], "SENSOR")) {
            systemConfig.sensorReadInterval = interval;
            msg.control.print(F("Sensor read interval set to "));
            msg.control.print(interval);
            msg.control.println(F(" ms"));
        } else if (streq(argv[2], "ALARM")) {
            systemConfig.alarmCheckInterval = interval;
            msg.control.print(F("Alarm check interval set to "));
            msg.control.print(interval);
            msg.control.println(F(" ms"));
        } else {
            msg.control.print(F("ERROR: Unknown interval type '"));
            msg.control.print(argv[2]);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid types: SENSOR, ALARM"));
            return 1;
        }
        return 0;
    }

    // SYSTEM REBOOT - Restart the device
    if (streq(argv[1], "REBOOT")) {
        msg.control.println(F("Rebooting system..."));
        platformReboot();
        return 0;
    }

    // SYSTEM RESET CONFIRM - Factory reset (clear config + reboot)
    if (streq(argv[1], "RESET")) {
        if (argc == 3 && streq(argv[2], "CONFIRM")) {
            msg.control.println(F("Factory reset: Erasing all configuration..."));
            resetInputConfig();
            resetSystemConfig();
            saveSystemConfig();
            msg.control.println(F("Configuration reset complete"));
            msg.control.println(F("Rebooting..."));
            platformReboot();
            return 0;
        }

        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  WARNING: Factory Reset"));
        msg.control.println(F("  This will erase ALL configuration"));
        msg.control.println(F("  and reboot the device"));
        msg.control.println(F("  Type: SYSTEM RESET CONFIRM"));
        msg.control.println(F("========================================"));
        msg.control.println();
        return 0;
    }

    msg.control.print(F("ERROR: Unknown subcommand '"));
    msg.control.print(argv[1]);
    msg.control.println(F("'"));
    msg.control.println(F("Valid commands: STATUS, DUMP, SEA_LEVEL, UNITS, INTERVAL, REBOOT, RESET"));
    return 1;
}

#ifdef ENABLE_RELAY_OUTPUT
static int cmd_relay(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: RELAY requires a subcommand"));
        msg.control.println(F("  Usage: RELAY LIST | <index> <subcommand> <args>"));
        return 1;
    }

    if (streq(argv[1], "LIST")) {
        printAllRelayStatus();
        return 0;
    }

    // All other subcommands require a relay index
    if (argc < 3) {
        msg.control.println(F("ERROR: Subcommand requires a relay index"));
        return 1;
    }

    uint8_t relayIndex = atoi(argv[1]);

    // Validate relay index
    if (relayIndex >= MAX_RELAYS) {
        msg.control.print(F("ERROR: Invalid relay index (0-"));
        msg.control.print(MAX_RELAYS - 1);
        msg.control.println(F(")"));
        return 1;
    }

    const char* subcommand = argv[2];

    if (streq(subcommand, "STATUS")) {
        printRelayStatus(relayIndex);
    } else if (streq(subcommand, "PIN")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: PIN requires a pin number"));
            return 1;
        }
        setRelayPin(relayIndex, atoi(argv[3]));
        msg.control.print(F("Relay "));
        msg.control.print(relayIndex);
        msg.control.print(F(" output pin set to "));
        msg.control.println(argv[3]);
    } else if (streq(subcommand, "INPUT")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: INPUT requires a pin name"));
            return 1;
        }
        bool valid = false;
        uint8_t pin = parsePin(argv[3], &valid);
        if (valid) {
            setRelayInput(relayIndex, pin);
            msg.control.print(F("Relay "));
            msg.control.print(relayIndex);
            msg.control.print(F(" monitoring input "));
            msg.control.println(argv[3]);
        } else {
            msg.control.print(F("ERROR: Invalid pin '"));
            msg.control.print(argv[3]);
            msg.control.println(F("'"));
            return 1;
        }
    } else if (streq(subcommand, "THRESHOLD")) {
        if (argc < 5) {
            msg.control.println(F("ERROR: THRESHOLD requires on and off values"));
            return 1;
        }
        setRelayThresholds(relayIndex, atof(argv[3]), atof(argv[4]));
        msg.control.print(F("Relay "));
        msg.control.print(relayIndex);
        msg.control.print(F(" thresholds: ON="));
        msg.control.print(argv[3]);
        msg.control.print(F(", OFF="));
        msg.control.println(argv[4]);
    } else if (streq(subcommand, "MODE")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: MODE requires a mode name"));
            return 1;
        }
        RelayMode mode;
        bool modeValid = false;
        if (streq(argv[3], "DISABLED")) { mode = RELAY_DISABLED; modeValid = true; }
        else if (streq(argv[3], "AUTO_HIGH")) { mode = RELAY_AUTO_HIGH; modeValid = true; }
        else if (streq(argv[3], "AUTO_LOW")) { mode = RELAY_AUTO_LOW; modeValid = true; }
        else if (streq(argv[3], "MANUAL_ON")) { mode = RELAY_MANUAL_ON; modeValid = true; }
        else if (streq(argv[3], "MANUAL_OFF")) { mode = RELAY_MANUAL_OFF; modeValid = true; }

        if (modeValid) {
            setRelayMode(relayIndex, mode);
            msg.control.print(F("Relay "));
            msg.control.print(relayIndex);
            msg.control.print(F(" mode set to "));
            msg.control.println(argv[3]);
        } else {
            msg.control.print(F("ERROR: Unknown mode '"));
            msg.control.print(argv[3]);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid modes: DISABLED, AUTO_HIGH, AUTO_LOW, MANUAL_ON, MANUAL_OFF"));
            return 1;
        }
    } else {
        msg.control.print(F("ERROR: Unknown subcommand '"));
        msg.control.print(subcommand);
        msg.control.println(F("'"));
        msg.control.println(F("Valid commands: LIST, STATUS, PIN, INPUT, THRESHOLD, MODE"));
        return 1;
    }
    return 0;
}
#endif

#ifdef ENABLE_TEST_MODE
static int cmd_test(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: TEST requires a subcommand"));
        msg.control.println(F("  Usage: TEST LIST"));
        msg.control.println(F("  Usage: TEST <0-N>"));
        msg.control.println(F("  Usage: TEST STOP"));
        msg.control.println(F("  Usage: TEST STATUS"));
        return 1;
    }

    const char* subcommand = argv[1];

    // TEST LIST
    if (streq(subcommand, "LIST")) {
        listTestScenarios();
        return 0;
    }

    // TEST STOP
    if (streq(subcommand, "STOP")) {
        if (!isTestModeActive()) {
            msg.control.println(F("No test scenario is currently running"));
        } else {
            stopTestMode();
            msg.control.println(F("Test mode stopped"));
        }
        return 0;
    }

    // TEST STATUS
    if (streq(subcommand, "STATUS")) {
        if (!isTestModeActive()) {
            msg.control.println(F("Test mode: INACTIVE"));
        } else {
            msg.control.println(F("Test mode: ACTIVE"));
            msg.control.println(F("  Use TEST LIST to see all scenarios"));
        }
        return 0;
    }

    // TEST <scenario_number> - Try to parse as a number
    char* endPtr;
    long scenarioNum = strtol(subcommand, &endPtr, 10);

    // Check if parsing succeeded and entire string was consumed
    if (endPtr != subcommand && *endPtr == '\0') {
        // Valid number
        if (scenarioNum < 0 || scenarioNum >= getNumTestScenarios()) {
            msg.control.print(F("ERROR: Invalid scenario index (must be 0-"));
            msg.control.print(getNumTestScenarios() - 1);
            msg.control.println(F(")"));
            msg.control.println(F("Use TEST LIST to see available scenarios"));
            return 1;
        }

        // Start the test scenario
        if (startTestScenario((uint8_t)scenarioNum)) {
            msg.control.println(F("Test scenario started"));
            msg.control.println(F("  Use TEST STATUS to check progress"));
            msg.control.println(F("  Use TEST STOP to end early"));
        } else {
            msg.control.println(F("ERROR: Failed to start test scenario"));
            return 1;
        }
        return 0;
    }

    // Unknown TEST subcommand
    msg.control.print(F("ERROR: Unknown subcommand '"));
    msg.control.print(subcommand);
    msg.control.println(F("'"));
    msg.control.println(F("  Usage: TEST LIST"));
    msg.control.println(F("  Usage: TEST <0-N>"));
    msg.control.println(F("  Usage: TEST STOP"));
    msg.control.println(F("  Usage: TEST STATUS"));
    return 1;
}
#endif

static int cmd_bus(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println();
        msg.control.println(F("Commands:"));
        msg.control.println(F("  BUS I2C [0|1|2]           - Show or select I2C bus"));
        msg.control.println(F("  BUS I2C CLOCK <kHz>       - Set I2C clock (100/400/1000)"));
        msg.control.println(F("  BUS SPI [0|1|2]           - Show or select SPI bus"));
        msg.control.println(F("  BUS SPI CLOCK <Hz>        - Set SPI clock"));
        msg.control.println(F("  BUS CAN [0|1|2]           - Show or select CAN bus"));
        msg.control.println(F("  BUS CAN BAUDRATE <bps>    - Set CAN baudrate"));
        msg.control.println(F("  BUS SERIAL                - Show all serial ports"));
        msg.control.println(F("  BUS SERIAL <1-8> ENABLE [baud] - Enable serial port"));
        msg.control.println(F("  BUS SERIAL <1-8> DISABLE  - Disable serial port"));
        msg.control.println(F("  BUS SERIAL <1-8> BAUDRATE <rate> - Set baud rate"));
        return 0;
    }

    const char* busType = argv[1];

    // -------------------------------------------------------------------------
    // BUS I2C [0|1|2] or BUS I2C CLOCK <kHz>
    // -------------------------------------------------------------------------
    if (streq(busType, "I2C")) {
        // BUS I2C (no arguments) - display I2C status
        if (argc == 2) {
            displayI2CStatus();
            return 0;
        }

        // BUS I2C CLOCK <kHz>
        if (streq(argv[2], "CLOCK")) {
            if (argc < 4) {
                msg.control.println(F("ERROR: CLOCK requires a speed in kHz"));
                msg.control.println(F("  Usage: BUS I2C CLOCK <100|400|1000>"));
                return 1;
            }

            uint16_t clock = atoi(argv[3]);
            if (clock != 100 && clock != 400 && clock != 1000) {
                msg.control.println(F("ERROR: I2C clock must be 100, 400, or 1000 kHz"));
                return 1;
            }

            systemConfig.buses.i2c_clock = clock;
            msg.control.print(F("I2C clock set to "));
            msg.control.print(clock);
            msg.control.println(F("kHz"));
            msg.control.println(F("Note: Takes effect on next reboot"));
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS I2C <0|1|2> - Select bus
        uint8_t bus_id = atoi(argv[2]);
        if (bus_id >= NUM_I2C_BUSES) {
            msg.control.print(F("ERROR: I2C bus "));
            msg.control.print(bus_id);
            msg.control.print(F(" not available (0-"));
            msg.control.print(NUM_I2C_BUSES - 1);
            msg.control.println(F(")"));
            return 1;
        }

        systemConfig.buses.active_i2c = bus_id;
        msg.control.print(F("I2C bus set to "));
        msg.control.print(getI2CBusName(bus_id));
        msg.control.print(F(" (SDA="));
        msg.control.print(getDefaultI2CSDA(bus_id));
        msg.control.print(F(", SCL="));
        msg.control.print(getDefaultI2CSCL(bus_id));
        msg.control.println(F(")"));
        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // -------------------------------------------------------------------------
    // BUS SPI [0|1|2] or BUS SPI CLOCK <Hz>
    // -------------------------------------------------------------------------
    if (streq(busType, "SPI")) {
        // BUS SPI (no arguments) - display SPI status
        if (argc == 2) {
            displaySPIStatus();
            return 0;
        }

        // BUS SPI CLOCK <Hz>
        if (streq(argv[2], "CLOCK")) {
            if (argc < 4) {
                msg.control.println(F("ERROR: CLOCK requires a speed in Hz"));
                msg.control.println(F("  Usage: BUS SPI CLOCK <Hz>"));
                msg.control.println(F("  Example: BUS SPI CLOCK 4000000  (4MHz)"));
                return 1;
            }

            uint32_t clock = atol(argv[3]);
            if (clock < 100000 || clock > 50000000) {
                msg.control.println(F("ERROR: SPI clock must be 100000-50000000 Hz"));
                return 1;
            }

            systemConfig.buses.spi_clock = clock;
            msg.control.print(F("SPI clock set to "));
            msg.control.print(clock / 1000000.0, 1);
            msg.control.println(F("MHz"));
            msg.control.println(F("Note: Takes effect on next transaction"));
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS SPI <0|1|2> - Select bus
        uint8_t bus_id = atoi(argv[2]);
        if (bus_id >= NUM_SPI_BUSES) {
            msg.control.print(F("ERROR: SPI bus "));
            msg.control.print(bus_id);
            msg.control.print(F(" not available (0-"));
            msg.control.print(NUM_SPI_BUSES - 1);
            msg.control.println(F(")"));
            return 1;
        }

        systemConfig.buses.active_spi = bus_id;
        msg.control.print(F("SPI bus set to "));
        msg.control.print(getSPIBusName(bus_id));
        msg.control.print(F(" (MOSI="));
        msg.control.print(getDefaultSPIMOSI(bus_id));
        msg.control.print(F(", MISO="));
        msg.control.print(getDefaultSPIMISO(bus_id));
        msg.control.print(F(", SCK="));
        msg.control.print(getDefaultSPISCK(bus_id));
        msg.control.println(F(")"));
        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
    }

    // -------------------------------------------------------------------------
    // BUS CAN [0|1|2] or BUS CAN BAUDRATE <bps>
    // -------------------------------------------------------------------------
    if (streq(busType, "CAN")) {
#if NUM_CAN_BUSES == 0
        msg.control.println(F("ERROR: No CAN buses available on this platform"));
        return 1;
#else
        // BUS CAN (no arguments) - display CAN status
        if (argc == 2) {
            displayCANStatus();
            return 0;
        }

        // BUS CAN BAUDRATE <bps>
        if (streq(argv[2], "BAUDRATE")) {
            if (argc < 4) {
                msg.control.println(F("ERROR: BAUDRATE requires a speed in bps"));
                msg.control.println(F("  Usage: BUS CAN BAUDRATE <125000|250000|500000|1000000>"));
                return 1;
            }

            uint32_t baudrate = atol(argv[3]);
            if (baudrate != 125000 && baudrate != 250000 && baudrate != 500000 && baudrate != 1000000) {
                msg.control.println(F("ERROR: CAN baudrate must be 125000, 250000, 500000, or 1000000"));
                return 1;
            }

            systemConfig.buses.can_baudrate = baudrate;
            msg.control.print(F("CAN baudrate set to "));
            msg.control.print(baudrate / 1000);
            msg.control.println(F("kbps"));
            msg.control.println(F("Note: Takes effect on next reboot"));
            msg.control.println(F("Use SAVE to persist"));
            return 0;
        }

        // BUS CAN <0|1|2> - Select bus
        uint8_t bus_id = atoi(argv[2]);
        if (bus_id >= NUM_CAN_BUSES) {
            msg.control.print(F("ERROR: CAN bus "));
            msg.control.print(bus_id);
            msg.control.print(F(" not available (0-"));
            msg.control.print(NUM_CAN_BUSES - 1);
            msg.control.println(F(")"));
            return 1;
        }

        systemConfig.buses.active_can = bus_id;
        msg.control.print(F("CAN bus set to "));
        msg.control.print(getCANBusName(bus_id));
        msg.control.print(F(" (TX="));
        msg.control.print(getDefaultCANTX(bus_id));
        msg.control.print(F(", RX="));
        msg.control.print(getDefaultCANRX(bus_id));
        msg.control.println(F(")"));
        msg.control.println(F("Note: Takes effect on next reboot"));
        msg.control.println(F("Use SAVE to persist"));
        return 0;
#endif
    }

    // -------------------------------------------------------------------------
    // BUS SERIAL [1-8] [ENABLE|DISABLE|BAUDRATE <rate>]
    // -------------------------------------------------------------------------
    if (streq(busType, "SERIAL")) {
#if NUM_SERIAL_PORTS == 0
        msg.control.println(F("ERROR: No serial ports available on this platform"));
        return 1;
#else
        // BUS SERIAL (no arguments) - display all serial port status
        if (argc == 2) {
            displaySerialStatus();
            return 0;
        }

        // Parse port number
        uint8_t port_id = atoi(argv[2]);

        // Check if it's a valid port number (1-8)
        if (port_id >= 1 && port_id <= 8) {
            // Validate port exists on this platform
            if (port_id > NUM_SERIAL_PORTS) {
                msg.control.print(F("ERROR: Serial"));
                msg.control.print(port_id);
                msg.control.print(F(" not available (1-"));
                msg.control.print(NUM_SERIAL_PORTS);
                msg.control.println(F(")"));
                return 1;
            }

            // BUS SERIAL <port> (no subcommand) - show port status
            if (argc == 3) {
                displaySerialPortStatus(port_id);
                return 0;
            }

            // BUS SERIAL <port> ENABLE [baudrate]
            if (streq(argv[3], "ENABLE")) {
                // Use saved baud rate from config, or 115200 if not set
                uint8_t baud_idx = systemConfig.serial.baudrate_index[port_id - 1];

                if (argc >= 5) {
                    uint32_t baudrate = atol(argv[4]);
                    baud_idx = getBaudRateIndex(baudrate);
                    if (getBaudRateFromIndex(baud_idx) != baudrate) {
                        msg.control.print(F("WARNING: Baud rate "));
                        msg.control.print(baudrate);
                        msg.control.print(F(" not supported, using "));
                        msg.control.println(getBaudRateFromIndex(baud_idx));
                    }
                }

                if (enableSerialPort(port_id, baud_idx)) {
                    msg.control.print(F("Serial"));
                    msg.control.print(port_id);
                    msg.control.print(F(" enabled @ "));
                    msg.control.print(getBaudRateString(baud_idx));
                    msg.control.print(F(" baud (RX="));
                    msg.control.print(getDefaultSerialRX(port_id));
                    msg.control.print(F(", TX="));
                    msg.control.print(getDefaultSerialTX(port_id));
                    msg.control.println(F(")"));
                    msg.control.println(F("Use SAVE to persist"));
                } else {
                    msg.control.print(F("ERROR: Failed to enable Serial"));
                    msg.control.println(port_id);
                }
                return 0;
            }

            // BUS SERIAL <port> DISABLE
            if (streq(argv[3], "DISABLE")) {
                if (disableSerialPort(port_id)) {
                    msg.control.print(F("Serial"));
                    msg.control.print(port_id);
                    msg.control.println(F(" disabled"));
                    msg.control.println(F("Use SAVE to persist"));
                }
                return 0;
            }

            // BUS SERIAL <port> BAUDRATE <rate>
            if (streq(argv[3], "BAUDRATE")) {
                if (argc < 5) {
                    msg.control.println(F("ERROR: BAUDRATE requires a speed"));
                    msg.control.println(F("  Usage: BUS SERIAL <port> BAUDRATE <rate>"));
                    msg.control.println(F("  Valid: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600"));
                    return 1;
                }

                uint32_t baudrate = atol(argv[4]);
                uint8_t baud_idx = getBaudRateIndex(baudrate);

                if (getBaudRateFromIndex(baud_idx) != baudrate) {
                    msg.control.print(F("WARNING: Baud rate "));
                    msg.control.print(baudrate);
                    msg.control.print(F(" not supported, using "));
                    msg.control.println(getBaudRateFromIndex(baud_idx));
                }

                systemConfig.serial.baudrate_index[port_id - 1] = baud_idx;
                msg.control.print(F("Serial"));
                msg.control.print(port_id);
                msg.control.print(F(" baudrate set to "));
                msg.control.println(getBaudRateString(baud_idx));
                msg.control.println(F("Note: Takes effect on next reboot"));
                msg.control.println(F("Use SAVE to persist"));
                return 0;
            }

            // Unknown subcommand for port
            msg.control.print(F("ERROR: Unknown command '"));
            msg.control.print(argv[3]);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid: ENABLE, DISABLE, BAUDRATE"));
            return 1;
        }

        // Not a port number - unknown subcommand
        msg.control.print(F("ERROR: Unknown serial command '"));
        msg.control.print(argv[2]);
        msg.control.println(F("'"));
        msg.control.println(F("  Usage: BUS SERIAL [1-8] [ENABLE|DISABLE|BAUDRATE <rate>]"));
        return 1;
#endif
    }

    // Unknown bus type
    msg.control.print(F("ERROR: Unknown bus type '"));
    msg.control.print(busType);
    msg.control.println(F("'"));
    msg.control.println(F("  Valid: I2C, SPI, CAN, SERIAL"));
    return 1;
}

// ===== LOG COMMAND =====
// Configure log filtering (levels and tags)
// Available in both CONFIG and RUN modes (STATUS/TAGS are read-only, LEVEL/TAG modify config)
static int cmd_log(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("Usage: LOG <subcommand>"));
        msg.control.println(F("  LOG STATUS              - Show current log configuration"));
        msg.control.println(F("  LOG TAGS                - List all available tags"));
        msg.control.println(F("  LOG LEVEL <plane> <lvl> - Set log level for plane"));
        msg.control.println(F("  LOG TAG <tag> <state>   - Enable/disable a tag"));
        msg.control.println();
        msg.control.println(F("Examples:"));
        msg.control.println(F("  LOG LEVEL DEBUG INFO    - Show INFO and above on debug plane"));
        msg.control.println(F("  LOG TAG SD DISABLE      - Hide all SD card messages"));
        msg.control.println(F("  LOG TAG ALL ENABLE      - Enable all tags"));
        return 1;
    }

    // Convert subcommand to uppercase
    char subcmd[16];
    strncpy(subcmd, argv[1], sizeof(subcmd) - 1);
    subcmd[sizeof(subcmd) - 1] = '\0';
    for (char* p = subcmd; *p; p++) *p = toupper(*p);

    // LOG STATUS - show current configuration
    if (streq(subcmd, "STATUS")) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  Log Filter Status"));
        msg.control.println(F("========================================"));

        // Show log levels for each plane
        msg.control.println(F("Log Levels:"));
        const char* planeNames[] = {"CONTROL", "DATA", "DEBUG"};
        for (int i = 0; i < 3; i++) {
            msg.control.print(F("  "));
            msg.control.print(planeNames[i]);
            msg.control.print(F(": "));
            LogLevel level = router.getLogFilter().getLevel(i);
            msg.control.println(router.getLogFilter().getLevelName(level));
        }

        msg.control.println();
        msg.control.println(F("Enabled Tags:"));

        // List enabled tags
        bool anyEnabled = false;
        for (uint8_t i = 0; i < NUM_LOG_TAGS; i++) {
            if (router.getLogFilter().isTagEnabled(i)) {
                const char* tagName = getTagName(i);
                if (tagName) {
                    msg.control.print(F("  "));
                    msg.control.println(tagName);
                    anyEnabled = true;
                }
            }
        }

        if (!anyEnabled) {
            msg.control.println(F("  (none)"));
        }

        msg.control.println(F("========================================"));
        msg.control.println();
        return 0;
    }

    // LOG TAGS - list all available tags with status
    if (streq(subcmd, "TAGS")) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  Available Log Tags"));
        msg.control.println(F("========================================"));

        for (uint8_t i = 0; i < NUM_LOG_TAGS; i++) {
            const char* tagName = getTagName(i);
            if (tagName) {
                bool enabled = router.getLogFilter().isTagEnabled(i);
                msg.control.print(F("  "));
                msg.control.print(tagName);
                msg.control.print(F(": "));
                msg.control.println(enabled ? F("ENABLED") : F("DISABLED"));
            }
        }

        msg.control.println(F("========================================"));
        msg.control.println();
        return 0;
    }

    // LOG LEVEL <plane> <level> - set log level
    if (streq(subcmd, "LEVEL")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: LEVEL requires plane and level"));
            msg.control.println(F("  Usage: LOG LEVEL <CONTROL|DATA|DEBUG> <NONE|ERROR|WARN|INFO|DEBUG>"));
            msg.control.println(F("  Examples:"));
            msg.control.println(F("    LOG LEVEL DEBUG ERROR  - Only show errors on debug plane"));
            msg.control.println(F("    LOG LEVEL DEBUG INFO   - Show INFO and above (INFO, WARN, ERROR)"));
            msg.control.println(F("    LOG LEVEL DEBUG DEBUG  - Show all messages (maximum verbosity)"));
            return 1;
        }

        // Parse plane name
        char planeName[16];
        strncpy(planeName, argv[2], sizeof(planeName) - 1);
        planeName[sizeof(planeName) - 1] = '\0';
        for (char* p = planeName; *p; p++) *p = toupper(*p);

        int plane = -1;
        if (streq(planeName, "CONTROL")) plane = PLANE_CONTROL;
        else if (streq(planeName, "DATA")) plane = PLANE_DATA;
        else if (streq(planeName, "DEBUG")) plane = PLANE_DEBUG;
        else {
            msg.control.print(F("ERROR: Unknown plane '"));
            msg.control.print(planeName);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid planes: CONTROL, DATA, DEBUG"));
            return 1;
        }

        // Parse level name
        LogLevel level = router.getLogFilter().parseLevelName(argv[3]);
        if (level == LOG_LEVEL_NONE && !streq(argv[3], "NONE") && !streq(argv[3], "none")) {
            msg.control.print(F("ERROR: Unknown level '"));
            msg.control.print(argv[3]);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid levels: NONE, ERROR, WARN, INFO, DEBUG"));
            return 1;
        }

        // Set the level
        router.getLogFilter().setLevel(plane, level);

        // Sync to systemConfig (so SAVE will persist it)
        router.syncConfig();

        msg.control.print(F("✓ "));
        msg.control.print(planeName);
        msg.control.print(F(" plane log level set to "));
        msg.control.println(router.getLogFilter().getLevelName(level));
        msg.control.println(F("  Use SAVE to persist this setting"));
        return 0;
    }

    // LOG TAG <tag> <ENABLE|DISABLE> - enable/disable tag
    if (streq(subcmd, "TAG")) {
        if (argc < 4) {
            msg.control.println(F("ERROR: TAG requires tag name and state"));
            msg.control.println(F("  Usage: LOG TAG <tagname> <ENABLE|DISABLE>"));
            msg.control.println(F("         LOG TAG ALL <ENABLE|DISABLE>"));
            msg.control.println(F("  Examples:"));
            msg.control.println(F("    LOG TAG SD DISABLE     - Hide SD card messages"));
            msg.control.println(F("    LOG TAG BME280 ENABLE  - Show BME280 sensor messages"));
            msg.control.println(F("    LOG TAG ALL ENABLE     - Enable all tags"));
            msg.control.println(F("  Use 'LOG TAGS' to see all available tags"));
            return 1;
        }

        // Parse state
        char stateName[16];
        strncpy(stateName, argv[3], sizeof(stateName) - 1);
        stateName[sizeof(stateName) - 1] = '\0';
        for (char* p = stateName; *p; p++) *p = toupper(*p);

        bool enable;
        if (streq(stateName, "ENABLE")) enable = true;
        else if (streq(stateName, "DISABLE")) enable = false;
        else {
            msg.control.print(F("ERROR: Unknown state '"));
            msg.control.print(stateName);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid states: ENABLE, DISABLE"));
            return 1;
        }

        // Check for ALL keyword
        char tagName[16];
        strncpy(tagName, argv[2], sizeof(tagName) - 1);
        tagName[sizeof(tagName) - 1] = '\0';
        for (char* p = tagName; *p; p++) *p = toupper(*p);

        if (streq(tagName, "ALL")) {
            if (enable) {
                router.getLogFilter().enableAllTags();
                msg.control.println(F("✓ All tags enabled"));
            } else {
                router.getLogFilter().disableAllTags();
                msg.control.println(F("✓ All tags disabled"));
            }
            // Sync to systemConfig (so SAVE will persist it)
            router.syncConfig();
            msg.control.println(F("  Use SAVE to persist this setting"));
            return 0;
        }

        // Look up tag ID
        uint8_t tagId = getTagID(argv[2]);
        if (tagId >= NUM_LOG_TAGS) {
            msg.control.print(F("ERROR: Unknown tag '"));
            msg.control.print(argv[2]);
            msg.control.println(F("'"));
            msg.control.println(F("  Use 'LOG TAGS' to see all available tags"));
            return 1;
        }

        // Enable/disable the tag
        router.getLogFilter().enableTag(tagId, enable);

        // Sync to systemConfig (so SAVE will persist it)
        router.syncConfig();

        msg.control.print(F("✓ Tag "));
        msg.control.print(getTagName(tagId));
        msg.control.print(F(" "));
        msg.control.println(enable ? F("enabled") : F("disabled"));
        msg.control.println(F("  Use SAVE to persist this setting"));
        return 0;
    }

    // Unknown subcommand
    msg.control.print(F("ERROR: Unknown LOG subcommand '"));
    msg.control.print(subcmd);
    msg.control.println(F("'"));
    msg.control.println(F("  Valid: STATUS, TAGS, LEVEL, TAG"));
    msg.control.println(F("  Use 'LOG' for usage help"));
    return 1;
}

#endif // USE_STATIC_CONFIG
