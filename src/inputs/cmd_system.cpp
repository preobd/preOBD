/*
 * cmd_system.cpp - SYSTEM command (status, units, intervals, reboot, reset)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "input_manager.h"
#include "input.h"
#include "serial_config.h"
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

// SYSTEM STATUS subcommand handler
static int system_status(int argc, const char* const* argv, int /*tokenIndex*/) {
    printSystemStatus();
    return 0;
}

// SYSTEM PINS subcommand handler
static int system_pins(int argc, const char* const* argv, int /*tokenIndex*/) {
    if (argc == 2) {
        printPinStatus();
        return 0;
    }
    // Specific pin query
    bool valid = false;
    uint8_t pin = parsePin(argv[2], &valid);
    if (valid) {
        printPinStatus(pin);
        return 0;
    }
    msg.control.println(F("ERROR: Invalid subcommand or pin"));
    return 1;
}

// SYSTEM DUMP subcommand handler
static int system_dump(int argc, const char* const* argv, int /*tokenIndex*/) {
    // SYSTEM DUMP REGISTRY JSON — export static firmware catalogs
    if (argc == 4 && streq(argv[2], "REGISTRY") && streq(argv[3], "JSON")) {
#if SUPPORTS_JSON_EXPORT
        msg.control.println();
        dumpRegistryToJson(msg.control);
        msg.control.println();
#else
        msg.control.println(F("ERROR: JSON export not supported on this platform"));
#endif
        return 0;
    }

    // SYSTEM DUMP JSON — export active user configuration
    if (argc == 3 && streq(argv[2], "JSON")) {
#if SUPPORTS_JSON_CONFIG
        msg.control.println();
        dumpConfigToJSON(msg.control);
        msg.control.println();
#else
        msg.control.println(F("ERROR: JSON export not supported on this platform"));
#endif
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

// SYSTEM SEA_LEVEL subcommand handler
static int system_sea_level(int argc, const char* const* argv, int /*tokenIndex*/) {
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

// SYSTEM UNITS subcommand handler
static int system_units(int argc, const char* const* argv, int /*tokenIndex*/) {
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

// SYSTEM INTERVAL subcommand handler
static int system_interval(int argc, const char* const* argv, int /*tokenIndex*/) {
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

// SYSTEM REBOOT subcommand handler
static int system_reboot(int argc, const char* const* argv, int /*tokenIndex*/) {
    msg.control.println(F("Rebooting system..."));
    platformReboot();
    return 0;
}

// SYSTEM RESET subcommand handler
static int system_reset(int argc, const char* const* argv, int /*tokenIndex*/) {
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


// SYSTEM subcommand tokens (PROGMEM)
static const char PSTR_SYS_STATUS[] PROGMEM = "STATUS";
static const char PSTR_SYS_PINS[] PROGMEM = "PINS";
static const char PSTR_SYS_DUMP[] PROGMEM = "DUMP";
static const char PSTR_SYS_SEA_LEVEL[] PROGMEM = "SEA_LEVEL";
static const char PSTR_SYS_UNITS[] PROGMEM = "UNITS";
static const char PSTR_SYS_INTERVAL[] PROGMEM = "INTERVAL";
static const char PSTR_SYS_REBOOT[] PROGMEM = "REBOOT";
static const char PSTR_SYS_RESET[] PROGMEM = "RESET";

static const Subcommand SYSTEM_SUBCOMMANDS[] PROGMEM = {
    { PSTR_SYS_STATUS, system_status },
    { PSTR_SYS_PINS, system_pins },
    { PSTR_SYS_DUMP, system_dump },
    { PSTR_SYS_SEA_LEVEL, system_sea_level },
    { PSTR_SYS_UNITS, system_units },
    { PSTR_SYS_INTERVAL, system_interval },
    { PSTR_SYS_REBOOT, system_reboot },
    { PSTR_SYS_RESET, system_reset },
};
static const uint8_t NUM_SYSTEM_SUBCOMMANDS = sizeof(SYSTEM_SUBCOMMANDS) / sizeof(Subcommand);

int cmd_system(int argc, const char* const* argv) {
    if (argc < 2) {
        msg.control.println(F("ERROR: SYSTEM requires a subcommand"));
        msg.control.println(F("  Usage: SYSTEM STATUS | DUMP | PINS | UNITS | SEA_LEVEL | INTERVAL | REBOOT | RESET"));
        return 1;
    }

    return dispatchSubcommand(SYSTEM_SUBCOMMANDS, NUM_SYSTEM_SUBCOMMANDS,
                              argv[1], "SYSTEM", argc, argv, 1);
}


#if ENABLE_SELFTEST
int selftestSystemTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_SYSTEM_SUBCOMMANDS; i++) {
        const char*        tok = (const char*)pgm_read_ptr(&SYSTEM_SUBCOMMANDS[i].token);
        SubcommandHandler  h   = (SubcommandHandler)pgm_read_ptr(&SYSTEM_SUBCOMMANDS[i].handler);
        failures += validateDispatchEntry(F("SYSTEM"), i, tok, (const void*)h, true);
    }
    return failures;
}
#endif
