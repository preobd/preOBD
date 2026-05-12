/*
 * cmd_set.cpp - SET command (configure inputs)
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

//=============================================================================
// SET command — leaves and dispatch
//
// cmd_set parses <pin> once, then dispatches on <field> to one of these leaf
// handlers. Pin is threaded through because parsePin() for the keywords "CAN"
// and "I2C" allocates virtual slots via internal counters and is not
// idempotent — the leaves cannot safely re-parse it.
//=============================================================================

typedef int (*SetFieldHandler)(uint8_t pin, int argc, const char* const* argv);
struct SetField {
    const char* token;
    SetFieldHandler handler;
};

// SET <pin> APPLICATION <application>
static int set_application(uint8_t pin, int argc, const char* const* argv) {
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
static int set_sensor(uint8_t pin, int argc, const char* const* argv) {
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
static int set_name(uint8_t pin, int argc, const char* const* argv) {
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
static int set_display_name(uint8_t pin, int argc, const char* const* argv) {
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

// SET <pin> DISPLAY ENABLE|DISABLE
static int set_display(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: DISPLAY requires ENABLE or DISABLE"));
        return 1;
    }
    if (streq(argv[3], "ENABLE")) {
        if (enableInputDisplay(pin, true)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.println(F(" display enabled"));
            return 0;
        }
        return 1;
    }
    if (streq(argv[3], "DISABLE")) {
        if (enableInputDisplay(pin, false)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.println(F(" display disabled"));
            return 0;
        }
        return 1;
    }
    msg.control.println(F("ERROR: DISPLAY requires ENABLE or DISABLE"));
    return 1;
}

// SET <pin> UNITS <units>
static int set_units(uint8_t pin, int argc, const char* const* argv) {
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

// SET <pin> ALARM ENABLE|DISABLE|WARMUP|PERSIST|<min> <max>
static int set_alarm(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: ALARM requires ENABLE, DISABLE, WARMUP, PERSIST, or <min> <max>"));
        return 1;
    }

    if (streq(argv[3], "ENABLE")) {
        if (enableInputAlarm(pin, true)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.println(F(" alarm enabled"));
            return 0;
        }
        return 1;
    }

    if (streq(argv[3], "DISABLE")) {
        if (enableInputAlarm(pin, false)) {
            msg.control.print(F("Input "));
            msg.control.print(argv[1]);
            msg.control.println(F(" alarm disabled"));
            return 0;
        }
        return 1;
    }

    if (streq(argv[3], "WARMUP")) {
        if (argc < 5) {
            msg.control.println(F("ERROR: ALARM WARMUP requires a time value in milliseconds"));
            return 1;
        }
        uint16_t value = atoi(argv[4]);
        if (value > 300000) {
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

    if (streq(argv[3], "PERSIST")) {
        if (argc < 5) {
            msg.control.println(F("ERROR: ALARM PERSIST requires a time value in milliseconds"));
            return 1;
        }
        uint16_t value = atoi(argv[4]);
        if (value > 60000) {
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

// SET <pin> DIVIDER <ratio>
// Voltage divider ratio at the ADC pin (V_at_pin / V_at_sensor).
static int set_divider(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: DIVIDER requires a ratio (e.g. 0.6 for 2.2k/3.3k)"));
        return 1;
    }
    float ratio = atof(argv[3]);
    if (ratio <= 0.0f || ratio > 1.0f) {
        msg.control.println(F("ERROR: Divider ratio must be > 0.0 and <= 1.0"));
        return 1;
    }
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }
    input->divider_ratio = ratio;
    msg.control.print(F("Input "));
    msg.control.print(argv[1]);
    msg.control.print(F(" divider ratio set to "));
    msg.control.println(ratio, 3);
    msg.control.println(F("  (use SAVE to persist)"));
    return 0;
}

// SET <pin> OUTPUT <target> ENABLE|DISABLE | ALL ENABLE|DISABLE | STATUS
static int set_output(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: OUTPUT requires a target"));
        msg.control.println(F("  Usage: SET <pin> OUTPUT <CAN|RealDash|Serial|SD_Log|ALL> <ENABLE|DISABLE>"));
        msg.control.println(F("         SET <pin> OUTPUT STATUS"));
        return 1;
    }

    if (streq(argv[3], "STATUS")) {
        printInputOutputInfo(pin);
        return 0;
    }

    if (argc < 5) {
        msg.control.println(F("ERROR: OUTPUT requires ENABLE or DISABLE"));
        return 1;
    }

    bool enable;
    if (streq(argv[4], "ENABLE")) {
        enable = true;
    } else if (streq(argv[4], "DISABLE")) {
        enable = false;
    } else {
        msg.control.println(F("ERROR: Expected ENABLE or DISABLE"));
        return 1;
    }

    if (streq(argv[3], "ALL")) {
        Input* input = getInputByPin(pin);
        if (!input) {
            msg.control.println(F("ERROR: Input not configured"));
            return 1;
        }
        input->outputMask = enable ? OUTPUT_MASK_ALL_DATA : 0x00;
        msg.control.print(F("Input "));
        msg.control.print(argv[1]);
        msg.control.print(F(" all data outputs "));
        msg.control.println(enable ? F("enabled") : F("disabled"));
        msg.control.println(F("  (use SAVE to persist)"));
        return 0;
    }

    uint8_t outputId;
    if (streq(argv[3], "CAN")) {
        outputId = OUTPUT_CAN;
    } else if (streq(argv[3], "REALDASH")) {
        outputId = OUTPUT_REALDASH;
    } else if (streq(argv[3], "SERIAL")) {
        outputId = OUTPUT_SERIAL;
    } else if (streq(argv[3], "SD_LOG") || streq(argv[3], "SD")) {
        outputId = OUTPUT_SD;
    } else {
        msg.control.print(F("ERROR: Unknown output '"));
        msg.control.print(argv[3]);
        msg.control.println(F("'"));
        msg.control.println(F("  Valid outputs: CAN, RealDash, Serial, SD_Log, ALL"));
        return 1;
    }

    if (setInputOutputMask(pin, outputId, enable)) {
        msg.control.print(F("Input "));
        msg.control.print(argv[1]);
        msg.control.print(F(" output "));
        msg.control.print(argv[3]);
        msg.control.print(F(" "));
        msg.control.println(enable ? F("enabled") : F("disabled"));
        msg.control.println(F("  (use SAVE to persist)"));
        return 0;
    }
    msg.control.println(F("ERROR: Input not configured"));
    return 1;
}

// SET <pin> CALIBRATION PRESET — clear custom calibration
static int set_calibration(uint8_t pin, int argc, const char* const* argv) {
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

    input->flags.useCustomCalibration = false;
    memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

    msg.control.print(F("Cleared custom calibration for pin "));
    msg.control.println(argv[1]);
    msg.control.println(F("Using preset calibration from sensor library"));
    return 0;
}

// SET <pin> RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
static int set_rpm(uint8_t pin, int argc, const char* const* argv) {
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

// SET <pin> SPEED <ppr> <tire_circ_mm> <ratio> [<mult>] <timeout> <max_speed>
static int set_speed(uint8_t pin, int argc, const char* const* argv) {
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

#if ENABLE_CAN
// SET <pin> CAN_TIMEOUT <ms>
static int set_can_timeout(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: CAN_TIMEOUT requires 1 parameter"));
        msg.control.println(F("  Usage: SET <pin> CAN_TIMEOUT <ms>"));
        msg.control.println(F("  Example: SET CAN:0 CAN_TIMEOUT 500"));
        msg.control.println(F("  Range: 100-30000 ms (default 2000)"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }
    if (input->calibrationType != CAL_CAN_IMPORT) {
        msg.control.println(F("ERROR: CAN_TIMEOUT only applies to CAN-imported sensors"));
        return 1;
    }

    uint16_t timeout_ms = (uint16_t)atoi(argv[3]);
    if (timeout_ms < 100 || timeout_ms > 30000) {
        msg.control.println(F("ERROR: Timeout must be between 100 and 30000 ms"));
        return 1;
    }

    if (!input->flags.useCustomCalibration) {
        memcpy_P(&input->customCalibration.can, input->presetCalibration, sizeof(input->customCalibration.can));
        input->flags.useCustomCalibration = true;
    }
    input->customCalibration.can.timeout_ms = timeout_ms;

    msg.control.print(F("CAN timeout set for pin "));
    msg.control.print(argv[1]);
    msg.control.print(F(": "));
    msg.control.print(timeout_ms);
    msg.control.println(F(" ms"));
    return 0;
}
#endif // ENABLE_CAN

// SET <pin> PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
static int set_pressure_linear(uint8_t pin, int argc, const char* const* argv) {
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
// Generic bias resistor — works with Steinhart-Hart, Lookup, Beta, Pressure Polynomial
static int set_bias(uint8_t pin, int argc, const char* const* argv) {
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

    #define BIAS_R_MIN 10.0
    #define BIAS_R_MAX 10000000.0
    if (bias < BIAS_R_MIN || bias > BIAS_R_MAX) {
        msg.control.print(F("ERROR: Bias resistor ("));
        msg.control.print(bias, 1);
        msg.control.println(F("Ω) must be between 10Ω and 10MΩ"));
        return 1;
    }

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
static int set_steinhart(uint8_t pin, int argc, const char* const* argv) {
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

    if (bias_r <= 0) {
        msg.control.println(F("ERROR: bias_r must be > 0"));
        return 1;
    }
    if (a == 0 || b == 0 || c == 0) {
        msg.control.println(F("WARNING: Zero coefficient detected - may indicate error"));
    }

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
static int set_beta(uint8_t pin, int argc, const char* const* argv) {
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
static int set_pressure_poly(uint8_t pin, int argc, const char* const* argv) {
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

    if (bias_r <= 0) {
        msg.control.println(F("ERROR: bias_r must be > 0"));
        return 1;
    }

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

// SET field tokens — PROGMEM string literals so the dispatch table itself
// can live in flash on AVR (saves ~70 bytes RAM on mega2560).
static const char PSTR_SET_APPLICATION[]     PROGMEM = "APPLICATION";
static const char PSTR_SET_SENSOR[]          PROGMEM = "SENSOR";
static const char PSTR_SET_NAME[]            PROGMEM = "NAME";
static const char PSTR_SET_DISPLAY_NAME[]    PROGMEM = "DISPLAY_NAME";
static const char PSTR_SET_DISPLAY[]         PROGMEM = "DISPLAY";
static const char PSTR_SET_UNITS[]           PROGMEM = "UNITS";
static const char PSTR_SET_ALARM[]           PROGMEM = "ALARM";
static const char PSTR_SET_DIVIDER[]         PROGMEM = "DIVIDER";
static const char PSTR_SET_OUTPUT[]          PROGMEM = "OUTPUT";
static const char PSTR_SET_CALIBRATION[]     PROGMEM = "CALIBRATION";
static const char PSTR_SET_RPM[]             PROGMEM = "RPM";
static const char PSTR_SET_SPEED[]           PROGMEM = "SPEED";
#if ENABLE_CAN
static const char PSTR_SET_CAN_TIMEOUT[]     PROGMEM = "CAN_TIMEOUT";
#endif
static const char PSTR_SET_PRESSURE_LINEAR[] PROGMEM = "PRESSURE_LINEAR";
static const char PSTR_SET_BIAS[]            PROGMEM = "BIAS";
static const char PSTR_SET_STEINHART[]       PROGMEM = "STEINHART";
static const char PSTR_SET_BETA[]            PROGMEM = "BETA";
static const char PSTR_SET_PRESSURE_POLY[]   PROGMEM = "PRESSURE_POLY";

// SET field dispatch table — PROGMEM-resident on AVR; access via pgm_read_ptr.
static const SetField SET_FIELDS[] PROGMEM = {
    { PSTR_SET_APPLICATION,     set_application },
    { PSTR_SET_SENSOR,          set_sensor },
    { PSTR_SET_NAME,            set_name },
    { PSTR_SET_DISPLAY_NAME,    set_display_name },
    { PSTR_SET_DISPLAY,         set_display },
    { PSTR_SET_UNITS,           set_units },
    { PSTR_SET_ALARM,           set_alarm },
    { PSTR_SET_DIVIDER,         set_divider },
    { PSTR_SET_OUTPUT,          set_output },
    { PSTR_SET_CALIBRATION,     set_calibration },
    { PSTR_SET_RPM,             set_rpm },
    { PSTR_SET_SPEED,           set_speed },
#if ENABLE_CAN
    { PSTR_SET_CAN_TIMEOUT,     set_can_timeout },
#endif
    { PSTR_SET_PRESSURE_LINEAR, set_pressure_linear },
    { PSTR_SET_BIAS,            set_bias },
    { PSTR_SET_STEINHART,       set_steinhart },
    { PSTR_SET_BETA,            set_beta },
    { PSTR_SET_PRESSURE_POLY,   set_pressure_poly },
};
static const uint8_t NUM_SET_FIELDS = sizeof(SET_FIELDS) / sizeof(SetField);

int cmd_set(int argc, const char* const* argv) {
    // SET <pin> <field> <value>
    // Also supports combined syntax: SET <pin> <application> <sensor>

    if (argc < 3) {
        msg.control.println(F("ERROR: Invalid SET syntax"));
        msg.control.println(F("  Usage: SET <pin> <field> <value>"));
        msg.control.println(F("  Example: SET A0 APPLICATION CHT"));
        msg.control.println(F("  Or combined: SET 7 EGT MAX31855"));
        return 1;
    }

    // Parse pin (allocates virtual slot for "CAN" / "I2C" — must only happen once)
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
                    msg.control.print(measurementTypeName(sensorMeasType));
                    msg.control.print(F(" but "));
                    msg.control.print(field);
                    msg.control.print(F(" expects "));
                    msg.control.print(measurementTypeName(appMeasType));
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

    // SET CAN <pid>  -  Import CAN sensor by PID
    // Example: SET CAN 0x0C  (imports Engine RPM from OBD-II)
    // This automatically assigns the next available CAN virtual pin (CAN:0, CAN:1, etc.)
#if ENABLE_CAN
    if (streq(argv[1], "CAN") && argc >= 3) {
        // Parse PID (supports hex like 0x0C or decimal like 12)
        uint8_t pid;
        if (argv[2][0] == '0' && (argv[2][1] == 'x' || argv[2][1] == 'X')) {
            pid = (uint8_t)strtoul(argv[2] + 2, nullptr, 16);
        } else {
            pid = (uint8_t)atoi(argv[2]);
        }

        // pin was already allocated by parsePin("CAN") at the top of cmd_set
        uint8_t virtualPin = pin;

        // Lookup standard PID info
        const StandardPIDInfo* pidInfo = lookupStandardPID(pid);

        // Configure as CAN_IMPORT sensor
        uint8_t canSensorIndex = getSensorIndexByName("CAN_IMPORT");
        if (canSensorIndex == 0) {
            msg.control.println(F("ERROR: CAN_IMPORT sensor not found in library"));
            return 1;
        }

        // Create the input slot before setInputSensor (which requires the slot to exist)
        if (!allocateInputSlot(virtualPin)) {
            return 1;
        }

        if (!setInputSensor(virtualPin, canSensorIndex)) {
            msg.control.println(F("ERROR: Failed to configure CAN sensor"));
            freeInputSlot(virtualPin);
            return 1;
        }

        Input* input = getInputByPin(virtualPin);
        if (!input) {
            msg.control.println(F("ERROR: Failed to create CAN input"));
            return 1;
        }

        // Configure CAN calibration
        if (pidInfo) {
            // Use standard PID info for automatic configuration
            input->customCalibration.can.source_can_id = 0x7E8;  // OBD-II default
            input->customCalibration.can.source_pid = pid;
            input->customCalibration.can.data_offset = 0;
            input->customCalibration.can.data_length = pidInfo->data_length;
            input->customCalibration.can.is_big_endian = true;
            input->customCalibration.can.scale_factor = pidInfo->scale_factor;
            input->customCalibration.can.offset = pidInfo->offset;
            input->customCalibration.can.timeout_ms = CAN_DEFAULT_TIMEOUT_MS;
            input->flags.useCustomCalibration = true;

            // Set measurement type from standard PID table
            input->measurementType = pidInfo->measurementType;

            // Set display name from standard table
            strncpy_P(input->displayName, pidInfo->name, sizeof(input->displayName) - 1);
            input->displayName[sizeof(input->displayName) - 1] = '\0';
            strncpy_P(input->abbrName, pidInfo->abbr, sizeof(input->abbrName) - 1);
            input->abbrName[sizeof(input->abbrName) - 1] = '\0';

            msg.control.print(F("✓ Imported CAN sensor CAN:"));
            msg.control.print(virtualPin - 0xC0);
            msg.control.print(F(" - PID 0x"));
            if (pid < 0x10) msg.control.print('0');
            msg.control.print(pid, HEX);
            msg.control.print(F(" ("));
            msg.control.print(input->displayName);
            msg.control.println(F(")"));
        } else {
            // Unknown PID - use default calibration
            input->customCalibration.can.source_can_id = 0x7E8;
            input->customCalibration.can.source_pid = pid;
            input->customCalibration.can.data_offset = 0;
            input->customCalibration.can.data_length = 1;
            input->customCalibration.can.is_big_endian = true;
            input->customCalibration.can.scale_factor = 1.0;
            input->customCalibration.can.offset = 0.0;
            input->customCalibration.can.timeout_ms = CAN_DEFAULT_TIMEOUT_MS;
            input->flags.useCustomCalibration = true;

            sprintf(input->displayName, "CAN PID 0x%02X", pid);
            sprintf(input->abbrName, "C%02X", pid);

            msg.control.print(F("✓ Imported CAN sensor CAN:"));
            msg.control.print(virtualPin - 0xC0);
            msg.control.print(F(" - PID 0x"));
            if (pid < 0x10) msg.control.print('0');
            msg.control.print(pid, HEX);
            msg.control.println(F(" (unknown PID - using defaults)"));
            msg.control.println(F("  Hint: Use 'SET CAN:0 ...' commands to customize"));
        }

        input->flags.isEnabled = true;
        return 0;
    }
#endif // ENABLE_CAN

    // Field dispatch — try the leaves above. SET_FIELDS lives in PROGMEM on
    // AVR, so read entries via pgm_read_ptr; on Teensy/ESP32 PROGMEM is a no-op
    // and pgm_read_ptr is a plain dereference.
    for (uint8_t i = 0; i < NUM_SET_FIELDS; i++) {
        const char* token = (const char*)pgm_read_ptr(&SET_FIELDS[i].token);
        if (streq_P(field, token)) {
            SetFieldHandler h = (SetFieldHandler)pgm_read_ptr(&SET_FIELDS[i].handler);
            return h(pin, argc, argv);
        }
    }

    // Unknown field
    msg.control.print(F("ERROR: Unknown SET field '"));
    msg.control.print(field);
    msg.control.println(F("'"));
    msg.control.println(F("  Type 'HELP SET' for usage information"));
    return 1;
}

#if ENABLE_SELFTEST
// Validate every SET_FIELDS entry. Reads through pgm_read_ptr — the same path
// cmd_set's dispatch loop takes — so a regression that breaks PROGMEM access
// fails this check immediately.
int selftestSetTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_SET_FIELDS; i++) {
        const char*     tok = (const char*)pgm_read_ptr(&SET_FIELDS[i].token);
        SetFieldHandler h   = (SetFieldHandler)pgm_read_ptr(&SET_FIELDS[i].handler);
        failures += validateDispatchEntry(F("SET"), i, tok, (const void*)h, true);
    }
    return failures;
}
#endif // ENABLE_SELFTEST
