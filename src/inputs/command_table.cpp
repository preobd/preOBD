/*
 * command_table.cpp - Table-driven command dispatch implementation
 */

#include "../config.h"

#include "command_table.h"
#include "command_helpers.h"
#include "serial_config.h"
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
#include "../lib/json_registry.h"
#include "../lib/bus_manager.h"
#include "../lib/bus_defaults.h"
#include "../lib/serial_manager.h"
#include "../lib/pin_registry.h"
#include "../outputs/output_base.h"
#include "../lib/obd_query.h"
#include "../lib/display_manager.h"
#if ENABLE_RELAY_OUTPUT
#include "../outputs/output_relay.h"
#endif
#if ENABLE_ELM327
#include "../outputs/output_elm327.h"
#endif
#if ENABLE_TEST_MODE
#include "../test/test_mode.h"
#endif
#if ENABLE_CAN
#include "sensors/can/can_scan.h"
#include "../lib/can_sensor_library/standard_pids.h"
#endif
#include <string.h>
#include <ctype.h>

// AVR watchdog for system reset
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    #include <avr/wdt.h>
#endif

// Top-level handler prototypes live in command_handlers.h
#include "command_handlers.h"

// Platform-specific reboot helper (shared by REBOOT and SYSTEM REBOOT/RESET)
void platformReboot() {
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
#if SUPPORTS_JSON_IMPORT_STREAM
    {"JSON", cmd_json, "Import JSON configuration over serial", true},
#endif
    {"REBOOT", cmd_reboot, "", true},  // Undocumented alias for SYSTEM REBOOT
    {"BUS", cmd_bus, "Configure I2C/SPI/CAN buses", true},
    {"LOG", cmd_log, "Configure log levels and tags", false},
    {"AT", cmd_at, "Send raw AT command to a serial port", false},

#if ENABLE_RELAY_OUTPUT
    {"RELAY", cmd_relay, "Configure relay outputs", true},
#endif
#if ENABLE_TEST_MODE
    {"TEST", cmd_test, "Test mode control", false},
#endif
#if ENABLE_CAN
    {"SCAN", cmd_scan, "Scan CAN bus for PIDs", true},
#endif
#if ENABLE_SELFTEST
    {"SELFTEST", cmd_selftest, "Validate dispatch tables", false},
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
#if ENABLE_TEST_MODE
           streq(cmdName, "TEST") ||
#endif
           false;
}

// Subcommand dispatcher (see command_table.h).
//
// Subcommand tables are declared PROGMEM by callers (BUS_SUBCOMMANDS,
// SYSTEM_SUBCOMMANDS) so they live in flash on AVR. On AVR a direct
// `table[i].token` read returns a flash address dereferenced as RAM —
// garbage. Read both fields via pgm_read_ptr; on Teensy/ESP32 PROGMEM is
// a no-op and pgm_read_ptr is a plain dereference. Token strings are
// likewise PROGMEM-resident, so compare with streq_P.
int dispatchSubcommand(const Subcommand* table, uint8_t tableLen,
                       const char* token, const char* commandName,
                       int argc, const char* const* argv, int tokenIndex) {
    for (uint8_t i = 0; i < tableLen; i++) {
        const char* entryToken = (const char*)pgm_read_ptr(&table[i].token);
        if (streq_P(token, entryToken)) {
            SubcommandHandler h = (SubcommandHandler)pgm_read_ptr(&table[i].handler);
            return h(argc, argv, tokenIndex);
        }
    }
    msg.control.print(F("ERROR: Unknown "));
    msg.control.print(commandName);
    msg.control.print(F(" subcommand '"));
    msg.control.print(token);
    msg.control.println(F("'"));
    return 1;
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

#if ENABLE_SELFTEST
// Validate the top-level COMMANDS[] table. Strings here are RAM-resident
// (not declared PROGMEM), so pass tokenInProgmem=false.
static int selftestCommandsTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        failures += validateDispatchEntry(F("COMMANDS"), i,
                                          COMMANDS[i].name,
                                          (const void*)COMMANDS[i].handler,
                                          false);
    }
    return failures;
}

