/*
 * command_table.cpp - Table-driven command dispatch implementation
 */

#include "../config.h"

#include "command_table.h"
#include "command_dispatch.h"
#include "command_helpers.h"
#include "serial_config.h"
#include "../inputs/input_manager.h"
#include "../storage/input_store.h"  // saveInputConfig/loadInputConfig (#402 step 2)
#include "input_display.h"
#include "registry_display.h"
#include "../config.h"
#include "../version.h"
#include "../core/system_mode.h"
#include "../core/reset_reason.h"
#include "../core/ram_stats.h"
#include "../storage/system_config.h"
#include "../storage/json_config.h"
#include "../core/message_router.h"
#include "../core/message_api.h"
#include "../core/log_filter.h"
#include "../core/log_tags.h"
#include "../sensors/units_registry.h"
#include "../outputs/display/lcd_layout.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../core/platform.h"
#include "../storage/json_registry.h"
#include "../drivers/bus_manager.h"
#include "../hal/bus_defaults.h"
#include "../drivers/serial_manager.h"
#include "../pinctrl/pin_registry.h"
#include "../rules/rules_manager.h"  // control engine re-arm trio (LOAD path)
#include "../outputs/output_base.h"
#include "../outputs/output_format.h"   // SdLogFormat / parseSdLogFormat
#include "../outputs/obd_query.h"
#include "../outputs/display/display_manager.h"
#if INCLUDE_LED
#include "../drivers/rgb_led.h"   // TEST OUTPUT actuator
#endif
#include "../core/watchdog.h"
#if INCLUDE_CAN
#include "../inputs/input_can.h"   // canInputIsRunning (SCAN CAN)
#endif
#if INCLUDE_CAN && SUPPORTS_JSON_EXPORT
#include "../inputs/can_status_json.h"
#endif  // TEST OUTPUT feeds the WDT during its blocking sequence
#if INCLUDE_ELM327
#include "../outputs/output_elm327.h"
#endif
#if INCLUDE_TEST_MODE
#include "../test/test_mode.h"
#endif
#if INCLUDE_CAN
#include "../sensors/drivers/can/can_scan.h"
#include "../sensors/can_sensor_library/standard_pids.h"
#endif
#include "../core/config_generation.h"
#include <string.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif
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
    #elif defined(ARDUINO_ARCH_STM32)
        NVIC_SystemReset();
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
    {"CLEAR", cmd_clear, "Clear input", true},
    {"OUTPUT", cmd_output, "Configure outputs", true},
    {"DISPLAY", cmd_display, "Configure display", true},
    {"TRANSPORT", cmd_transport, "Configure message routing", true},
    // SYSTEM: configModeOnly=false; per-subcommand runModeAllowed flags in
    // SYSTEM_SUBCOMMANDS (cmd_system.cpp) gate the destructive verbs (RESET,
    // REBOOT, SEA_LEVEL, UNITS, INTERVAL) while leaving STATUS / PINS / DUMP
    // available in RUN mode.
    {"SYSTEM", cmd_system, "System configuration", false},
    {"SAVE", cmd_save, "Save configuration", true},
    {"LOAD", cmd_load, "Load configuration", true},
#if SUPPORTS_JSON_IMPORT_STREAM
    {"JSON", cmd_json, "Import JSON configuration over serial", true},
#endif
    {"REBOOT", cmd_reboot, "", true},  // Undocumented alias for SYSTEM REBOOT
    // BUS / SCAN: configModeOnly=false so their read-only JSON forms (BUS CAN
    // JSON, SCAN STATUS|RESULTS JSON) answer in RUN and never bump
    // configGeneration -- the app re-reads the whole dump on every bump
    // (#510). Every mutating form is still gated: BUS by its subcommand
    // table's runModeAllowed=false, SCAN inline in cmd_scan().
    {"BUS", cmd_bus, "Configure I2C/SPI/CAN buses", false},
    {"LOG", cmd_log, "Configure log levels and tags", false},
    {"AT", cmd_at, "Send raw AT command to a serial port", false},

#if INCLUDE_RELAY_OUTPUT
    {"RELAY", cmd_relay, "Deprecated - use RULE / ACTION", false},  // prints a migration pointer; relays are now control-engine Actions
#endif
#if INCLUDE_RULES_ENGINE
    {"RULE", cmd_rule, "Configure control-engine rules", false},  // gated per-subcommand: RUN allows LIST/<id>; ADD/DEL/RESET config-only
    {"ACTION", cmd_action, "Configure control-engine actions", false},  // gated per-subcommand: RUN allows LIST/<id>/ON/OFF/AUTO; ADD/DEL/RULE config-only
    {"ALARM", cmd_alarm, "Acknowledge alarms", false},  // gated per-subcommand: RUN allows STATUS/SILENCE; DURATION/SCOPE config-only
#endif
#if INCLUDE_TEST_MODE
    {"TEST", cmd_test, "Test mode control", false},
#endif
#if INCLUDE_CAN
    {"SCAN", cmd_scan, "Scan CAN bus for PIDs", false},
#endif
    // NOTE: the former top-level SELFTEST command is retired; its dispatch-table
    // validation is now `SYSTEM DIAG INTEGRITY` (see runDiagIntegrity / cmd_system.cpp).
};

const uint8_t NUM_COMMANDS = sizeof(COMMANDS) / sizeof(Command);

// Dispatch routing (dispatchCommand / dispatchSubcommand) lives in
// command_dispatch.cpp so the routing mechanism is unit-testable on the host
// without the heavy message subsystem. That TU reaches user-facing error
// output through the command_dispatch.h reporter seam, defined here:

void dispatchReportUnknownCommand(const char* name) {
    msg.control.print(F("ERROR: Unknown command '"));
    msg.control.print(name);
    msg.control.println(F("'"));
    msg.control.println(F("  Type HELP for available commands"));
}

void dispatchReportConfigRequired(const char* cmdUpper) {
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.print  (F("  ERROR: "));
    msg.control.print  (cmdUpper);
    msg.control.println(F(" requires CONFIG mode"));
    msg.control.println(F("  Type CONFIG to enter configuration mode"));
    msg.control.println(F("========================================"));
    msg.control.println();
}

void dispatchReportUnknownSubcommand(const char* commandName, const char* token) {
    msg.control.print(F("ERROR: Unknown "));
    msg.control.print(commandName);
    msg.control.print(F(" subcommand '"));
    msg.control.print(token);
    msg.control.println(F("'"));
}

void dispatchReportSubcmdConfigRequired(const char* commandName, const char* token) {
    msg.control.print(F("ERROR: "));
    msg.control.print(commandName);
    msg.control.print(' ');
    msg.control.print(token);
    msg.control.println(F(" requires CONFIG mode"));
    msg.control.println(F("  Type CONFIG to enter configuration mode"));
}

#if INCLUDE_SELFTEST
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

// Dispatch-table integrity check, surfaced as `SYSTEM DIAG INTEGRITY`. Walks
// every dispatch table and asserts invariants — catches PROGMEM-access
// regressions on AVR (where a missing pgm_read_ptr returns garbage) the moment
// it's run after flashing. Returns the total failing-entry count; each failure
// prints its own diagnostic line via validateDispatchEntry. The DIAG handler
// (cmd_system.cpp) prints the section header and pass/fail verdict.
int runDiagIntegrity() {
    int failures = 0;
    failures += selftestCommandsTable();
    failures += selftestSetTable();
    failures += selftestBusTable();
    failures += selftestSystemTable();
    return failures;
}
#endif // INCLUDE_SELFTEST

//=============================================================================
// Command Handler Implementations
//=============================================================================