// SELFTEST — walk every dispatch table and assert invariants. Catches
// PROGMEM-access regressions on AVR (where a missing pgm_read_ptr returns
// garbage) the moment a user runs this command after flashing.
int cmd_selftest(int /*argc*/, const char* const* /*argv*/) {
    msg.control.println(F("Running dispatch-table selftest..."));
    int failures = 0;
    failures += selftestCommandsTable();
    failures += selftestSetTable();
    failures += selftestBusTable();
    failures += selftestSystemTable();

    msg.control.print(F("SELFTEST: "));
    if (failures == 0) {
        msg.control.println(F("PASS"));
        return 0;
    }
    msg.control.print(F("FAIL ("));
    msg.control.print(failures);
    msg.control.println(F(" issue(s))"));
    return 1;
}
#endif // ENABLE_SELFTEST

//=============================================================================
// Command Handler Implementations
//=============================================================================

int cmd_help(int argc, const char* const* argv) {
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

int cmd_list(int argc, const char* const* argv) {
    if (argc == 1) {
        msg.control.println(F("ERROR: LIST requires a subcommand"));
        msg.control.println(F("  Usage: LIST INPUTS | APPLICATIONS | SENSORS [category] | OUTPUTS | TRANSPORTS"));
        msg.control.println(F("         LIST APPLICATIONS JSON | SENSORS [category] JSON | OUTPUTS JSON"));
        msg.control.println(F("         LIST UNITS JSON | CATEGORIES JSON | PIDS JSON"));
        msg.control.println(F("         LIST MEASUREMENT_TYPES JSON | CALIBRATION_TYPES JSON"));
        return 1;
    }

    // Trailing JSON token check: "LIST <sub> JSON" or "LIST SENSORS <cat> JSON"
    bool asJson = (argc >= 3 && streq(argv[argc - 1], "JSON"));

// EMIT_JSON(call): on supporting platforms, wraps call with blank-line padding.
// On non-supporting platforms, emits an error and returns — and the call expression
// is absent from the macro expansion so the symbols are never referenced.
#if SUPPORTS_JSON_EXPORT
#define EMIT_JSON(call) do { msg.control.println(); call; msg.control.println(); } while(0)
#else
#define EMIT_JSON(call) do { msg.control.println(F("ERROR: JSON export not supported on this platform")); return 1; } while(0)
#endif

    if (streq(argv[1], "INPUTS")) {
        listAllInputs();
    } else if (streq(argv[1], "APPLICATIONS")) {
        if (asJson) { EMIT_JSON(writeApplicationsJson(msg.control)); }
        else { listApplicationPresets(); }
    } else if (streq(argv[1], "SENSORS")) {
        if (asJson) {
            // LIST SENSORS JSON  or  LIST SENSORS <category> JSON
            const char* filter = (argc == 4) ? argv[2] : nullptr;
            EMIT_JSON(writeSensorsJson(msg.control, filter));
        } else {
            // LIST SENSORS [category|filter]
            const char* filter = (argc >= 3) ? argv[2] : NULL;
            listSensors(filter);
        }
    } else if (streq(argv[1], "OUTPUTS")) {
        if (asJson) { EMIT_JSON(writeOutputsJson(msg.control)); }
        else { listOutputModules(); }
    } else if (streq(argv[1], "UNITS")) {
        if (asJson) { EMIT_JSON(writeUnitsJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST UNITS JSON")); }
    } else if (streq(argv[1], "CATEGORIES")) {
        if (asJson) { EMIT_JSON(writeCategoriesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST CATEGORIES JSON")); }
    } else if (streq(argv[1], "PIDS")) {
        if (asJson) { EMIT_JSON(writePidsJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST PIDS JSON")); }
    } else if (streq(argv[1], "MEASUREMENT_TYPES")) {
        if (asJson) { EMIT_JSON(writeMeasurementTypesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST MEASUREMENT_TYPES JSON")); }
    } else if (streq(argv[1], "CALIBRATION_TYPES")) {
        if (asJson) { EMIT_JSON(writeCalibrationTypesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST CALIBRATION_TYPES JSON")); }
    } else if (streq(argv[1], "TRANSPORTS")) {
        router.listAvailableTransports();
    } else {
        msg.control.print(F("ERROR: Unknown LIST subcommand '"));
        msg.control.print(argv[1]);
        msg.control.println(F("'"));
        msg.control.println(F("  Valid: INPUTS, APPLICATIONS, SENSORS, OUTPUTS, TRANSPORTS,"));
        msg.control.println(F("         UNITS, CATEGORIES, PIDS, MEASUREMENT_TYPES, CALIBRATION_TYPES"));
        return 1;
    }
    return 0;
}
#undef EMIT_JSON

int cmd_version(int argc, const char* const* argv) {
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
    msg.control.print(F("  System Config Version: "));
    msg.control.println(systemConfig.version);
    msg.control.print(F("  Active Inputs: "));
    extern uint8_t numActiveInputs;
    msg.control.print(numActiveInputs);
    msg.control.print(F("/"));
    msg.control.println(MAX_INPUTS);
    msg.control.println(F("========================================"));
    msg.control.println();
    return 0;
}

int cmd_config(int argc, const char* const* argv) {
    // CONFIG command now only enters configuration mode
    // CONFIG SAVE/LOAD have been removed - use SAVE FILE / LOAD FILE instead
    setMode(MODE_CONFIG);
    return 0;
}

int cmd_run(int argc, const char* const* argv) {
    setMode(MODE_RUN);
    return 0;
}

int cmd_save(int argc, const char* const* argv) {
    // Case 1: SAVE (bare) → EEPROM (backward compatible)
    if (argc == 1) {
        msg.control.println(F("Saving configuration to EEPROM..."));
        saveInputConfig();
        saveSystemConfig();
        obdQuery_buildLookupTable();  // Rebuild PID table — inputs may have changed
        msg.control.println(F("Configuration saved"));
        return 0;
    }

    // Case 2: SAVE EEPROM (explicit)
    if (argc == 2 && streq(argv[1], "EEPROM")) {
        msg.control.println(F("Saving configuration to EEPROM..."));
        saveInputConfig();
        saveSystemConfig();
        obdQuery_buildLookupTable();  // Rebuild PID table — inputs may have changed
        msg.control.println(F("Configuration saved"));
        return 0;
    }

    // Case 3: SAVE [destination:]filename (file path - anything that's not "EEPROM")
    if (argc >= 2) {
#if SUPPORTS_JSON_CONFIG && SUPPORTS_SD
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
#else
        msg.control.println(F("ERROR: File save not supported on this platform"));
        return 1;
#endif
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

int cmd_load(int argc, const char* const* argv) {
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
#if SUPPORTS_JSON_CONFIG && SUPPORTS_SD
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
#else
        msg.control.println(F("ERROR: File load not supported on this platform"));
        return 1;
#endif
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

#if SUPPORTS_JSON_IMPORT_STREAM
int cmd_json(int argc, const char* const* argv) {
    if (argc < 2 || !streq(argv[1], "IMPORT")) {
        msg.control.println(F("Usage: JSON IMPORT"));
        msg.control.println(F("  Paste JSON, then type 'END JSON' on its own line"));
        return 1;
    }
    if (!serialConfig_beginJsonImport()) {
        msg.control.println(F("ERROR: JSON import already in progress"));
        return 1;
    }
    return 0;
}
#endif

int cmd_reboot(int argc, const char* const* argv) {
    msg.control.println(F("Rebooting system..."));
    platformReboot();
    return 0;
}

// Stub implementations for remaining commands
// These will be filled in next


int cmd_enable(int argc, const char* const* argv) {
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

int cmd_disable(int argc, const char* const* argv) {
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

int cmd_clear(int argc, const char* const* argv) {
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

int cmd_info(int argc, const char* const* argv) {
        if (argc < 2) {
        msg.control.println(F("ERROR: INFO requires a pin"));
        msg.control.println(F("  Usage: INFO <pin> [ALARM|OUTPUT|CALIBRATION]"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    // Check for subcommands (ALARM, CALIBRATION, OUTPUT)
    if (argc == 3) {
        if (streq(argv[2], "ALARM")) {
            printInputAlarmInfo(pin);
        } else if (streq(argv[2], "OUTPUT")) {
            printInputOutputInfo(pin);
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

int cmd_output(int argc, const char* const* argv) {
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
            msg.control.println(F("  (use SAVE to persist)"));
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
            msg.control.println(F("  (use SAVE to persist)"));
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
            msg.control.println(F("  (use SAVE to persist)"));
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

int cmd_display(int argc, const char* const* argv) {
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

int cmd_transport(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: TRANSPORT requires a subcommand"));
        msg.control.println(F("  Usage: TRANSPORT STATUS | <plane> <transport> [SECONDARY]"));
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
        msg.control.println(F("  Usage: TRANSPORT <plane> <transport> [SECONDARY]"));
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

    // Optional SECONDARY keyword: TRANSPORT <plane> <transport> SECONDARY
    // Sets this as the secondary (listen-on + multicast-to) transport for the plane,
    // leaving the primary unchanged. Both primary and secondary are polled for input
    // and receive all output, enabling simultaneous control from two ports.
    if (argc >= 4 && !streq(argv[3], "SECONDARY")) {
        msg.control.print(F("ERROR: Unknown option '"));
        msg.control.print(argv[3]);
        msg.control.println(F("' (did you mean SECONDARY?)"));
        return 1;
    }
    bool isSecondary = (argc >= 4);

    // Conflict: refuse to assign a serial port that is owned by ELM327
#if ENABLE_ELM327
    if (transport >= TRANSPORT_SERIAL1 && transport <= TRANSPORT_SERIAL8) {
        uint8_t port_id = transport - TRANSPORT_SERIAL1 + 1;
        if (systemConfig.buses.elm327_serial_port == port_id) {
            msg.control.print(F("ERROR: Serial"));
            msg.control.print(port_id);
            msg.control.println(F(" is owned by ELM327"));
            msg.control.print(F("  Run: BUS SERIAL "));
            msg.control.print(port_id);
            msg.control.println(F(" ELM327 DISABLE first"));
            return 1;
        }
    }
#endif

    if (router.setTransport(plane, transport, isSecondary)) {
        msg.control.print(F("Set "));
        msg.control.print(argv[1]);
        if (isSecondary) msg.control.print(F(" secondary"));
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

#if ENABLE_RELAY_OUTPUT
int cmd_relay(int argc, const char* const* argv) {
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

#if ENABLE_TEST_MODE
int cmd_test(int argc, const char* const* argv) {
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

// Configure log filtering (levels and tags)
// Available in both CONFIG and RUN modes (STATUS/TAGS are read-only, LEVEL/TAG modify config)
int cmd_log(int argc, const char* const* argv) {
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

// ============================================================================
// SCAN COMMAND - CAN bus scanning
// ============================================================================

#if ENABLE_CAN
int cmd_scan(int argc, const char* const* argv) {
    // Usage: SCAN CAN [duration_ms]
    //        SCAN CANCEL

    if (argc < 2) {
        msg.control.println(F("SCAN - Scan CAN bus for active PIDs"));
        msg.control.println(F(""));
        msg.control.println(F("Usage:"));
        msg.control.println(F("  SCAN CAN [duration]  - Scan CAN bus (default 10000ms)"));
        msg.control.println(F("  SCAN CANCEL          - Cancel/clear scan results"));
        msg.control.println(F(""));
        msg.control.println(F("Examples:"));
        msg.control.println(F("  SCAN CAN             - Scan for 10 seconds"));
        msg.control.println(F("  SCAN CAN 15000       - Scan for 15 seconds"));
        msg.control.println(F("  SCAN CANCEL          - Clear results"));
        return 0;
    }

    const char* subcmd = argv[1];

    if (streq(subcmd, "CAN")) {
        // Get duration (default 10 seconds)
        uint16_t duration = 10000;
        if (argc >= 3) {
            duration = atoi(argv[2]);
            if (duration < 1000) {
                msg.control.println(F("ERROR: Duration must be at least 1000ms"));
                return 1;
            }
            if (duration > 60000) {
                msg.control.println(F("ERROR: Duration must be at most 60000ms"));
                return 1;
            }
        }

        // Check if CAN input is enabled
        if (systemConfig.buses.can_input_mode == CAN_INPUT_OFF) {
            msg.control.println(F("ERROR: CAN input not enabled"));
            msg.control.println(F("  Use 'BUS CAN INPUT CAN1 ENABLE' or 'BUS CAN INPUT CAN1 LISTEN' first"));
            return 1;
        }

        // Start scan
        startCANScan(duration);
        return 0;
    }

    if (streq(subcmd, "CANCEL")) {
        cancelCANScan();
        return 0;
    }

    // Unknown subcommand
    msg.control.print(F("ERROR: Unknown SCAN subcommand '"));
    msg.control.print(subcmd);
    msg.control.println(F("'"));
    msg.control.println(F("  Valid: CAN, CANCEL"));
    return 1;
}
#endif // ENABLE_CAN

// ============================================================================
// AT - Send raw AT command to a hardware serial port (no line ending)
// Usage: AT <port#> <command>
// Example: AT 1 AT+NAMEpreOBD
//          AT 1 AT+BAUD0
// ============================================================================
int cmd_at(int argc, const char* const* argv) {
    if (argc < 3) {
        msg.control.println(F("Usage: AT <port> <command>"));
        msg.control.println(F("  Sends raw bytes to a serial port with no line ending."));
        msg.control.println(F("  Example: AT 1 AT+NAMEpreOBD"));
        msg.control.println(F("           AT 1 AT+BAUD0       (9600, BLE-friendly)"));
        return 1;
    }

    uint8_t port_id = atoi(argv[1]);
    if (port_id < 1 || port_id > NUM_SERIAL_PORTS) {
        msg.control.print(F("ERROR: Invalid port (1-"));
        msg.control.print(NUM_SERIAL_PORTS);
        msg.control.println(F(")"));
        return 1;
    }

    if (!isSerialPortActive(port_id)) {
        msg.control.print(F("ERROR: Serial"));
        msg.control.print(port_id);
        msg.control.println(F(" is not active. Enable it first with BUS SERIAL <port> ENABLE"));
        return 1;
    }

    Stream* port = getSerialPort(port_id);
    if (!port) {
        msg.control.println(F("ERROR: Could not get serial port"));
        return 1;
    }

    // Drain any stale bytes before sending so we don't mix old data into the response
    while (port->available()) port->read();

    // Write the AT command raw (no CR/LF) — required by HM-10 and most BT modules
    port->print(argv[2]);
    port->flush();

    // Read response for up to 500ms and print it here, before router.update() can
    // grab it and misinterpret it as a command.
    msg.control.print(F("Serial"));
    msg.control.print(port_id);
    msg.control.print(F(" response: "));
    unsigned long deadline = millis() + 500;
    bool got_response = false;
    while (millis() < deadline) {
        while (port->available()) {
            msg.control.write((char)port->read());
            got_response = true;
            deadline = millis() + 100;  // extend deadline while data is arriving
        }
    }
    if (!got_response) {
        msg.control.print(F("(no response)"));
    }
    msg.control.println();
    return 0;
}