int cmd_help(int argc, const char* const* argv) {
    if (argc == 1) {
        // No arguments - show overview
        printHelpOverview();
    } else if (argc == 2) {
        // Category argument provided
        if (streq_P(argv[1], PSTR("QUICK"))) {
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
    bool asJson = (argc >= 3 && streq_P(argv[argc - 1], PSTR("JSON")));

// EMIT_JSON(call): on supporting platforms, wraps call with blank-line padding.
// On non-supporting platforms, emits an error and returns — and the call expression
// is absent from the macro expansion so the symbols are never referenced.
#if SUPPORTS_JSON_EXPORT
#define EMIT_JSON(call) do { msg.control.println(); call; msg.control.println(); } while(0)
#else
#define EMIT_JSON(call) do { msg.control.println(F("ERROR: JSON export not supported on this platform")); return 1; } while(0)
#endif

    if (streq_P(argv[1], PSTR("INPUTS"))) {
        listAllInputs();
    } else if (streq_P(argv[1], PSTR("APPLICATIONS"))) {
        if (asJson) { EMIT_JSON(writeApplicationsJson(msg.control)); }
        else { listApplicationPresets(); }
    } else if (streq_P(argv[1], PSTR("SENSORS"))) {
        if (asJson) {
            // LIST SENSORS JSON  or  LIST SENSORS <category> JSON
            const char* filter = (argc == 4) ? argv[2] : nullptr;
            EMIT_JSON(writeSensorsJson(msg.control, filter));
        } else {
            // LIST SENSORS [category|filter]
            const char* filter = (argc >= 3) ? argv[2] : NULL;
            listSensors(filter);
        }
    } else if (streq_P(argv[1], PSTR("OUTPUTS"))) {
        if (asJson) { EMIT_JSON(writeOutputsJson(msg.control)); }
        else { listOutputModules(); }
    } else if (streq_P(argv[1], PSTR("UNITS"))) {
        if (asJson) { EMIT_JSON(writeUnitsJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST UNITS JSON")); }
    } else if (streq_P(argv[1], PSTR("CATEGORIES"))) {
        if (asJson) { EMIT_JSON(writeCategoriesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST CATEGORIES JSON")); }
    } else if (streq_P(argv[1], PSTR("PIDS"))) {
        if (asJson) { EMIT_JSON(writePidsJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST PIDS JSON")); }
    } else if (streq_P(argv[1], PSTR("MEASUREMENT_TYPES"))) {
        if (asJson) { EMIT_JSON(writeMeasurementTypesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST MEASUREMENT_TYPES JSON")); }
    } else if (streq_P(argv[1], PSTR("CALIBRATION_TYPES"))) {
        if (asJson) { EMIT_JSON(writeCalibrationTypesJson(msg.control)); }
        else { msg.control.println(F("JSON-only subcommand. Usage: LIST CALIBRATION_TYPES JSON")); }
    } else if (streq_P(argv[1], PSTR("TRANSPORTS"))) {
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
    msg.control.print(F("  "));
    printResetReason(msg.control, true);
#if defined(ARDUINO_ARCH_STM32)
    msg.control.print(F("  "));
    ramStatsPrint(msg.control);
#endif
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

// Persist the whole RAM config to EEPROM. Shared by `SAVE` / `SAVE EEPROM`
// and the one-shot `CLEAR <pin> SAVE` (#320) so they can't drift apart.
// Returns false if a flash-backed store failed to write; the batch has
// already printed why.
static bool saveConfigToEeprom() {
    msg.control.println(F("Saving configuration to EEPROM..."));
    {
        EepromBatch batch;   // one flash write for both, on flash-backed stores
        saveInputConfig();
        saveSystemConfig();
    }
    obdQuery_buildLookupTable();  // Rebuild PID table — inputs may have changed
    if (eepromCommitFailed()) return false;
    msg.control.println(F("Configuration saved"));
    return true;
}

int cmd_save(int argc, const char* const* argv) {
    // Case 1: SAVE (bare) → EEPROM (backward compatible)
    if (argc == 1) {
        return saveConfigToEeprom() ? 0 : 1;
    }

    // Case 2: SAVE EEPROM (explicit)
    if (argc == 2 && streq_P(argv[1], PSTR("EEPROM"))) {
        return saveConfigToEeprom() ? 0 : 1;
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

// Shared EEPROM-load path for `LOAD` and `LOAD EEPROM`. Both loaders are
// validate-before-mutate (a failed load leaves the prior in-RAM config and any
// driven outputs untouched), so we re-arm the control engine and report success
// ONLY when the load actually applied — never claim "loaded" on a blank/corrupt
// EEPROM, and never reset the running engine on a no-op LOAD (review fixes).
static int loadConfigFromEEPROM() {
    msg.control.println(F("Loading configuration from EEPROM..."));
    bool inputsOk = loadInputConfig();
    bool sysOk    = loadSystemConfig();

#if INCLUDE_RULES_ENGINE
    if (inputsOk) {
        // Re-arm from the freshly-loaded tables: rule contexts, then action pin
        // states + registry (mirrors the JSON import path, #296 B3). Gated on a
        // successful input/control load so a failed LOAD can't reset the engine.
        rulesManagerInit(millis());
        rulesActionsInit();
        rulesSyncActionPins();
    }
#endif

    if (inputsOk && sysOk) {
        msg.control.println(F("Configuration loaded"));
        return 0;
    }
    if (!inputsOk && !sysOk) {
        msg.control.println(F("ERROR: No valid configuration in EEPROM — nothing changed (run SAVE first)"));
        return 1;
    }
    msg.control.print(F("WARNING: partial load — "));
    msg.control.println(inputsOk ? F("system config missing/corrupt (inputs+rules loaded)")
                                 : F("input/control config missing/corrupt (system config loaded)"));
    return 1;
}

int cmd_load(int argc, const char* const* argv) {
    // Case 1: LOAD (bare) → EEPROM (backward compatible)
    if (argc == 1) {
        return loadConfigFromEEPROM();
    }

    // Case 2: LOAD EEPROM (explicit)
    if (argc == 2 && streq_P(argv[1], PSTR("EEPROM"))) {
        return loadConfigFromEEPROM();
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
            printSaveReminder();
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
    if (argc < 2 || !streq_P(argv[1], PSTR("IMPORT"))) {
        msg.control.println(F("Usage: JSON IMPORT [<byte-count>]"));
        msg.control.println(F("  No count: paste JSON, then type 'END JSON' on its own line"));
        msg.control.println(F("  With count: stream exactly <byte-count> raw bytes (no sentinel)"));
        return 1;
    }
    if (argc >= 3) {
        // Byte-counted mode: a programmatic sender (mobile app, scripts) that
        // already knows its payload length. Validated up front so an
        // oversized count fails immediately instead of partway through a
        // multi-second transfer.
        long n = strtol(argv[2], nullptr, 10);
        if (n <= 0 || (unsigned long)n > (unsigned long)(JSON_IMPORT_MAX_BYTES - 1)) {
            msg.control.print(F("ERROR: byte count must be 1-"));
            msg.control.println(JSON_IMPORT_MAX_BYTES - 1);
            return 1;
        }
        if (!serialConfig_beginJsonImport((size_t)n)) {
            msg.control.println(F("ERROR: JSON import already in progress"));
            return 1;
        }
        return 0;
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


int cmd_clear(int argc, const char* const* argv) {
    // Accepted shapes (a trailing SAVE persists in the same command, #320):
    //   CLEAR <pin> [SAVE]
    //   CLEAR <pin> CALIBRATION [SAVE]
    // SAVE writes the WHOLE RAM config, exactly like a following `SAVE` —
    // not just this clear. Anything else is rejected rather than falling
    // through to a full clear, so a typo can't wipe an input.
    bool calibrationOnly = false;
    bool save = false;
    bool argsOk = argc >= 2 && argc <= 4;
    for (int i = 2; argsOk && i < argc; i++) {
        if (i == 2 && streq_P(argv[i], PSTR("CALIBRATION"))) {
            calibrationOnly = true;
        } else if (i == argc - 1 && streq_P(argv[i], PSTR("SAVE"))) {
            save = true;
        } else {
            argsOk = false;
        }
    }
    if (!argsOk) {
        if (argc < 2) {
            msg.control.println(F("ERROR: CLEAR requires a pin"));
        } else {
            msg.control.println(F("ERROR: Invalid CLEAR arguments"));
        }
        msg.control.println(F("  Usage: CLEAR <pin> [SAVE]"));
        msg.control.println(F("         CLEAR <pin> CALIBRATION [SAVE]"));
        msg.control.println(F("  SAVE persists the whole config, not just this clear"));
        return 1;
    }

    bool valid;
    uint8_t pin = parsePin(argv[1], &valid);
    if (!valid) return 1;

    // CLEAR <pin> CALIBRATION — drop custom calibration only.
    if (calibrationOnly) {
        if (!clearInputCalibration(pin)) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }
        msg.control.print(F("Cleared custom calibration for pin "));
        msg.control.println(argv[1]);
        msg.control.println(F("Using preset calibration from sensor library"));
        if (save) {
            return saveConfigToEeprom() ? 0 : 1;
        }
        printSaveReminder();
        return 0;
    }

    clearInput(pin);
    msg.control.print(F("Input "));
    msg.control.print(argv[1]);
    msg.control.println(F(" cleared"));

#if INCLUDE_RULES_ENGINE
    // Removing the input without its Rules used to leave them orphaned: a Rule
    // sourced from a pin with no input can never assert, renders as a nameless
    // `pinN` row, and (pre-#345) counted as a FAULTED source — so one forgotten
    // orphan could hold a FAILSAFE ON relay energized forever (#345). Take the
    // Rules with the input, through the same referential-integrity path as
    // RULE DEL, and say exactly what went.
    uint8_t releasedActions = 0;
    const uint8_t removedRules = rulesRemoveRulesForInput(pin, &releasedActions);
    if (removedRules) {
        msg.control.print(F("  Removed "));
        msg.control.print(removedRules);
        msg.control.println(F(" rule(s) sourced from this input"));
        if (releasedActions) {
            msg.control.print(F("  NOTE: released "));
            msg.control.print(releasedActions);
            msg.control.println(F(" driven action(s) that followed them; they are now OFF"));
        }
    }
#endif

    if (save) {
        return saveConfigToEeprom() ? 0 : 1;
    }
    printSaveReminder();
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
        if (streq_P(argv[2], PSTR("ALARM"))) {
#if INCLUDE_RULES_ENGINE
            printInputAlarmInfo(pin);
#else
            msg.control.println(F("ERROR: alarms are not enabled in this build"));
#endif
        } else if (streq_P(argv[2], PSTR("OUTPUT"))) {
            printInputOutputInfo(pin);
        } else if (streq_P(argv[2], PSTR("CALIBRATION"))) {
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

// FORMAT is SD-only, so it disappears from the usage text on a board that
// vetoes SD logging (mega2560) rather than being advertised and then rejected.
#if INCLUDE_SD_LOGGING
#define OUTPUT_USAGE_FORMAT " | FORMAT <LONG|WIDE>"
#define OUTPUT_VALID_FORMAT "|FORMAT"
#else
#define OUTPUT_USAGE_FORMAT ""
#define OUTPUT_VALID_FORMAT ""
#endif

int cmd_output(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: OUTPUT requires a subcommand"));
        msg.control.println(F("  Usage: OUTPUT STATUS | <name> ENABLE | DISABLE | INTERVAL <ms>" OUTPUT_USAGE_FORMAT));
        return 1;
    }

    if (streq_P(argv[1], PSTR("STATUS"))) {
        listOutputs();
        return 0;
    }

    // All other subcommands require a name
    if (argc < 3) {
        msg.control.println(F("ERROR: Subcommand requires an output name"));
        msg.control.println(F("  Usage: OUTPUT <name> ENABLE | DISABLE | INTERVAL <ms>" OUTPUT_USAGE_FORMAT));
        return 1;
    }

    const char* outputName = argv[1];
    const char* subcommand = argv[2];

    if (streq_P(subcommand, PSTR("ENABLE"))) {
        if (setOutputEnabled(outputName, true)) {
            msg.control.print(outputName);
            msg.control.println(F(" enabled"));
            printSaveReminder();
        } else {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
    } else if (streq_P(subcommand, PSTR("DISABLE"))) {
        if (setOutputEnabled(outputName, false)) {
            msg.control.print(outputName);
            msg.control.println(F(" disabled"));
            printSaveReminder();
        } else {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
#if INCLUDE_SD_LOGGING
    } else if (streq_P(subcommand, PSTR("FORMAT"))) {
        // SD-only: the other outputs have no file layout to choose.
        int outputIndex = getOutputIndexByName(outputName);
        if (outputIndex < 0) {
            msg.control.print(F("ERROR: Unknown output '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
        if (outputIndex != OUTPUT_SD) {
            msg.control.print(F("ERROR: FORMAT applies to SD_Log only, not '"));
            msg.control.print(outputName);
            msg.control.println(F("'"));
            return 1;
        }
        if (argc < 4) {
            msg.control.println(F("ERROR: FORMAT requires LONG or WIDE"));
            msg.control.println(F("  WIDE: one row per sample, a column per input"));
            msg.control.println(F("  LONG: one row per reading (Time,Sensor,Value,Units)"));
            return 1;
        }
        SdLogFormat format;
        if (!parseSdLogFormat(argv[3], format)) {
            msg.control.print(F("ERROR: Unknown format '"));
            msg.control.print(argv[3]);
            msg.control.println(F("' (expected LONG or WIDE)"));
            return 1;
        }
        systemConfig.sdLogFormat = (uint8_t)format;
        msg.control.print(F("SD log format set to "));
        msg.control.println(sdLogFormatName(systemConfig.sdLogFormat));
        printSaveReminder();
#endif
    } else if (streq_P(subcommand, PSTR("INTERVAL"))) {
        if (argc < 4) {
            msg.control.println(F("ERROR: INTERVAL requires a time in ms"));
            return 1;
        }
        if (streq_P(outputName, PSTR("CAN"))) {
            // A responder: nothing to pace since the 0x7E8 broadcast went (#528)
            msg.control.println(F("ERROR: CAN output answers OBD-II requests as they arrive; it has no interval"));
            return 1;
        }
        uint16_t interval = atoi(argv[3]);
        if (setOutputInterval(outputName, interval)) {
            msg.control.print(outputName);
            msg.control.print(F(" interval set to "));
            msg.control.print(interval);
            msg.control.println(F("ms"));
            printSaveReminder();
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
        msg.control.println(F("Valid commands: STATUS, or <module> ENABLE|DISABLE|INTERVAL" OUTPUT_VALID_FORMAT));
        return 1;
    }

    return 0;
}

#if INCLUDE_OLED
#define DISPLAY_VALID_TYPES F("  Valid types: LCD, OLED, NONE")
#else
#define DISPLAY_VALID_TYPES F("  Valid types: LCD, NONE")
#endif

int cmd_display(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: DISPLAY requires a subcommand"));
        msg.control.println(F("  Usage: DISPLAY STATUS | ENABLE | DISABLE | TYPE <type> | ADDRESS <hex> | INTERVAL <ms>"));
        return 1;
    }

    const char* subcommand = argv[1];

    // DISPLAY STATUS
    if (streq_P(subcommand, PSTR("STATUS"))) {
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
        printDisplayPanel();
        msg.control.print(F("Update Interval: "));
        msg.control.print(systemConfig.lcdUpdateInterval);
        msg.control.println(F("ms"));
#if INCLUDE_DISPLAY
        // The grid holds 8; more pages through groups of 8 (#480).
        uint8_t shown = 0;
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (lcdShowsInput(inputs[i].pin != 0xFF, inputs[i].flags.isEnabled, inputs[i].flags.display, isInConfigMode())) shown++;
        }
        msg.control.print(F("Inputs shown: "));
        msg.control.print(shown);
        if (shown > LCD_CELLS) {
            msg.control.print(F(" (pages of 8, every "));
            msg.control.print(LCD_PAGE_INTERVAL_MS / 1000);
            msg.control.print(F(" s)"));
        }
        msg.control.println();
#endif
        return 0;
    }

    // DISPLAY ENABLE
    if (streq_P(subcommand, PSTR("ENABLE"))) {
        systemConfig.displayEnabled = 1;
        setDisplayRuntime(true);
        msg.control.println(F("Display enabled"));
        printSaveReminder();
        return 0;
    }

    // DISPLAY DISABLE
    if (streq_P(subcommand, PSTR("DISABLE"))) {
        systemConfig.displayEnabled = 0;
        setDisplayRuntime(false);
        msg.control.println(F("Display disabled"));
        printSaveReminder();
        return 0;
    }

    // DISPLAY TYPE <LCD|OLED|NONE>  (OLED where the board has the driver, #58)
    if (streq_P(subcommand, PSTR("TYPE"))) {
        if (argc < 3) {
            msg.control.println(F("ERROR: TYPE requires a display type"));
            msg.control.println(DISPLAY_VALID_TYPES);
            return 1;
        }
        const char* typeStr = argv[2];
        if (streq_P(typeStr, PSTR("LCD"))) {
            if (displayI2CPinsConflict()) return 1;

            systemConfig.displayType = DISPLAY_LCD;
            msg.control.println(F("Display type set to LCD"));
            msg.control.println(F("Note: Takes effect on next reboot"));
        } else if (streq_P(typeStr, PSTR("OLED"))) {
#if INCLUDE_OLED
            if (displayI2CPinsConflict()) return 1;   // the OLED takes SDA/SCL at boot too

            systemConfig.displayType = DISPLAY_OLED;
            msg.control.println(F("Display type set to OLED (128x64 SH1107, I2C 0x3C)"));
            msg.control.println(F("Note: Takes effect on next reboot"));
#else
            msg.control.println(F("ERROR: this board's firmware has no OLED driver"));
            return 1;
#endif
        } else if (streq_P(typeStr, PSTR("NONE"))) {
            systemConfig.displayType = DISPLAY_NONE;
            msg.control.println(F("Display type set to NONE"));
        } else {
            msg.control.print(F("ERROR: Invalid display type '"));
            msg.control.print(typeStr);
            msg.control.println(F("'"));
            msg.control.println(DISPLAY_VALID_TYPES);
            return 1;
        }
        return 0;
    }

    // DISPLAY ADDRESS <hex>
    if (streq_P(subcommand, PSTR("ADDRESS"))) {
        if (argc < 3) {
            msg.control.println(F("ERROR: ADDRESS requires an I2C address"));
            msg.control.println(F("  Usage: DISPLAY ADDRESS <hex>"));
            return 1;
        }
        const char* addrStr = argv[2];
        char* endptr;
        long addr = strtol(addrStr, &endptr, 16);
        // An LCD backpack is a PCF8574 or PCF8574A: nothing else is an LCD
        // address (#590). The trailing-garbage check rejects "0x3G" etc.
        if (endptr == addrStr || *endptr != '\0' || !isValidLcdI2CAddress(addr)) {
            msg.control.print(F("ERROR: Invalid LCD I2C address '"));
            msg.control.print(addrStr);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid: 0x20-0x27 (PCF8574) or 0x38-0x3F (PCF8574A)"));
            return 1;
        }
        systemConfig.lcdI2CAddress = (uint8_t)addr;
        msg.control.print(F("LCD I2C address set to 0x"));
        msg.control.println(systemConfig.lcdI2CAddress, HEX);
        msg.control.println(F("Note: Takes effect on next reboot"));
        printSaveReminder();
        return 0;
    }

    // DISPLAY INTERVAL <ms>
    if (streq_P(subcommand, PSTR("INTERVAL"))) {
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

    if (streq_P(argv[1], PSTR("STATUS"))) {
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
    if (argc >= 4 && !streq_P(argv[3], PSTR("SECONDARY"))) {
        msg.control.print(F("ERROR: Unknown option '"));
        msg.control.print(argv[3]);
        msg.control.println(F("' (did you mean SECONDARY?)"));
        return 1;
    }
    bool isSecondary = (argc >= 4);

    // Conflict: refuse to assign a serial port that is owned by ELM327
#if INCLUDE_ELM327
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
        printSaveReminder();
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

#if INCLUDE_RELAY_OUTPUT
int cmd_relay(int argc, const char* const* argv) {
    // RELAY is deprecated: a relay is now a control-engine Action of type RELAY.
    (void)argc; (void)argv;
    msg.control.println(F("RELAY is deprecated - relays are now control-engine Actions."));
    msg.control.println(F("  Threshold:  RULE ADD <input> ABOVE|BELOW <limit> [HYST <w>]"));
    msg.control.println(F("  Output:     ACTION ADD RELAY <pin> [FAILSAFE ON|OFF] [INVERT]"));
    msg.control.println(F("  Link/drive: ACTION <id> RULE <ruleId> ;  ACTION <id> ON|OFF|AUTO"));
    msg.control.println(F("  Status:     RULE LIST ;  ACTION LIST"));
    return 1;
}
#endif

#if INCLUDE_TEST_MODE
int cmd_test(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: TEST requires a subcommand"));
        msg.control.println(F("  Usage: TEST LIST"));
        msg.control.println(F("  Usage: TEST <0-N>"));
        msg.control.println(F("  Usage: TEST STOP"));
        msg.control.println(F("  Usage: TEST STATUS"));
        msg.control.println(F("  Usage: TEST INJECT <pin> <value>"));
        msg.control.println(F("  Usage: TEST CLEAR"));
        msg.control.println(F("  Usage: TEST OUTPUT (CONFIG mode)"));
        return 1;
    }

    const char* subcommand = argv[1];

    // TEST LIST
    if (streq_P(subcommand, PSTR("LIST"))) {
        listTestScenarios();
        return 0;
    }

    // TEST STOP
    if (streq_P(subcommand, PSTR("STOP"))) {
        if (!isTestModeActive()) {
            msg.control.println(F("No test scenario is currently running"));
        } else {
            stopTestMode();
            msg.control.println(F("Test mode stopped"));
        }
        return 0;
    }

    // TEST STATUS
    if (streq_P(subcommand, PSTR("STATUS"))) {
        if (!isTestModeActive()) {
            msg.control.println(F("Test mode: INACTIVE"));
        } else {
            msg.control.println(F("Test mode: ACTIVE"));
            msg.control.println(F("  Use TEST LIST to see all scenarios"));
        }
        return 0;
    }

    // TEST INJECT <pin> <value> — hold one input at a fixed value (deterministic
    // bench testing, e.g. drive an input past its alarm threshold).
    if (streq_P(subcommand, PSTR("INJECT"))) {
        if (isTestModeActive()) {
            msg.control.println(F("ERROR: a scenario is running; TEST STOP first"));
            return 1;
        }
        if (argc < 4) {
            msg.control.println(F("ERROR: usage: TEST INJECT <pin> <value>"));
            return 1;
        }
        bool pinOk = false;
        uint8_t pin = parsePin(argv[2], &pinOk);
        if (!pinOk) {
            msg.control.print(F("ERROR: invalid pin '"));
            msg.control.print(argv[2]);
            msg.control.println(F("'"));
            return 1;
        }
        Input* in = requireInput(pin);   // prints standard error if unconfigured
        if (!in) return 1;
        float v = atof(argv[3]);
        if (!injectInputValue(getInputIndex(pin), v)) {
            msg.control.println(F("ERROR: could not inject (input not enabled?)"));
            return 1;
        }
        msg.control.print(F("Injecting "));
        msg.control.print(in->abbrName);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.print(F(") = "));
        msg.control.println(v, 2);
        msg.control.println(F("  (use TEST CLEAR to restore live readings)"));
        return 0;
    }

    // TEST CLEAR — restore all injected inputs to their live readings
    if (streq_P(subcommand, PSTR("CLEAR"))) {
        if (!anyInjectedInputs()) {
            msg.control.println(F("No inputs are currently injected"));
        } else {
            clearInjectedInputs();
            msg.control.println(F("Cleared injected inputs; live readings restored"));
        }
        return 0;
    }

    // TEST OUTPUT — pulse each output in turn so an operator can confirm wiring
    // (the scan-tool "output test" pattern). CONFIG mode only.
    //
    // Feeds the watchdog between steps: on Teensy 4.x the WDT cannot actually be
    // disabled (CONFIG mode only nominally "disables" it), so a multi-second
    // blocking sequence that never feeds it trips the 2s timeout and resets the
    // MCU. That was #239 — the fault was the un-fed watchdog, not tone().
    if (streq_P(subcommand, PSTR("OUTPUT"))) {
        if (isInRunMode()) {
            msg.control.println(F("ERROR: TEST OUTPUT requires CONFIG mode"));
            msg.control.println(F("  Type CONFIG first (actuating outputs in RUN could interfere)"));
            return 1;
        }
        msg.control.println(F("=== Output actuation test (watch / listen to confirm) ==="));
        bool any = false;
        (void)any;
#if INCLUDE_LED
        any = true;
        msg.control.println(F("RGB LED -> RED"));
        rgbLedSolid(RGB_RED, PRIORITY_ALARM);   updateRGBLed(); watchdogReset(); delay(600);
        msg.control.println(F("RGB LED -> GREEN"));
        rgbLedSolid(RGB_GREEN, PRIORITY_ALARM); updateRGBLed(); watchdogReset(); delay(600);
        msg.control.println(F("RGB LED -> BLUE"));
        rgbLedSolid(RGB_BLUE, PRIORITY_ALARM);  updateRGBLed(); watchdogReset(); delay(600);
        rgbLedRelease(PRIORITY_ALARM);          updateRGBLed();
#endif
#if INCLUDE_ALARMS && defined(BUZZER_PIN)
        any = true;
        msg.control.println(F("Buzzer -> chirp"));
        tone(BUZZER_PIN, 700); watchdogReset(); delay(250); platformNoTone(BUZZER_PIN);
#endif
#if INCLUDE_RULES_ENGINE
        for (int i = 0; i < MAX_ACTIONS; i++) {
            const Action& a = actions[i];
            if (a.id == RULES_ID_NONE || a.type != ACT_RELAY) continue;
            any = true;
            msg.control.print(F("Action #")); msg.control.print(a.id);
            msg.control.print(F(" (pin ")); msg.control.print(a.target);
            msg.control.println(F(") -> ON"));
            pinMode(a.target, OUTPUT);
            digitalWrite(a.target, HIGH); watchdogReset(); delay(700);
            msg.control.print(F("Action #")); msg.control.print(a.id);
            msg.control.println(F(" -> OFF"));
            digitalWrite(a.target, LOW);
        }
#endif
        if (!any) msg.control.println(F("(no actuable outputs in this build)"));
        else      msg.control.println(F("Output test complete"));
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
    if (streq_P(subcmd, PSTR("STATUS"))) {
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
                    msg.control.println(reinterpret_cast<const __FlashStringHelper*>(tagName));
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
    if (streq_P(subcmd, PSTR("TAGS"))) {
        msg.control.println();
        msg.control.println(F("========================================"));
        msg.control.println(F("  Available Log Tags"));
        msg.control.println(F("========================================"));

        for (uint8_t i = 0; i < NUM_LOG_TAGS; i++) {
            const char* tagName = getTagName(i);
            if (tagName) {
                bool enabled = router.getLogFilter().isTagEnabled(i);
                msg.control.print(F("  "));
                msg.control.print(reinterpret_cast<const __FlashStringHelper*>(tagName));
                msg.control.print(F(": "));
                msg.control.println(enabled ? F("ENABLED") : F("DISABLED"));
            }
        }

        msg.control.println(F("========================================"));
        msg.control.println();
        return 0;
    }

    // LOG LEVEL <plane> <level> - set log level
    if (streq_P(subcmd, PSTR("LEVEL"))) {
        if (isInRunMode()) {
            msg.control.println(F("ERROR: LOG LEVEL requires CONFIG mode (mutates log filter)"));
            return 1;
        }
        configGenerationBump();   // logFilter is in SYSTEM DUMP JSON (#455)
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
        if (streq_P(planeName, PSTR("CONTROL"))) plane = PLANE_CONTROL;
        else if (streq_P(planeName, PSTR("DATA"))) plane = PLANE_DATA;
        else if (streq_P(planeName, PSTR("DEBUG"))) plane = PLANE_DEBUG;
        else {
            msg.control.print(F("ERROR: Unknown plane '"));
            msg.control.print(planeName);
            msg.control.println(F("'"));
            msg.control.println(F("  Valid planes: CONTROL, DATA, DEBUG"));
            return 1;
        }

        // Parse level name
        LogLevel level = router.getLogFilter().parseLevelName(argv[3]);
        // streq_P is case-insensitive, so PSTR("NONE") matches "none", "None", etc.
        // The pre-existing duplicate `!streq(argv[3], "none")` check that used to
        // sit on this line was redundant (same-case-insensitive compare twice).
        if (level == LOG_LEVEL_NONE && !streq_P(argv[3], PSTR("NONE"))) {
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
        printSaveReminder();
        return 0;
    }

    // LOG TAG <tag> <ENABLE|DISABLE> - enable/disable tag
    if (streq_P(subcmd, PSTR("TAG"))) {
        if (isInRunMode()) {
            msg.control.println(F("ERROR: LOG TAG requires CONFIG mode (mutates log filter)"));
            return 1;
        }
        configGenerationBump();   // logFilter is in SYSTEM DUMP JSON (#455)
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
        if (streq_P(stateName, PSTR("ENABLE"))) enable = true;
        else if (streq_P(stateName, PSTR("DISABLE"))) enable = false;
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

        if (streq_P(tagName, PSTR("ALL"))) {
            if (enable) {
                router.getLogFilter().enableAllTags();
                msg.control.println(F("✓ All tags enabled"));
            } else {
                router.getLogFilter().disableAllTags();
                msg.control.println(F("✓ All tags disabled"));
            }
            // Sync to systemConfig (so SAVE will persist it)
            router.syncConfig();
            printSaveReminder();
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
        msg.control.print(reinterpret_cast<const __FlashStringHelper*>(getTagName(tagId)));  // PROGMEM (log_tags.h)
        msg.control.print(F(" "));
        msg.control.println(enable ? F("enabled") : F("disabled"));
        printSaveReminder();
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

#if INCLUDE_CAN
#if SUPPORTS_JSON_EXPORT
// SCAN STATUS JSON / SCAN RESULTS JSON (#510): the scan's machine-readable
// face, readable in RUN. Framed like LIST ... JSON (blank line before, one
// line of JSON, newline).
static FLASHMEM void emitScanSupportedJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (const CanDiscState* d = getCANScanDiscovery()) {
        for (uint8_t e = 0; e < CAN_DISC_MAX_ECUS; e++) {
            if (!(d->ecuMask & (1 << e))) continue;
            canScanSupportedToJson((uint16_t)(0x7E8 + e), d->supported[e], arr.add<JsonObject>());
        }
    }
    msg.control.println();
    WatchdogKickingPrint out(msg.control);
    serializeJson(doc, out);
    msg.control.println();
}

static FLASHMEM void emitScanJson(bool results) {
    uint32_t now = millis();
    JsonDocument doc;
    if (results) {
        JsonArray arr = doc.to<JsonArray>();
        uint8_t count = 0;
        const CANScanResult* r = getCANScanResults(&count);
        for (uint8_t i = 0; i < count; i++) {
            CanScanEntrySnapshot e;
            e.canId = r[i].can_id;
            e.pid = r[i].pid;
            e.dataLength = r[i].data_length;
            e.frames = r[i].sample_count;
            e.ageMs = now - r[i].last_seen_ms;
            e.data = r[i].last_data;
            canScanEntryToJson(e, arr.add<JsonObject>());
        }
    } else {
        CanScanSnapshot s;
        uint8_t count = 0;
        getCANScanResults(&count);
        CANScanState st = getCANScanState();
        s.state = st == SCAN_LISTENING ? CAN_SCAN_JSON_LISTENING
                : st == SCAN_QUERYING  ? CAN_SCAN_JSON_QUERYING
                : st == SCAN_DONE      ? CAN_SCAN_JSON_DONE
                                       : CAN_SCAN_JSON_IDLE;
        s.scanId = getCANScanId();
        s.durationMs = getCANScanDuration();
        s.remainingMs = getCANScanRemainingMs(now);
        s.found = count;
        s.passive = true;
        s.ecus = 0;
        if (const CanDiscState* d = getCANScanDiscovery()) {
            s.passive = false;
            s.ecus = canDiscEcuCount(*d);
            s.found = 0;
            for (uint8_t e = 0; e < CAN_DISC_MAX_ECUS; e++) s.found += canDiscCountEcu(*d, e);
        }
        canScanStatusToJson(s, doc.to<JsonObject>());
    }
    msg.control.println();
    WatchdogKickingPrint out(msg.control);
    serializeJson(doc, out);
    msg.control.println();
}
#endif

int cmd_scan(int argc, const char* const* argv) {
    // Usage: SCAN CAN [duration_ms] [JSON]   (CONFIG)
    //        SCAN PIDS [JSON]                (CONFIG, #511)
    //        SCAN CANCEL                     (CONFIG)
    //        SCAN STATUS JSON | RESULTS JSON | SUPPORTED JSON (any mode, read-only)

    if (argc < 2) {
        msg.control.println(F("SCAN - Scan CAN bus for active PIDs"));
        msg.control.println(F(""));
        msg.control.println(F("Usage:"));
        msg.control.println(F("  SCAN CAN [duration] [JSON] - Scan CAN bus (default 10000ms)"));
        msg.control.println(F("  SCAN PIDS [JSON]     - Ask the ECUs which PIDs they support"));
        msg.control.println(F("  SCAN CANCEL          - Cancel/clear scan results"));
        msg.control.println(F("  SCAN STATUS JSON     - Scan state and progress"));
        msg.control.println(F("  SCAN RESULTS JSON    - What SCAN CAN found"));
        msg.control.println(F("  SCAN SUPPORTED JSON  - What SCAN PIDS found, per ECU"));
        msg.control.println(F(""));
        msg.control.println(F("Examples:"));
        msg.control.println(F("  SCAN CAN             - Scan for 10 seconds"));
        msg.control.println(F("  SCAN CAN 15000       - Scan for 15 seconds"));
        msg.control.println(F("  SCAN CAN 15000 JSON  - Same, without the text table at the end"));
        msg.control.println(F("  SCAN PIDS            - List the PIDs this car's ECUs support"));
        msg.control.println(F("  SCAN CANCEL          - Clear results"));
        return 0;
    }

    const char* subcmd = argv[1];

    // Read-only JSON forms: any mode, no configGeneration bump (#510)
    if (streq_P(subcmd, PSTR("STATUS")) || streq_P(subcmd, PSTR("RESULTS")) ||
        streq_P(subcmd, PSTR("SUPPORTED"))) {
        if (argc != 3 || !streq_P(argv[2], PSTR("JSON"))) {
            msg.control.println(F("ERROR: Usage: SCAN STATUS JSON | SCAN RESULTS JSON | SCAN SUPPORTED JSON"));
            return 1;
        }
#if SUPPORTS_JSON_EXPORT
        if (streq_P(subcmd, PSTR("SUPPORTED"))) emitScanSupportedJson();
        else emitScanJson(streq_P(subcmd, PSTR("RESULTS")));
        return 0;
#else
        msg.control.println(F("ERROR: JSON export not supported on this platform"));
        return 1;
#endif
    }

    // Starting or clearing a scan is CONFIG-only (SCAN is not configModeOnly
    // at the top level so the forms above answer in RUN). A scan changes no
    // configuration, so it does not bump configGeneration.
    if (isInRunMode()) {
        dispatchReportConfigRequired("SCAN");
        return 1;
    }

    if (streq_P(subcmd, PSTR("CAN"))) {
        // SCAN CAN [duration] [JSON]
        bool quiet = (argc >= 3 && streq_P(argv[argc - 1], PSTR("JSON")));
        uint8_t nArgs = quiet ? argc - 1 : argc;
        if (nArgs > 3) {
            msg.control.println(F("ERROR: Usage: SCAN CAN [duration] [JSON]"));
            return 1;
        }

        // Get duration (default 10 seconds)
        uint16_t duration = 10000;
        if (nArgs >= 3) {
            long ms = atol(argv[2]);
            if (ms < 1000) {
                msg.control.println(F("ERROR: Duration must be at least 1000ms"));
                return 1;
            }
            if (ms > 60000) {
                msg.control.println(F("ERROR: Duration must be at most 60000ms"));
                return 1;
            }
            duration = (uint16_t)ms;
        }

        // Check if CAN input is enabled
        if (systemConfig.buses.can_input_mode == CAN_INPUT_OFF) {
            msg.control.println(F("ERROR: CAN input not enabled"));
            msg.control.println(F("  Use 'BUS CAN INPUT CAN1 ENABLE' or 'BUS CAN INPUT CAN1 LISTEN' first"));
            return 1;
        }
        // ...and actually running: BUS CAN INPUT takes effect on reboot, so
        // an input enabled since boot would scan its whole window and find
        // nothing, with no error (#510 review)
        if (!canInputIsRunning()) {
            msg.control.println(F("ERROR: CAN input is not running"));
            msg.control.println(F("  SAVE and reboot after 'BUS CAN INPUT ...' to start it"));
            return 1;
        }

        // Start scan
        startCANScan(duration, quiet);
        return 0;
    }

    if (streq_P(subcmd, PSTR("PIDS"))) {
        // SCAN PIDS [JSON] (#511): transmits, so NORMAL input only
        bool quiet = (argc == 3 && streq_P(argv[2], PSTR("JSON")));
        if (argc > 3 || (argc == 3 && !quiet)) {
            msg.control.println(F("ERROR: Usage: SCAN PIDS [JSON]"));
            return 1;
        }
#if SUPPORTS_JSON_EXPORT
        if (systemConfig.buses.can_input_mode != CAN_INPUT_NORMAL) {
            msg.control.println(F("ERROR: SCAN PIDS needs CAN input in ENABLE mode"));
            msg.control.println(F("  It sends requests to the ECU; LISTEN never transmits."));
            msg.control.println(F("  Use SCAN CAN to listen, or 'BUS CAN INPUT CAN1 ENABLE' first"));
            return 1;
        }
        if (!canInputRunsNormal()) {
            // Configured ENABLE but not running, or still in its boot mode
            msg.control.println(F("ERROR: CAN input is not running in ENABLE mode yet"));
            msg.control.println(F("  SAVE and reboot after 'BUS CAN INPUT ...' to apply it"));
            return 1;
        }
        startCANPidScan(quiet);
        return 0;
#else
        msg.control.println(F("ERROR: SCAN PIDS not supported on this platform"));
        return 1;
#endif
    }

    if (streq_P(subcmd, PSTR("CANCEL"))) {
        cancelCANScan();
        return 0;
    }

    // Unknown subcommand
    msg.control.print(F("ERROR: Unknown SCAN subcommand '"));
    msg.control.print(subcmd);
    msg.control.println(F("'"));
    msg.control.println(F("  Valid: CAN, PIDS, CANCEL, STATUS JSON, RESULTS JSON, SUPPORTED JSON"));
    return 1;
}
#endif // INCLUDE_CAN

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
    while (port->available()) { port->read(); watchdogReset(); }

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
        // A chatty peer keeps pushing `deadline` out (+100ms/byte); feed the WDT
        // so a long response can't trip the 2s watchdog (it can't be disabled on
        // Teensy 4.x, and AT runs in RUN mode where it's armed). See #243.
        watchdogReset();
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
