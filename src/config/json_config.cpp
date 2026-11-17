/*
 * json_config.cpp - JSON Configuration Export/Import Implementation
 *
 * NOTE: This entire file is excluded from static config builds to save memory.
 *       JSON features are only available in EEPROM mode (runtime config).
 */

#include "../sensors/can_sensor_library/obd_output_pids.h"  // obdLengthFor (#516)
#include "../config.h"
#include <errno.h>
#include <ctype.h>

// Only compile JSON features on platforms with enough RAM and in runtime config mode
#include "json_config.h"
#include "json_registry.h"
#include "../buses/pinctrl/board_pins.h"   // boardName(), boardHasPin(): firmware.platform and the board check (#567, #570)
#if SUPPORTS_JSON_CONFIG
#include "system_config.h"
#include "../hal/bus_defaults.h"
#if INCLUDE_CAN
#include "../sensors/can/input_can.h"   // syncCanToConfig (#612)
#endif
#include "../buses/pinctrl/bus_wiring.h"
#include "../buses/serial_manager.h"
#include "../inputs/input.h"
#include "../inputs/input_manager.h"
#include "../buses/pinctrl/pin_label.h"
#include "../inputs/calibration_apply.h"
#include "../sensors/units_registry.h"
#include "../inputs/measurement_compat.h"  // units vs measurement type (#458)
#include "../sensors/sensor_library.h"
#include "../sensors/application_presets.h"
#include "../version.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../buses/pinctrl/pin_registry.h"
#include "../core/watchdog.h"
#include "../transports/sd_manager.h"
#include "../outputs/formats/obd_query.h"     // PID table rebuilt after an inputs import (#562)
#include "../outputs/formats/output_format.h"   // SdLogFormat / parseSdLogFormat
#include "../outputs/annunciator/output_alarm.h"   // alarmSilence* policy accessors (#433)
#include "../outputs/table/output_base.h"    // syncOutputsToConfig (#610)
#include "../outputs/table/output_table.h"
#include "../outputs/table/output_admission.h"   // import admission (#612)
#if INCLUDE_RULES_ENGINE
#include "../rules/rules_json.h"
#include "../rules/rules_manager.h"
#endif
#include "../core/config_generation.h"

// JSON config is a COLD path: it runs only on SYSTEM DUMP JSON / JSON IMPORT,
// never in the sensor loop. On Teensy 4.x every function lands in ITCM (RAM1) by
// default, and ITCM is allocated in 32 KB blocks -- crossing a block boundary
// costs 32 KB of RAM1 that local variables can no longer use, not the few hundred
// bytes the code actually occupies. FLASHMEM keeps this module in flash where it
// belongs. Same macro json_registry.cpp already uses; a no-op off Teensy.
//
// Unconditional (not nested under SUPPORTS_SD): the functions below using
// FLASHMEM are used regardless of SD support (SUPPORTS_JSON_CONFIG can be 1
// with SUPPORTS_SD 0, e.g. an SD-less board with JSON config still enabled).
// Previously this only compiled by accident, via a same-macro #define that
// leaked in from inputs/input_manager.cpp's own (now moved, #402 step 1)
// FLASHMEM guard when both files were amalgamated into one translation unit
// (as the native tests do) -- exposed once that guard moved to pin_label.cpp.
#ifndef FLASHMEM
#define FLASHMEM
#endif

#if SUPPORTS_SD
#include "../transports/sd_fs.h"   // SD library for this platform (STM32: SDIO)
#endif

// External references
extern Input inputs[MAX_INPUTS];
extern SystemConfig systemConfig;
extern uint8_t numActiveInputs;

// Get current timestamp (seconds since epoch, if available)
// For now, we'll use millis() / 1000 as a placeholder
static FLASHMEM uint32_t getCurrentTimestamp() {
    return millis() / 1000;
}

// Get calibration type string
//
// static_assert next to the switch: a CalibrationType this doesn't name falls
// to "UNKNOWN" at runtime, which exportCalibration() below cannot pair with a
// re-importable "source":"CUSTOM" block (#382) -- so a 13th value must fail
// the BUILD, not silently export an unimportable record.
static_assert(NUM_CALIBRATION_TYPES == 12,
              "CalibrationType grew -- add the new value to getCalibrationType() "
              "and exportCalibration() below, then update this count");
static FLASHMEM const char* getCalibrationType(const Input* input) {
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
        case CAL_SPEED: return "SPEED";
        case CAL_CAN_IMPORT: return "CAN_IMPORT";
        case CAL_LEVEL_TABLE: return "LEVEL_TABLE";
        default: return "UNKNOWN";
    }
}

// Emit one float calibration parameter.
//
// ArduinoJson stores a value that fits in a float AS a float (VariantImpl.hpp:
// setFloat) and prints a 4-byte float with only 6 significant digits
// (TextFormatter.hpp: `sizeof(T) >= 8 ? 9 : 6`), so a coefficient could not
// survive DUMP -> IMPORT: a Steinhart B of 0.0002341 came back out as 0.000234,
// and several other values lost their low bit. Casting to double doesn't help —
// the library narrows it straight back. So render the number here with the nine
// significant digits a float needs and hand ArduinoJson finished text
// (serialized() with a non-const char* copies the buffer).
//
// The trade-off is cosmetic: an inexact decimal now dumps as 1.02100003 rather
// than 1.02. Exactness wins — these are calibration coefficients.
static FLASHMEM void addFloatParam(JsonObject& params, const char* key, float value) {
    if (!isfinite(value)) {          // JSON has no NaN/Inf spelling
        params[key] = 0;
        return;
    }

    // Nine significant digits: shift the decimal count by the exponent.
    int decimals = 8;
    float magnitude = fabsf(value);
    if (magnitude > 0.0f) {
        decimals = 8 - (int)floorf(log10f(magnitude));
        if (decimals < 0) decimals = 0;
        if (decimals > 30) decimals = 30;
    }

    char buf[48];
    dtostrf(value, 0, (unsigned char)decimals, buf);

    // Trim the trailing zeros dtostrf's fixed-notation output leaves behind
    // ("2200.00000" -> "2200"), keeping the dump readable.
    char* dot = strchr(buf, '.');
    if (dot != nullptr) {
        char* end = buf + strlen(buf) - 1;
        while (end > dot && *end == '0') *end-- = '\0';
        if (end == dot) *end = '\0';
    }
    params[key] = serialized(buf);
}

// Export calibration parameters to JSON
//
// static_assert next to the switch: see getCalibrationType() above -- a new
// CalibrationType must be added to BOTH switches or the build fails (#382).
static_assert(NUM_CALIBRATION_TYPES == 12,
              "CalibrationType grew -- add the new value to exportCalibration() "
              "and getCalibrationType() above, then update this count");
static FLASHMEM void exportCalibration(JsonObject& calObj, const Input* input) {
    calObj["type"] = getCalibrationType(input);

    if (!input->flags.useCustomCalibration) {
        calObj["source"] = "PRESET";
        return;
    }

    // CAL_LEVEL_TABLE's bias resistor is inert -- like CAL_PRESSURE_TABLE, its
    // read path (sensors/level/table.cpp) takes the bias from the PROGMEM
    // preset unconditionally, so there is nothing genuinely customisable to
    // round-trip. Say PRESET rather than emit a CUSTOM block importCalibration()
    // would have to refuse anyway -- that self-contradictory "source":"CUSTOM",
    // no re-appliable params combination is exactly the #382 bug.
    if (input->calibrationType == CAL_LEVEL_TABLE) {
        calObj["source"] = "PRESET";
        return;
    }

    calObj["source"] = "CUSTOM";
    JsonObject params = calObj["params"].to<JsonObject>();

    // Float params go through addFloatParam() so they survive the trip as text;
    // integer fields keep printing as integers.

    switch (input->calibrationType) {
        case CAL_THERMISTOR_STEINHART:
            addFloatParam(params, "biasResistor", input->customCalibration.steinhart.bias_resistor);
            addFloatParam(params, "steinhartA", input->customCalibration.steinhart.steinhart_a);
            addFloatParam(params, "steinhartB", input->customCalibration.steinhart.steinhart_b);
            addFloatParam(params, "steinhartC", input->customCalibration.steinhart.steinhart_c);
            break;

        case CAL_THERMISTOR_BETA:
            addFloatParam(params, "biasResistor", input->customCalibration.beta.bias_resistor);
            addFloatParam(params, "beta", input->customCalibration.beta.beta);
            addFloatParam(params, "r0", input->customCalibration.beta.r0);
            addFloatParam(params, "t0", input->customCalibration.beta.t0);
            break;

        case CAL_THERMISTOR_TABLE:
            addFloatParam(params, "biasResistor", input->customCalibration.lookup.bias_resistor);
            break;

        case CAL_PRESSURE_TABLE:
            // Pressure tables use same union member as thermistor lookup (only bias resistor is customizable)
            addFloatParam(params, "biasResistor", input->customCalibration.lookup.bias_resistor);
            break;

        case CAL_LINEAR:
            addFloatParam(params, "voltageMin", input->customCalibration.pressureLinear.voltage_min);
            addFloatParam(params, "voltageMax", input->customCalibration.pressureLinear.voltage_max);
            addFloatParam(params, "outputMin", input->customCalibration.pressureLinear.output_min);
            addFloatParam(params, "outputMax", input->customCalibration.pressureLinear.output_max);
            break;

        case CAL_PRESSURE_POLYNOMIAL:
            addFloatParam(params, "biasResistor", input->customCalibration.pressurePolynomial.bias_resistor);
            addFloatParam(params, "polyA", input->customCalibration.pressurePolynomial.poly_a);
            addFloatParam(params, "polyB", input->customCalibration.pressurePolynomial.poly_b);
            addFloatParam(params, "polyC", input->customCalibration.pressurePolynomial.poly_c);
            break;

        case CAL_VOLTAGE_DIVIDER:
            addFloatParam(params, "r1", input->customCalibration.voltageDivider.r1);
            addFloatParam(params, "r2", input->customCalibration.voltageDivider.r2);
            addFloatParam(params, "correction", input->customCalibration.voltageDivider.correction);
            addFloatParam(params, "offset", input->customCalibration.voltageDivider.offset);
            break;

        case CAL_RPM:
            params["poles"] = input->customCalibration.rpm.poles;
            addFloatParam(params, "pulleyRatio", input->customCalibration.rpm.pulley_ratio);
            addFloatParam(params, "calibrationMult", input->customCalibration.rpm.calibration_mult);
            params["timeoutMs"] = input->customCalibration.rpm.timeout_ms;
            params["minRPM"] = input->customCalibration.rpm.min_rpm;
            params["maxRPM"] = input->customCalibration.rpm.max_rpm;
            break;

        case CAL_SPEED:
            params["pulsesPerRev"] = input->customCalibration.speed.pulses_per_rev;
            params["tireCircumferenceMm"] = input->customCalibration.speed.tire_circumference_mm;
            addFloatParam(params, "finalDriveRatio", input->customCalibration.speed.final_drive_ratio);
            addFloatParam(params, "calibrationMult", input->customCalibration.speed.calibration_mult);
            params["timeoutMs"] = input->customCalibration.speed.timeout_ms;
            params["maxSpeedKph"] = input->customCalibration.speed.max_speed_kph;
            break;

        case CAL_CAN_IMPORT:
            params["sourceCanId"] = input->customCalibration.can.source_can_id;
            params["sourcePid"] = input->customCalibration.can.source_pid;
            params["dataOffset"] = input->customCalibration.can.data_offset;
            params["dataLength"] = input->customCalibration.can.data_length;
            params["isBigEndian"] = (bool)input->customCalibration.can.is_big_endian;
            addFloatParam(params, "scaleFactor", input->customCalibration.can.scale_factor);
            addFloatParam(params, "offset", input->customCalibration.can.offset);
            params["timeoutMs"] = input->customCalibration.can.timeout_ms;
            break;

        default:
            break;
    }
}

// Export single input to JSON
FLASHMEM void exportInputToJSON(JsonObject& inputObj, const Input* input) {
    if (!input) {
        return;
    }

    // Basic info
    inputObj["pin"] = input->pin;
    {
        // Display label ("A0", "I2C:0", ...) — clients can't derive it from
        // the raw pin number without knowing the board's analog mapping.
        char pinLabel[8];
        formatPinLabel(input->pin, pinLabel, sizeof(pinLabel));
        inputObj["pinLabel"] = pinLabel;
    }
    inputObj["abbr"] = input->abbrName;
    inputObj["name"] = input->displayName;

    // Application, sensor, units (use registry names)
    inputObj["app"] = reinterpret_cast<const char*>(getApplicationNameByIndex(input->applicationIndex));
    inputObj["applicationIndex"] = input->applicationIndex;
    inputObj["sensor"] = reinterpret_cast<const char*>(getSensorNameByIndex(input->sensorIndex));
    inputObj["sensorIndex"] = input->sensorIndex;
    inputObj["units"] = reinterpret_cast<const char*>(getUnitStringByIndex(input->unitsIndex));
#if SUPPORTS_JSON_EXPORT
    // What the input measures, which SET UNITS checks against. Not always the
    // application's: a CAN import has application NONE and takes its type
    // from the PID (#517). Export-only; JSON IMPORT derives it again.
    inputObj["measurementType"] = jsonMeasurementTypeName((MeasurementType)input->measurementType);
#endif
    // Base→display conversion coefficients (display = base * factor + offset),
    // so a client can convert base-unit values (alarm thresholds below, alarm
    // frame value/threshold) to display units without duplicating the registry.
    if (input->unitsIndex < NUM_UNITS) {
        const UnitsInfo* uinfo = &UNITS_REGISTRY[input->unitsIndex];
        inputObj["unitsFactor"] = pgm_read_float(&uinfo->conversionFactor);
        inputObj["unitsOffset"] = pgm_read_float(&uinfo->conversionOffset);
    }

    // Alarm thresholds — read back from the input's control-engine alarm Rule
    // (the live source of truth), NOT the legacy input->min/maxValue, which
    // nothing evaluates. No Rule → both null, which the webapp renders as "no
    // alarm set" rather than a stale preset default (#295). A one-sided alarm
    // (BELOW = Min-only floor, ABOVE = Max-only ceiling) emits null on the unused
    // side, which the webapp already tolerates (#312).
    JsonObject alarm = inputObj["alarm"].to<JsonObject>();
#if INCLUDE_RULES_ENGINE
    int alarmSlot = rulesAlarmRuleSlotForInput(input->pin);
    if (alarmSlot >= 0) {
        const Rule& ar = rules[alarmSlot];
        // Which rules[] record this facade is derived from (#431). Without it
        // a client listing rules must re-implement rulesAlarmRuleSlotForInput's
        // band-over-one-sided / first-in-SLOT-order predicate — and slot order
        // is not recoverable from the dump after a JSON IMPORT reorders it, so
        // a pin with an extra hand-added RULE ADD could be misclassified.
        // Additive: absent in older dumps, clients fall back to the predicate.
        alarm["ruleId"] = ar.id;
        switch (ar.condition) {
            case RULE_BELOW:   // Min-only floor
                alarm["min"] = ar.limitLo;
                alarm["max"] = nullptr;
                break;
            case RULE_ABOVE:   // Max-only ceiling
                alarm["min"] = nullptr;
                alarm["max"] = ar.limitLo;
                break;
            default:           // RULE_OUTSIDE_BAND — Min+Max band
                alarm["min"] = ar.limitLo;
                alarm["max"] = ar.limitHi;
                break;
        }
        // Approach-tier (WARNING) edges (#425, slice B) — read-only facade for the
        // app's slider: the EFFECTIVE edge (AUTO derives it, so the slider can draw
        // the auto handle where it actually sits) plus an *Auto flag so it can be
        // labeled. NONE / an unused side both emit null + *Auto:false, mirroring how
        // min/max already emit null for a one-sided rule. warnMin <-> the lo side;
        // warnMax <-> the hi side — EXCEPT for an ABOVE (Max-only ceiling) rule,
        // where the single lo-side edge IS the max-side warning, so it is reported
        // as warnMax (mirrors how limitLo is reported as `max`, not `min`, for that
        // shape). EXPORT ONLY — see the import side below for why.
        float warnMinEdge = NAN, warnMaxEdge = NAN;
        bool  warnMinAuto = false, warnMaxAuto = false;
        if (ar.condition == RULE_ABOVE) {
            warnMaxEdge = rulesRuleWarnEdge(ar, false);
            warnMaxAuto = ruleWarnMode(ar, false) == RULE_WARN_AUTO;
        } else {
            warnMinEdge = rulesRuleWarnEdge(ar, false);
            warnMinAuto = ruleWarnMode(ar, false) == RULE_WARN_AUTO;
            if (ar.condition == RULE_OUTSIDE_BAND) {
                warnMaxEdge = rulesRuleWarnEdge(ar, true);
                warnMaxAuto = ruleWarnMode(ar, true) == RULE_WARN_AUTO;
            }
        }
        // ArduinoJson has no "serialize NaN as null" — assign null explicitly.
        if (isnan(warnMinEdge)) alarm["warnMin"] = nullptr; else alarm["warnMin"] = warnMinEdge;
        if (isnan(warnMaxEdge)) alarm["warnMax"] = nullptr; else alarm["warnMax"] = warnMaxEdge;
        alarm["warnMinAuto"] = warnMinAuto;
        alarm["warnMaxAuto"] = warnMaxAuto;
    } else {
        alarm["min"] = nullptr;
        alarm["max"] = nullptr;
        alarm["warnMin"] = nullptr;
        alarm["warnMax"] = nullptr;
        alarm["warnMinAuto"] = false;
        alarm["warnMaxAuto"] = false;
    }
#else
    alarm["min"] = input->minValue;
    alarm["max"] = input->maxValue;
#endif

    // Flags — cast the 1-bit bitfields to bool explicitly. Assigned raw, ArduinoJson
    // serializes a `uint8_t : 1` as a JSON integer (0/1, matching its promoted type),
    // not a boolean; the import side's `variant | default` idiom only returns the
    // stored value when `is<bool>()` is true, so an integer 0/1 silently fell through
    // to the hardcoded default on every import, discarding whatever was actually
    // captured (a disabled input always came back enabled).
    inputObj["enabled"] = (bool)input->flags.isEnabled;
    inputObj["alarmEnabled"] = (bool)input->flags.alarm;
    inputObj["displayEnabled"] = (bool)input->flags.display;
    inputObj["routeMask"] = (uint8_t)(input->routeMask & outputsExistingMask());  // routes to outputs that exist (#612)

    // Voltage-divider ratio at the ADC pin (#445). Emitted only when one is set:
    // 1.0 is "no divider" and 0 is the pre-field EEPROM default the reader
    // already treats as 1.0, so both stay implicit and older dumps read the same.
    // Additive key, schemaVersion unchanged.
    if (input->divider_ratio != 0.0f && input->divider_ratio != 1.0f) {
        addFloatParam(inputObj, "divider", input->divider_ratio);
    }

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

// Export all inputs to JSON array. Walk every slot keyed on occupancy
// (pin != 0xFF): inputs[] is a sparse table and numActiveInputs is an
// enabled-count, not a bound — bounding by it dropped any input sitting
// past a freed hole. Disabled inputs export too ("enabled": false), so a
// client can render the full configured set.
FLASHMEM void exportInputsToJSON(JsonArray& inputsArray) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        Input* input = &inputs[i];
        if (input->pin != 0xFF) {
            JsonObject inputObj = inputsArray.add<JsonObject>();
            inputObj["idx"] = i;
            exportInputToJSON(inputObj, input);
        }
    }
}

// Export pin registry to JSON array
FLASHMEM void exportPinRegistryToJSON(JsonArray& pinsArray) {
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

#if INCLUDE_RULES_ENGINE
// Export the control-engine Rule + Action tables as top-level arrays (siblings
// of "inputs"). Only configured slots (id != RULES_ID_NONE) are emitted.
FLASHMEM void exportRulesToJSON(JsonArray& rulesArray) {
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].id == RULES_ID_NONE) continue;
        JsonObject ruleObj = rulesArray.add<JsonObject>();
        rulesExportRule(ruleObj, rules[i]);
    }
}

FLASHMEM void exportActionsToJSON(JsonArray& actionsArray) {
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (actions[i].id == RULES_ID_NONE) continue;
        JsonObject actionObj = actionsArray.add<JsonObject>();
        // Emit the full Rule membership (#343), not just the inline first member.
        // Size by the real bound on rulesActionRuleCount() — one inline member
        // plus at most MAX_ACTION_LINKS rows. MAX_RULES is a DIFFERENT knob
        // (mega2560: RULES 2, LINKS 2 → a count of 3 is representable), and
        // clamping to it would silently truncate the export instead of failing.
        uint8_t members[MAX_ACTION_LINKS + 1];
        const uint8_t n = rulesActionRuleCount(i);
        for (uint8_t m = 0; m < n; m++) members[m] = rulesActionRuleAt(i, m);
        rulesExportAction(actionObj, actions[i], members, n);
    }
}
#endif // INCLUDE_RULES_ENGINE

// One output you add, as SYSTEM DUMP JSON's system.outputs[] and OUTPUT LIST
// JSON carry it (#612): {id, format, transport, enabled, interval?, csv?}.
FLASHMEM void outputToJson(JsonObject obj, uint8_t id) {
    const OutputRecord* r = outputRecord(id);
    if (!r) return;
    char t[12];
    outputTransportName(r->transport, t, sizeof(t));
    obj["id"]        = id;
    obj["format"]    = outputFormatName((OutputFormat)r->format);
    obj["transport"] = t;
    obj["enabled"]   = r->enabled != 0;
    // Only paced outputs have an interval (#608)
    if (outputFormatPaced((OutputFormat)r->format)) obj["interval"] = r->interval;
#if INCLUDE_SD_LOGGING
    if (r->format == FMT_CSV && r->transport == OT_SD) obj["csv"] = sdLogFormatName(r->option);
#endif
}

// Export system configuration to JSON
FLASHMEM void exportSystemConfigToJSON(JsonObject& systemObj) {
    // The outputs you add (#612): the stored table, ids assigned by OUTPUT ADD
    JsonArray outputs = systemObj["outputs"].to<JsonArray>();
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        if (outputExists(id)) outputToJson(outputs.add<JsonObject>(), id);
    }

    // The alarm is a built-in output: on/off and its acknowledgement policy
    // (#433). Was system.outputs.alarm before #612.
    JsonObject alarm = systemObj["alarm"].to<JsonObject>();
    alarm["enabled"] = systemConfig.alarmEnabled != 0;
#if INCLUDE_RULES_ENGINE
    alarm["silenceSeconds"] = (uint16_t)(alarmSilenceDurationMs() / 1000UL);
    alarm["silenceLed"]     = alarmSilenceIncludesLed();
#endif

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


    JsonObject defaultUnits = display["defaultUnits"].to<JsonObject>();
    defaultUnits["temperature"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultTempUnits));
    defaultUnits["pressure"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultPressUnits));
    defaultUnits["elevation"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultElevUnits));
    defaultUnits["speed"] = reinterpret_cast<const char*>(getUnitStringByIndex(systemConfig.defaultSpeedUnits));

    // No "timing": its sensorRead/alarmCheck were never used and are retired (#601).

    // Export all registered pins from pin registry (read-only diagnostic — pins are compile-time constants from the board profile)
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
    // One set of settings per CAN bus (#612 phase 2a). The OBD output's bus is
    // its record's transport in system.outputs.
    JsonArray canBuses = buses["can"].to<JsonArray>();
    for (uint8_t b = 0; b < NUM_CAN_BUSES && b < CAN_BUS_SLOTS; b++) {
        JsonObject c = canBuses.add<JsonObject>();
        static const char* const CAN_MODE_NAMES[] = {"OFF", "NORMAL", "LISTEN"};
        const uint8_t m = systemConfig.buses.can[b].mode;
        c["bus"] = b;
        c["mode"] = CAN_MODE_NAMES[m <= CAN_BUS_LISTEN ? m : 0];
        c["baudrate"] = canBaudFromIndex(systemConfig.buses.can[b].baudIndex);
        c["input"] = systemConfig.buses.canInputBus == b;
    }

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
        port["framed"] = (bool)(systemConfig.serial.framed_enabled_mask & (1 << i));
    }

    // The console and the log (#612 phase 2b-1, §14.7). The console's
    // transports are shown for information: an import never moves the
    // console, which could cut the link the import arrives on (#561).
    char tbuf[12];
    JsonObject console = systemObj["console"].to<JsonObject>();
    outputTransportName(systemConfig.console.primary, tbuf, sizeof(tbuf));
    console["transport"] = tbuf;
    outputTransportName(systemConfig.console.secondary, tbuf, sizeof(tbuf));
    console["secondary"] = tbuf;

    JsonObject logObj = systemObj["log"].to<JsonObject>();
    outputTransportName(systemConfig.log.primary, tbuf, sizeof(tbuf));
    logObj["transport"] = tbuf;
    outputTransportName(systemConfig.log.secondary, tbuf, sizeof(tbuf));
    logObj["secondary"] = tbuf;
    const char* levelNames[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG"};
    logObj["consoleLevel"] = systemConfig.logFilter.console_level <= 4 ? levelNames[systemConfig.logFilter.console_level] : "UNKNOWN";
    logObj["debugLevel"] = systemConfig.logFilter.debug_level <= 4 ? levelNames[systemConfig.logFilter.debug_level] : "UNKNOWN";
    char tagsBuf[11];  // "0xFFFFFFFF" + null terminator
    snprintf(tagsBuf, sizeof(tagsBuf), "0x%08lX", (unsigned long)systemConfig.logFilter.enabledTags);
    logObj["enabledTags"] = tagsBuf;
}

// JSON Schema Version
// Increment when making backward-incompatible changes to JSON structure
// Version history:
//   1 - Initial release (v0.4.1-alpha)
#define JSON_SCHEMA_VERSION 1

// Main export function - dump entire config to JSON
FLASHMEM void dumpConfigToJSON(Print& output) {
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
    firmware["platform"] = boardName();
    firmware["timestamp"] = getCurrentTimestamp();
    firmware["maxInputs"] = MAX_INPUTS;
    firmware["activeInputs"] = numActiveInputs;

    // Mirrors writeFirmwareJson()'s capability list (json_registry.cpp,
    // SYSTEM DUMP REGISTRY JSON) — duplicated rather than shared because that
    // helper lives in a SUPPORTS_JSON_EXPORT-only translation unit and this
    // one is SUPPORTS_JSON_CONFIG-gated; the two flags are aliased on every
    // board today but are logically independent knobs. Keep both lists in
    // sync if a capability is ever added to one.
    JsonArray caps = firmware["capabilities"].to<JsonArray>();
#if SUPPORTS_JSON_IMPORT_STREAM
    caps.add("json_import");
    // JSON IMPORT replaces the inputs instead of merging (#566), so a client
    // must not CLEAR them first: a refused file would leave the unit empty
    // (#569). Clients can't infer this from firmware.build, which counts the
    // building repo's commits (#603).
    caps.add("json_import_replace");
#endif
    caps.add("json_export");
#if INCLUDE_CAN
    caps.add("can");
#endif
#if INCLUDE_SD_LOGGING
    caps.add("sd");
#endif
#if INCLUDE_LCD
    caps.add("lcd");
#endif
#if INCLUDE_OLED
    caps.add("oled");
#endif

    // System configuration
    JsonObject system = doc["system"].to<JsonObject>();
    exportSystemConfigToJSON(system);

    // Inputs
    JsonArray inputsArray = doc["inputs"].to<JsonArray>();
    exportInputsToJSON(inputsArray);

#if INCLUDE_RULES_ENGINE
    // Control engine — Rule + Action tables (top-level, additive; omitted when
    // empty stays empty arrays so the webapp can tell "supported, none" apart
    // from "not supported").
    JsonArray rulesArray = doc["rules"].to<JsonArray>();
    exportRulesToJSON(rulesArray);
    JsonArray actionsArray = doc["actions"].to<JsonArray>();
    exportActionsToJSON(actionsArray);
#endif

    // Serialize - feed watchdog every 32 bytes to survive slow transports
    // (e.g. HM-10 BLE bridge at 9600 baud blocks in write() long enough to
    //  trip a 2-second watchdog that can't be disabled at runtime)
    WatchdogKickingPrint wdOut(output);
    serializeJsonPretty(doc, wdOut);
    output.println();
}

// Import calibration from JSON (#373).
//
// This used to parse nothing and return true, so SYSTEM DUMP JSON -> JSON IMPORT
// silently dropped every hand-tuned calibration and left the input on its preset.
// It now applies the exported params through the same validating core the CLI's
// `SET <pin> CAL ...` leaves use (inputs/calibration_apply.h), so the two paths
// cannot disagree about what a valid calibration is, and refuses the record when
// the calibration is malformed or rejected rather than importing a half-record.
//
// Covers every type exportCalibration() emits with parameters, with one
// deliberate exception:
//   - PRESSURE_TABLE: its read path takes the bias resistor from the PROGMEM
//     preset unconditionally, so a custom bias is inert and the CLI refuses to
//     set one. A CUSTOM record of this type is refused rather than silently
//     accepted (it is unreachable from any current firmware export).
// LEVEL_TABLE is the same "bias is inert" situation but exportCalibration()
// never emits a CUSTOM block for it (source is forced to PRESET on export,
// #382), so there is no CUSTOM LEVEL_TABLE record to import in the first
// place -- it falls to the same "cannot be imported" refusal below as
// PRESSURE_TABLE if one is hand-written anyway.
//
// The caller passes a STAGED (scratch) Input, not the live one: the record is
// judged before anything is committed, so a refusal costs the table nothing.
// Only the override, its flag and the calibration type are meaningful on the way
// out - the caller copies those three onto the real input after the commit.
static FLASHMEM bool importCalibration(Input* input, JsonObject& calObj, uint8_t index) {
    if (input == nullptr) return false;

    // Missing "source" used to reach strcmp(nullptr, ...) and fault the board on
    // any hand-edited or third-party JSON.
    const char* source = calObj["source"];
    if (source == nullptr) {
        msg.debug.error(TAG_JSON, "Input %d: calibration has no source", index);
        return false;
    }

    if (strcmp(source, "PRESET") == 0) {
        // Explicitly clear rather than relying on setInputApplication()/
        // setInputSensor() having cleared it on the way through.
        input->flags.useCustomCalibration = false;
        memset(&input->customCalibration, 0, sizeof(CalibrationOverride));
        return true;
    }

    if (strcmp(source, "CUSTOM") != 0) {
        msg.debug.error(TAG_JSON, "Input %d: unknown calibration source", index);
        return false;
    }

    JsonVariant paramsVar = calObj["params"];
    const char* type = calObj["type"];
    if (type == nullptr || !paramsVar.is<JsonObject>()) {
        msg.debug.error(TAG_JSON, "Input %d: custom calibration missing type/params", index);
        return false;
    }
    JsonObject p = paramsVar.as<JsonObject>();

    // Start from a clean override so the imported record is byte-deterministic
    // (and so applyCalBias() still sees "was on the preset" and seeds correctly).
    input->flags.useCustomCalibration = false;
    memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

    CalApplyStatus st;
    if (strcmp(type, "THERMISTOR_STEINHART") == 0) {
        st = applyCalSteinhart(input,
                               p["biasResistor"] | 0.0f, p["steinhartA"] | 0.0f,
                               p["steinhartB"] | 0.0f,   p["steinhartC"] | 0.0f);
    } else if (strcmp(type, "THERMISTOR_BETA") == 0) {
        st = applyCalBeta(input,
                          p["biasResistor"] | 0.0f, p["beta"] | 0.0f,
                          p["r0"] | 0.0f,           p["t0"] | 0.0f);
    } else if (strcmp(type, "PRESSURE_POLYNOMIAL") == 0) {
        st = applyCalPressurePoly(input,
                                  p["biasResistor"] | 0.0f, p["polyA"] | 0.0f,
                                  p["polyB"] | 0.0f,        p["polyC"] | 0.0f);
    } else if (strcmp(type, "LINEAR") == 0) {
        st = applyCalLinear(input,
                            p["voltageMin"] | 0.0f, p["voltageMax"] | 0.0f,
                            p["outputMin"] | 0.0f,  p["outputMax"] | 0.0f);
    } else if (strcmp(type, "VOLTAGE_DIVIDER") == 0) {
        st = applyCalVoltageDivider(input,
                                    p["r1"] | 0.0f,         p["r2"] | 0.0f,
                                    p["correction"] | 0.0f, p["offset"] | 0.0f);
    } else if (strcmp(type, "RPM") == 0) {
        st = applyCalRpm(input,
                         (uint8_t)(p["poles"] | 0),
                         p["pulleyRatio"] | 0.0f,
                         p["calibrationMult"] | 0.0f,
                         (uint16_t)(p["timeoutMs"] | 0),
                         (uint16_t)(p["minRPM"] | 0),
                         (uint16_t)(p["maxRPM"] | 0));
    } else if (strcmp(type, "THERMISTOR_TABLE") == 0) {
        // Bias resistor is the only overridable field for a table sensor; the
        // input keeps the calibration type its sensor gave it.
        st = applyCalBias(input, p["biasResistor"] | 0.0f);
    } else if (strcmp(type, "SPEED") == 0) {
        st = applyCalSpeed(input,
                           (uint8_t)(p["pulsesPerRev"] | 0),
                           (uint16_t)(p["tireCircumferenceMm"] | 0),
                           p["finalDriveRatio"] | 0.0f,
                           p["calibrationMult"] | 0.0f,
                           (uint16_t)(p["timeoutMs"] | 0),
                           (uint16_t)(p["maxSpeedKph"] | 0));
    } else if (strcmp(type, "CAN_IMPORT") == 0) {
        st = applyCalCanImport(input,
                               (uint16_t)(p["sourceCanId"] | 0),
                               (uint8_t)(p["sourcePid"] | 0),
                               (uint8_t)(p["dataOffset"] | 0),
                               (uint8_t)(p["dataLength"] | 0),
                               p["isBigEndian"] | false,
                               p["scaleFactor"] | 0.0f,
                               p["offset"] | 0.0f,
                               (uint16_t)(p["timeoutMs"] | 0));
    } else {
        // PRESSURE_TABLE, the "UNKNOWN" types, and anything a future firmware
        // adds: nothing here can re-apply it, so say so instead of returning
        // success over a dropped calibration.
        msg.debug.error(TAG_JSON, "Input %d: calibration type %s cannot be imported",
                        index, type);
        return false;
    }

    if (st == CAL_APPLY_ERR_SENSOR_TYPE) {
        // A profile the sensor can't read (#460): keep the input on its preset
        // and say so, rather than drop the record (import policy, #425).
        input->flags.useCustomCalibration = false;
        memset(&input->customCalibration, 0, sizeof(CalibrationOverride));
        msg.control.print(F("  NOTE: input "));
        msg.control.print(index);
        msg.control.print(F(" calibration "));
        msg.control.print(type);
        msg.control.println(F(" doesn't match its sensor; using the preset"));
        return true;
    }
    if (st != CAL_APPLY_OK) {
        // Leave nothing half-applied behind.
        input->flags.useCustomCalibration = false;
        memset(&input->customCalibration, 0, sizeof(CalibrationOverride));
        msg.debug.error(TAG_JSON, "Input %d: calibration rejected (type %s, code %d)",
                        index, type, (int)st);
        return false;
    }
    return true;
}

// Import a single input record.
//
// CONTRACT - all-or-nothing, the same one setInputApplication() carries (#371).
// On `false` NOTHING has changed: no slot was claimed, numActiveInputs is
// untouched, and an input that already existed on `pin` keeps every field it
// had. It is guaranteed structurally: everything the record could be refused
// for is judged in an ADMISSION phase, before the first write, and the COMMIT
// phase that follows cannot fail.
//
// This matters because "N of M inputs imported" is a claim about the table: a
// record that failed on its calibration used to leave an enabled input behind,
// reading a live sensor, while the summary said it had imported nothing (#373).
FLASHMEM bool importInputFromJSON(JsonObject& inputObj, uint8_t index) {
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
    // A missing `units` is not fatal: findUnitsIndexByName(nullptr) is
    // UNITS_NOT_FOUND, which degrades to the application default below (#458).
    if (!appName || !sensorName) {
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
    // strict: a miss is UNITS_NOT_FOUND (#458); the export writes the symbol (#509)
    uint8_t unitsIdx = findUnitsIndexByNameOrSymbol(unitsName);

    msg.debug.debug(TAG_JSON, "Registry indices: app=%d, sensor=%d, units=%d", appIdx, sensorIdx, unitsIdx);

    // Validate registry lookups (index 0 = NONE for app/sensor). Units are
    // judged below, after the sensor resolves: a bad units key degrades, it
    // never fails the record.
    // A CAN import has no application: SET CAN builds it as app NONE +
    // CAN_IMPORT on a CAN:n virtual pin, and the dump exports it that way.
    // Rejecting app NONE outright dropped every CAN input on restore (#499).
    // Only the literal "NONE" counts: an unknown name also resolves to 0.
    const bool canImportRecord = appIdx == 0 && strcasecmp(appName, "NONE") == 0 &&
                                 pin >= PIN_CAN_BASE && pin <= PIN_CAN_MAX &&
                                 sensorIdx != 0 && sensorIdx == getSensorIndexByName("CAN_IMPORT");
    if ((appIdx == 0 && !canImportRecord) || sensorIdx == 0) {
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

    // Measurement-type compatibility (#383), judged here rather than left to
    // setInputSensor(): on an EXISTING input the commit phase runs
    // setInputApplication() first, so a refusal there would leave the input
    // half-rewritten with the new application (#502). App NONE (a CAN import)
    // matches any sensor.
    if (!sensorMatchesApplication(sensorIdx, appIdx)) {
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.println(F(") - sensor measures a different quantity than the application"));
        return false;
    }

    // Resolve the sensor here rather than inside setInputSensor(): an unknown
    // sensor index is that function's ONLY failure mode, so pre-checking it is
    // what makes the commit phase below infallible. The resolved SensorInfo also
    // carries the calibration type the staging step needs.
    const SensorInfo* flashSensor = getSensorByIndex(sensorIdx);
    if (flashSensor == nullptr) {
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.println(F(") - unknown sensor"));
        msg.debug.error(TAG_JSON, "Unresolvable sensor index %d", sensorIdx);
        return false;
    }
    SensorInfo sensorInfo;
    loadSensorInfo(flashSensor, &sensorInfo);


    // Divider ratio (#445): same range check as SET <pin> DIVIDER, judged here
    // so a bad value fails the record before anything is written. Absent → 1.0
    // (no divider), which is also what setInputApplication() seeds below.
    // Read before the calibration is staged: a linear calibration's voltage
    // ceiling depends on it (#467).
    float dividerRatio = 1.0f;
    if (inputObj["divider"].isNull() == false) {
        dividerRatio = inputObj["divider"].as<float>();
        if (!(dividerRatio > 0.0f && dividerRatio <= 1.0f)) {
            msg.control.print(F("ERROR: Failed to import input "));
            msg.control.print(index);
            msg.control.print(F(" (pin "));
            msg.control.print(pin);
            msg.control.println(F(") - divider ratio must be > 0.0 and <= 1.0"));
            return false;
        }
    }

    // Stage the calibration on a scratch Input, so a calibration this firmware
    // refuses fails the record BEFORE anything is written. Applying it to the
    // real input at the end instead left an enabled, fully configured input
    // behind on a refusal while the summary reported 0 imported.
    //
    // The scratch costs ~100 bytes of stack and no RAM: this whole translation
    // unit is SUPPORTS_JSON_CONFIG-gated and mega2560/uno set that to 0, so it
    // never lands on the AVR budget that ruled a snapshot out in #371. Staging
    // (rather than validating and then re-applying) also means the bytes that
    // reach the input are exactly the bytes admission approved.
    bool haveCalibration = false;
    bool stagedCustom = false;
    CalibrationType stagedType = sensorInfo.calibrationType;
    CalibrationOverride stagedOverride;
    memset(&stagedOverride, 0, sizeof(stagedOverride));

    if (inputObj["calibration"].isNull() == false) {
        Input staged;
        memset(&staged, 0, sizeof(staged));
        staged.pin = pin;
        // What setInputSensor() would give the real input, so applyCal*() judges
        // the parameters against the same type and seeds from the same preset.
        staged.calibrationType = sensorInfo.calibrationType;
        staged.presetCalibration = sensorInfo.defaultCalibration;
        // A linear calibration is judged against the divider it will sit
        // behind (#467), so the record's ratio is read first.
        staged.divider_ratio = dividerRatio;

        JsonObject cal = inputObj["calibration"];
        if (!importCalibration(&staged, cal, index)) {
            msg.control.print(F("ERROR: Failed to import input "));
            msg.control.print(index);
            msg.control.print(F(" (pin "));
            msg.control.print(pin);
            msg.control.println(F(") - calibration rejected"));
            return false;
        }

        haveCalibration = true;
        stagedCustom = staged.flags.useCustomCalibration;
        stagedType = staged.calibrationType;   // a typed profile sets its own
        stagedOverride = staged.customCalibration;
    }

    // Units (#458): an unknown name or a unit of another quantity used to be
    // stored as-is (a typo became CELSIUS; °F on a pressure input ran the °F
    // conversion over bar). Degrade to the application's default units with a
    // NOTE instead of failing the record — a display preference isn't worth
    // losing the input's trip point and relay links over (import policy, #425).
    //
    // Judged after the calibration is staged: an imported OBD-II PID measures
    // what the PID table says, not the CAN_IMPORT preset's TEMPERATURE (#496).
    MeasurementType measured = inputMeasurementType(sensorIdx, appIdx);
    if (stagedCustom && stagedType == CAL_CAN_IMPORT) {
        measured = canImportMeasurementType(stagedOverride.can.source_can_id,
                                            stagedOverride.can.source_pid, measured);
    }
    if (!unitsMatchMeasurement(unitsIdx, measured)) {
        const ApplicationPreset* appPreset = getApplicationByIndex(appIdx);
        unitsIdx = preferredUnitsFor(measured, appPreset ? READ_APP_DEFAULT_UNITS(appPreset) : 0);
        msg.control.print(F("  NOTE: input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.print(F(") units '"));
        msg.control.print(unitsName ? unitsName : "");
        msg.control.println(F("' unknown or wrong type; using the default (SYSTEM UNITS)"));
    }

    // ===== COMMIT - from here on nothing may fail =====
    // Use input manager functions to properly configure the input
    // This ensures all function pointers and calibration are wired correctly
    extern bool setInputApplication(uint8_t pin, uint8_t appIndex);
    extern bool setInputSensor(uint8_t pin, uint8_t sensorIndex);
    extern bool setInputUnits(uint8_t pin, uint8_t unitsIndex);
    extern bool setInputAbbr(uint8_t pin, const char* abbr);
    extern bool setInputDisplayName(uint8_t pin, const char* displayName);
    extern bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue);
    extern bool enableInput(uint8_t pin, bool enable);
    extern bool enableInputAlarm(uint8_t pin, bool enable);
    extern bool enableInputDisplay(uint8_t pin, bool enable);
    extern bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length);

    // Let setInputApplication() own slot creation AND admission (#363).
    //
    // This used to do `inputs[index].pin = pin;` first, which made every
    // imported record look like an *existing* input to setInputApplication() --
    // so its refusal path never applied -- and then discarded the return value,
    // letting the setInputSensor() below finish building an input the validator
    // had just rejected (e.g. one sitting on an I2C/SPI/CAN-reserved pin: #351's
    // bus wedge, re-entered through LOAD / profile import). Honour the refusal
    // instead; importInputsFromJSON() already counts a false return as a failed
    // record for its summary.
    //
    // Side benefit of dropping the pre-assignment: the record no longer stomps
    // whatever unrelated input happened to occupy slot `index`.
    // Needed only by the tripwire below: a record that creates an input must be
    // able to give the slot back (#362), and one that modifies an existing input
    // must not free a slot that was not ours.
    const bool isNewInput = (getInputByPin(pin) == nullptr);

    // A CAN import is built the way SET CAN builds it: a bare slot, then the
    // sensor. setInputApplication(NONE) can't — the NONE preset has no
    // default sensor to resolve (#499).
    bool slotReady;
    if (canImportRecord) {
        // Resets an existing input's app/OBD PID like setInputApplication()
        // does; the record's own fields (obd2 only when set) re-apply below.
        slotReady = createCanImportInput(pin) != nullptr;
    } else {
        slotReady = setInputApplication(pin, appIdx);
    }
    if (!slotReady) {
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.print(F(" (pin "));
        msg.control.print(pin);
        msg.control.println(F(") - configuration rejected"));
        return false;
    }

    Input* input = getInputByPin(pin);
    if (input == nullptr) {
        // Unreachable: setInputApplication() returned true, so the slot exists.
        return false;
    }

    // Apply configuration using input manager functions.
    //
    // Neither call can fail here: setInputSensor()'s failure modes -- an
    // unresolvable sensor index and a sensor/application type mismatch (#383)
    // -- are both pre-checked during admission, and both bail only on a
    // missing slot, which the slot step above just created. The guard stays as
    // a tripwire -- if a future failure mode is added, a new record must still
    // leave nothing behind rather than a half-built input.
    if (!setInputSensor(pin, sensorIdx)) {
        if (isNewInput) freeInputSlot(pin);
        msg.control.print(F("ERROR: Failed to import input "));
        msg.control.print(index);
        msg.control.println(F(" - configuration failed"));
        return false;
    }

    // Set names
    if (inputObj["abbr"].isNull() == false) {
        setInputAbbr(pin, inputObj["abbr"]);
    }
    if (inputObj["name"].isNull() == false) {
        setInputDisplayName(pin, inputObj["name"]);
    }

    // Set alarm thresholds. This writes only the legacy input->min/maxValue
    // fields (dead — nothing evaluates them); the live alarm Rule is restored
    // from the top-level "rules" array via importRulesFromJSON, which already
    // handles one-sided ABOVE/BELOW. Only apply when BOTH sides are present: a
    // one-sided alarm carries a null side, and ArduinoJson would coerce that null
    // to 0.0 and trip setInputAlarmRange's "min < max" check with a spurious
    // error (#312). Skipping is loss-free since these fields are dead.
    if (inputObj["alarm"].isNull() == false) {
        JsonObject alarm = inputObj["alarm"];
        if (!alarm["min"].isNull() && !alarm["max"].isNull()) {
            setInputAlarmRange(pin, alarm["min"], alarm["max"]);
        }
    }

    // Set flags (use || for default value, not | which is bitwise OR)
    enableInput(pin, inputObj["enabled"] | true);  // Default to true if missing
    enableInputAlarm(pin, inputObj["alarmEnabled"] | false);  // Default to false if missing
    enableInputDisplay(pin, inputObj["displayEnabled"] | true);  // Default to true if missing
    // Routes (#612): one bit per output id; absent, every route
    input->routeMask = inputObj["routeMask"] | ROUTE_MASK_ALL;

    // OBD2
    if (inputObj["obd2"].isNull() == false) {
        JsonObject obd2 = inputObj["obd2"];
        uint8_t pid = obd2["pid"] | (uint8_t)0;
        setInputOBD(pin, pid, pid ? obdLengthFor(pid, obd2["length"] | (uint8_t)0) : 0);
    }

    // Divider ratio — after setInputApplication(), which resets it to 1.0.
    input->divider_ratio = dividerRatio;

    // Calibration: install the bytes admission already validated. Nothing here
    // can be refused - the judging happened before the first write, which is
    // what keeps a rejected calibration from leaving a live input behind.
    if (haveCalibration) {
        input->customCalibration = stagedOverride;
        input->flags.useCustomCalibration = stagedCustom;
        if (stagedCustom) {
            input->calibrationType = stagedType;
        }
    }

    // Units last: their check reads the calibration just installed (#496).
    // unitsIdx was judged against the same measurement type above, so this
    // cannot refuse.
    input->measurementType = inputMeasurementTypeOf(*input);
    setInputUnits(pin, unitsIdx);

    return true;
}

// Import all inputs from JSON
FLASHMEM bool importInputsFromJSON(JsonArray& inputsArray) {
    uint8_t importedCount = 0;
    uint8_t totalInputs = inputsArray.size();

    // An "inputs" array REPLACES the input table, as "rules" and "actions" already
    // replace theirs (#562): a dump describes the whole config, so restoring one
    // must not keep inputs added since. The file's records are applied first (an
    // input already on one of its pins is updated in place, so no pin ends up in
    // two slots), and only then are inputs the file does not mention removed --
    // and only if the import worked, so a file whose every record is refused
    // leaves the table as it was rather than empty. Rules are left alone: the
    // caller re-arms them, and rulesManagerInit() names any rule whose input
    // the file did not bring back.
    uint8_t filePins[32] = {0};   // bitmap of every pin the file names
    for (JsonVariant v : inputsArray) {
        const uint8_t pin = v["pin"] | 0xFF;
        filePins[pin >> 3] |= (uint8_t)(1u << (pin & 7));
    }

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

    // Recount by the enabled rule (matching input_manager): the import set
    // may include disabled inputs, which don't count as active.
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;
        }
    }
    // An empty array is a valid request for no inputs; otherwise at least one
    // record must have made it before anything is removed.
    const bool ok = totalInputs == 0 || importedCount > 0;
    if (ok) {
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            const uint8_t pin = inputs[i].pin;
            if (pin != 0xFF && !(filePins[pin >> 3] & (1u << (pin & 7)))) clearInput(pin);
        }
    }
    return ok;
}

// Import system configuration from JSON.
//
// ADMISSION/COMMIT (#382): the input-record half of import got this structure
// in #371/#373 -- everything that can be judged is judged before the first
// write, so a refusal changes nothing. This half used to write straight into
// the LIVE `systemConfig` global field-by-field with only block-level
// isNull() guards, which had two concrete failure modes:
//   - a "display" object present without a "type" key made `const char*
//     displayType = display["type"]` come back nullptr, and strcmp(nullptr,
//     "LCD") is undefined behaviour -- a hand-edited or partial config could
//     fault the board;
//   - a partial object (an old export, or a hand-edited one) coerced a
//     missing JsonVariant to 0, zeroing a value instead of leaving the
//     existing one alone -- the same "absent key must not become zero"
//     shape #377 already fixed for the bus selection below.
//
// The fix here is a STAGED copy: every key is applied to `staged` (seeded
// from the live config, so an absent key naturally means "keep the current
// value" rather than "coerce to zero/null"), each string field is null-
// guarded before use, and the live config is only overwritten once, at the
// very end -- one assignment nothing above this line can fail past.
#if NUM_SERIAL_PORTS > 0
// A serial port that is running and routed for commands may be the link this
// import arrived on, so the import never changes it (#561) -- the same reason
// SYSTEM RESET CONFIRM keeps serial settings (RESET_KEEP_LINK, #549). Read from
// systemConfig.console, which every CONSOLE TRANSPORT keeps in step (routingCaptureConfig).
FLASHMEM static bool serialPortCarriesCommands(uint8_t port_id) {
    if (!isSerialPortActive(port_id)) return false;
    const uint8_t t = (uint8_t)(TRANSPORT_SERIAL1 + (port_id - 1));
    return systemConfig.console.primary == t || systemConfig.console.secondary == t;
}
#endif

// "NONE".."DEBUG" -> 0..4, or 0xFF for anything else ("UNKNOWN", a typo, a
// non-string): an unreadable level keeps the current one rather than becoming NONE.
// PROGMEM, not literals: on Teensy 4 a literal (F() included) stays in RAM1,
// where these and the NOTE lines below cost a 1 KB step of stack on teensy41.
static const char kLevelNames[] PROGMEM = "NONE\0ERROR\0WARN\0INFO\0DEBUG\0";
FLASHMEM static uint8_t parseLogLevelName(const char* name) {
    if (!name) return 0xFF;
    const char* p = kLevelNames;
    for (uint8_t i = 0; i < 5; i++) {
        if (strcasecmp_P(name, p) == 0) return i;
        p += strlen_P(p) + 1;
    }
    return 0xFF;
}

static const char kSerialKept1[] PROGMEM = "NOTE: Serial";
static const char kDisplayAddrIgnored[] PROGMEM = "NOTE: display address ignored: valid 0x20-0x27 or 0x38-0x3F";
static const char kIntervalNoteA[] PROGMEM = "NOTE: outputs: id ";
static const char kIntervalNoteIgnored[] PROGMEM = ".interval is out of range or not a number; the default is used";
static const char kSerialKept2[] PROGMEM = " kept its settings: it carries commands (BUS SERIAL to change it)";
static const char kSerialReboot[] PROGMEM = "NOTE: serial port changes take effect after SAVE and a reboot";
#define JSON_FSTR(a) (reinterpret_cast<const __FlashStringHelper*>(a))

FLASHMEM bool importSystemConfigFromJSON(JsonObject& systemObj, bool importHasInputs) {
    // ---- ADMISSION PHASE -------------------------------------------------
    SystemConfig staged = systemConfig;

    // The alarm, a built-in output (system.alarm since #612)
    if (systemObj["alarm"].isNull() == false) {
        JsonObject alarmObj = systemObj["alarm"];
        if (alarmObj["enabled"].isNull() == false) {
            staged.alarmEnabled = alarmObj["enabled"] ? 1 : 0;
        }
#if INCLUDE_RULES_ENGINE
        // Acknowledgement policy (#433). Absent keys keep the current byte; an
        // out-of-range duration is dropped rather than failing the import.
        uint8_t byte = staged.alarmSilence;
        if (alarmObj["silenceSeconds"].is<long>()) {   // a non-number must not coerce to 0 (= CLEAR)
            const long secs = alarmObj["silenceSeconds"];
            uint8_t code = ALARM_SILENCE_DUR_DEFAULT;
            bool ok = true;
            if (secs == 0) code = ALARM_SILENCE_DUR_CLEAR;
            else if (secs < 0 || secs > ALARM_SILENCE_DUR_MAX_S ||
                     secs % ALARM_SILENCE_DUR_STEP_S != 0) ok = false;
            else code = (uint8_t)(secs / ALARM_SILENCE_DUR_STEP_S);
            if (ok) byte = (uint8_t)((byte & ALARM_SILENCE_LED_BIT) | code);
        }
        if (alarmObj["silenceLed"].isNull() == false) {
            const bool led = alarmObj["silenceLed"];
            byte = led ? (uint8_t)(byte | ALARM_SILENCE_LED_BIT)
                       : (uint8_t)(byte & ~ALARM_SILENCE_LED_BIT);
        }
        staged.alarmSilence = byte;
#endif
    }

    // Display settings
    if (systemObj["display"].isNull() == false) {
        JsonObject display = systemObj["display"];
        if (display["enabled"].isNull() == false) {
            staged.displayEnabled = display["enabled"];
        }

        // Null-guarded: an absent/non-string "type" used to reach
        // strcmp(nullptr, "LCD") and fault the board. Absent now means "keep
        // the current display type" rather than "silently force NONE".
        if (display["type"].isNull() == false) {
            const char* displayType = display["type"];
            if (displayType != nullptr) {
                if (strcmp(displayType, "LCD") == 0) {
                    staged.displayType = DISPLAY_LCD;
                } else if (strcmp(displayType, "OLED") == 0) {
                    staged.displayType = DISPLAY_OLED;
                } else {
                    staged.displayType = DISPLAY_NONE;
                }
            }
        }

        // LCD I2C address (hex string like "0x27"). Same rule as
        // `DISPLAY ADDRESS` (#590): only a PCF8574/PCF8574A backpack address is
        // taken; anything else keeps the current address, with a note, rather
        // than failing the import (same posture as "format" above).
        if (display["address"].isNull() == false) {
            const char* addr = display["address"];
            char* end = nullptr;
            const long value = addr ? strtol(addr, &end, 16) : -1;  // base 16 accepts a 0x prefix
            if (addr && end != addr && *end == '\0' && isValidLcdI2CAddress(value)) {
                staged.lcdI2CAddress = (uint8_t)value;
            } else {
                msg.control.println(JSON_FSTR(kDisplayAddrIgnored));
            }
        }

        // Default units
        if (display["defaultUnits"].isNull() == false) {
            JsonObject units = display["defaultUnits"];

            if (units["temperature"].isNull() == false) {
                uint8_t idx = findUnitsIndexByNameOrSymbol(units["temperature"]);
                if (unitsMatchMeasurement(idx, MEASURE_TEMPERATURE)) staged.defaultTempUnits = idx;
            }
            if (units["pressure"].isNull() == false) {
                uint8_t idx = findUnitsIndexByNameOrSymbol(units["pressure"]);
                if (unitsMatchMeasurement(idx, MEASURE_PRESSURE)) staged.defaultPressUnits = idx;
            }
            if (units["elevation"].isNull() == false) {
                uint8_t idx = findUnitsIndexByNameOrSymbol(units["elevation"]);
                if (unitsMatchMeasurement(idx, MEASURE_ELEVATION)) staged.defaultElevUnits = idx;
            }
            if (units["speed"].isNull() == false) {
                uint8_t idx = findUnitsIndexByNameOrSymbol(units["speed"]);
                if (unitsMatchMeasurement(idx, MEASURE_SPEED)) staged.defaultSpeedUnits = idx;
            }
        }
    }

    // "timing" (sensorRead/alarmCheck/lcdUpdate, #601) and
    // display.updateInterval (DISPLAY INTERVAL) are retired and ignored,
    // silently, because every older dump carries them. The LCD redraws on
    // LCD_UPDATE_INTERVAL_MS (config.h); the stored value was never read.

    // Physical constants
    if (systemObj["constants"].isNull() == false) {
        JsonObject constants = systemObj["constants"];
        // Out of range keeps the current value, as an unknown unit name does above.
        if (constants["seaLevelPressure"].isNull() == false) {
            const float hPa = constants["seaLevelPressure"];
            if (seaLevelPressureValid(hPa)) staged.seaLevelPressure = hPa;
        }
    }

    // Bus Configuration (with backward-compatible defaults)
    if (systemObj["buses"].isNull() == false) {
        JsonObject buses = systemObj["buses"];
        // An ABSENT key means "this config says nothing about the bus", which is
        // 0xFF (no selection) — NOT DEFAULT_I2C_BUS. Writing the platform
        // default here would launder the very guess #377 deletes into a STORED
        // user statement: every subsequent boot would read active_i2c == 0 as
        // "the user picked Wire" and claim 18/19 for good. Export writes the
        // real value (255 included), so a round trip is unaffected; only a
        // config that never mentioned the bus changes meaning, and it changes to
        // what a fresh board now does.
        staged.buses.active_i2c = buses["i2c"] | 0xFF;
        staged.buses.i2c_clock = buses["i2cClock"] | DEFAULT_I2C_CLOCK;
        staged.buses.active_spi = buses["spi"] | 0xFF;
        staged.buses.spi_clock = buses["spiClock"] | DEFAULT_SPI_CLOCK;

        // CAN (#612 phase 2a): one entry per bus. The array replaces the
        // settings: a bus it doesn't list is off, and CAN input reads the bus
        // marked "input" (none: off). A bad mode or rate keeps the current one.
        if (buses["can"].is<JsonArray>()) {
            for (uint8_t b = 0; b < CAN_BUS_SLOTS; b++) staged.buses.can[b].mode = CAN_BUS_OFF;
            staged.buses.canInputBus = 0xFF;
            for (JsonObject c : buses["can"].as<JsonArray>()) {
                const int b = c["bus"] | -1;
                if (b < 0 || b >= NUM_CAN_BUSES || b >= CAN_BUS_SLOTS) continue;
                const char* mode = c["mode"] | "";
                if (strcasecmp(mode, "NORMAL") == 0)      staged.buses.can[b].mode = CAN_BUS_NORMAL;
                else if (strcasecmp(mode, "LISTEN") == 0) staged.buses.can[b].mode = CAN_BUS_LISTEN;
                else if (strcasecmp(mode, "OFF") == 0)    staged.buses.can[b].mode = CAN_BUS_OFF;
                else staged.buses.can[b].mode = systemConfig.buses.can[b].mode;
                const uint8_t idx = canBaudIndexOf(c["baudrate"] | 0UL);
                if (idx != 0xFF) staged.buses.can[b].baudIndex = idx;
                if ((c["input"] | false) && staged.buses.can[b].mode != CAN_BUS_OFF) staged.buses.canInputBus = (uint8_t)b;
            }
        }
    } else {
        // No buses object: it states no I2C/SPI selection, so it gets none,
        // the answer resetSystemConfig() gives (#377). CAN is left as it is.
        staged.buses.active_i2c = 0xFF;
        staged.buses.i2c_clock = DEFAULT_I2C_CLOCK;
        staged.buses.active_spi = 0xFF;
        staged.buses.spi_clock = DEFAULT_SPI_CLOCK;
    }

    // Serial ports (#561). Applied to the stored config only, so they take
    // effect at the next boot, like BUS SERIAL <n> BAUDRATE / FRAMED; starting
    // or stopping a port mid-import would mean doing #558's ordering here. A
    // port that carries this command link keeps its settings (see
    // serialPortCarriesCommands). Absent keys keep the current value.
    uint8_t serialKeptMask = 0;
    bool serialChanged = false;
#if NUM_SERIAL_PORTS > 0
    if (systemObj["serial"]["ports"].is<JsonArray>()) {
        for (JsonObject port : systemObj["serial"]["ports"].as<JsonArray>()) {
            const int portNum = port["port"] | 0;     // range-check before narrowing: 257 is not Serial1
            if (portNum < 1 || portNum > NUM_SERIAL_PORTS) continue;
            const uint8_t id = (uint8_t)portNum;
            const uint8_t bit = (uint8_t)(1u << (id - 1));
            if (serialPortCarriesCommands(id)) {
                // Worth a NOTE only if the file wanted something else for it.
                const bool en = (staged.serial.enabled_mask & bit) != 0;
                const bool fr = (staged.serial.framed_enabled_mask & bit) != 0;
                if ((port["enabled"].is<bool>() && port["enabled"].as<bool>() != en) ||
                    (port["framed"].is<bool>() && port["framed"].as<bool>() != fr) ||
                    (port["baudrate"].is<uint32_t>() &&
                     port["baudrate"].as<uint32_t>() != getBaudRateFromIndex(staged.serial.baudrate_index[id - 1]))) {
                    serialKeptMask |= bit;
                }
                continue;
            }
            if (port["enabled"].is<bool>()) {
                if (port["enabled"].as<bool>()) staged.serial.enabled_mask |= bit;
                else staged.serial.enabled_mask &= (uint8_t)~bit;
            }
            if (port["baudrate"].is<uint32_t>()) {
                const uint32_t baud = port["baudrate"];
                const uint8_t idx = getBaudRateIndex(baud);
                if (getBaudRateFromIndex(idx) == baud) staged.serial.baudrate_index[id - 1] = idx;
            }
#if INCLUDE_FRAMED_TRANSPORT
            if (port["framed"].is<bool>()) {
                if (port["framed"].as<bool>()) staged.serial.framed_enabled_mask |= bit;
                else staged.serial.framed_enabled_mask &= (uint8_t)~bit;
            }
#endif
        }
        serialChanged = memcmp(&staged.serial, &systemConfig.serial, sizeof(staged.serial)) != 0;
    }
#endif

    // The outputs you add (#612 phase 2b-1): the array replaces the stored
    // table, ids kept. A record this board can't hold (id >= MAX_OUTPUTS, a
    // duplicate id, an unbuilt format, a transport the format can't use) is
    // skipped with a NOTE. An enabled one that couldn't run here, judged by
    // this file's own buses and serial ports (one format per stream, the
    // console's ports, the framed link, an off port or CAN bus), is imported
    // switched off with a NOTE, so the rest of the file still restores.
    JsonVariant outputsVar = systemObj["outputs"];
    const bool outputsPresent = outputsVar.is<JsonArray>();
    OutputRecord stagedOut[MAX_OUTPUTS];
    memcpy(stagedOut, outputRecords, sizeof(stagedOut));
    if (outputsPresent) {
        memset(stagedOut, 0, sizeof(stagedOut));
        for (JsonObject o : outputsVar.as<JsonArray>()) {
            const int id = o["id"] | -1;
            const OutputFormat fmt = parseOutputFormat(o["format"] | "");
            uint8_t t = OT_NONE;
            const bool tOk = parseOutputTransport(o["transport"] | "", &t);
            if (id < 0 || id >= MAX_OUTPUTS || stagedOut[id].format != FMT_NONE || fmt == FMT_NONE ||
                !outputFormatBuilt(fmt) || !tOk || !outputTransportAllowed(fmt, t)) {
                msg.control.print(F("NOTE: outputs: id "));
                msg.control.print(id);
                msg.control.print(F(" ("));
                msg.control.print(o["format"] | "?");
                msg.control.println(F(") is not an output this board can hold; skipped"));
                continue;
            }
            OutputRecord& r = stagedOut[id];
            r.id = (uint8_t)id;
            r.format = fmt;
            r.transport = t;
            r.enabled = (o["enabled"] | false) ? 1 : 0;
            r.interval = outputIntervalDefault(fmt, t);
            if (outputFormatPaced(fmt) && o["interval"].isNull() == false) {
                const long ms = o["interval"] | -1L;
                if (outputIntervalValid(fmt, t, ms)) {
                    r.interval = (uint16_t)ms;
                } else {
                    msg.control.print(JSON_FSTR(kIntervalNoteA));
                    msg.control.print(id);
                    msg.control.println(JSON_FSTR(kIntervalNoteIgnored));
                }
            }
#if INCLUDE_SD_LOGGING
            if (fmt == FMT_CSV && t == OT_SD && o["csv"].isNull() == false) {
                SdLogFormat layout;
                if (parseSdLogFormat(o["csv"] | "", layout)) r.option = (uint8_t)layout;
            }
#endif
        }
    }
    // One per kind (one OBD, one CSV on SD, one ELM327 per transport kind):
    // the lowest id keeps it, as the EEPROM load does
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        OutputRecord& r = stagedOut[id];
        if (r.format == FMT_NONE) continue;
        if (outputSingletonHolder(id, (OutputFormat)r.format, r.transport, stagedOut) == OUT_ID_NONE) continue;
        msg.control.print(F("NOTE: outputs: id "));
        msg.control.print(id);
        msg.control.println(F(" is a second output of a one-per-kind format; skipped"));
        memset(&r, 0, sizeof(r));
    }
    // Every enabled output in the staged table, against the staged config.
    // Also covers a file with no outputs array that moves a bus or a port
    // from under an existing output. In id order, each against the ones
    // already admitted, so where two conflict the lower id stays on (as for
    // one-per-kind outputs above)
    OutputRecord admitted[MAX_OUTPUTS];
    memcpy(admitted, stagedOut, sizeof(admitted));
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) admitted[id].enabled = 0;
    for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
        OutputRecord& r = stagedOut[id];
        if (r.format == FMT_NONE || !r.enabled) continue;
        const __FlashStringHelper* why = outputAdmitReason(id, (OutputFormat)r.format, r.transport, staged, admitted);
        const uint8_t port = outputTransportSerialPort(r.transport);
        if (!why && port != 0xFF && !(staged.serial.enabled_mask & (1u << (port - 1)))) why = F("needs its serial port enabled");
#if INCLUDE_CAN
        const uint8_t bus = outputTransportCanBus(r.transport);
        if (!why && r.format == FMT_OBD && bus != 0xFF && staged.buses.can[bus].mode != CAN_BUS_NORMAL) {
            why = F("needs its CAN bus on and not LISTEN");
        }
#endif
        if (!why) { admitted[id].enabled = 1; continue; }
        r.enabled = 0;
        msg.control.print(F("NOTE: outputs: id "));
        msg.control.print(id);
        msg.control.print(F(" imported off: "));
        msg.control.println(why);
    }

    // The log's levels and tags (#561). Levels by name; an unreadable value
    // keeps the current one. Its transports are export-only, like the console's.
    const bool logFilterPresent = systemObj["log"].is<JsonObject>();
    if (logFilterPresent) {
        JsonObject lf = systemObj["log"];
        uint8_t lvl = parseLogLevelName(lf["consoleLevel"]);
        if (lvl != 0xFF) staged.logFilter.console_level = lvl;
        lvl = parseLogLevelName(lf["debugLevel"]);
        if (lvl != 0xFF) staged.logFilter.debug_level = lvl;
        const char* tags = lf["enabledTags"];
        // strtoul alone would take "-1" (all tags), a leading space, and wrap or
        // saturate an out-of-range value; insist on a plain hex number that fits.
        if (tags && isxdigit((unsigned char)tags[0])) {
            char* end = nullptr;
            errno = 0;
            const unsigned long v = strtoul(tags, &end, 16);   // accepts the "0x" prefix
            if (end != tags && *end == '\0' && errno == 0 && v <= 0xFFFFFFFFUL) {
                staged.logFilter.enabledTags = (uint32_t)v;
            }
        }
    }

    // ---- COMMIT PHASE ----------------------------------------------------
    // Nothing above this line may fail past this point; one write.
    systemConfig = staged;
    if (outputsPresent) {
        // Without the file's own inputs, a route bit can't keep meaning the
        // output it used to: an id that changed format or transport kind, or
        // is new, is routed to every input (as OUTPUT ADD does); a removed one
        // is cleared (as OUTPUT DEL does). With inputs, their routeMask rules.
        for (uint8_t id = 0; id < MAX_OUTPUTS; id++) {
            const OutputRecord& was = outputRecords[id];
            const OutputRecord& now = stagedOut[id];
            const bool same = was.format == now.format &&
                              outputTransportKind(was.transport) == outputTransportKind(now.transport);
            if (same || importHasInputs) continue;
            for (uint8_t i = 0; i < MAX_INPUTS; i++) {
                if (inputs[i].pin == 0xFF) continue;
                if (now.format != FMT_NONE) inputs[i].routeMask |= (uint8_t)(1u << id);
                else                        inputs[i].routeMask &= (uint8_t)~(1u << id);
            }
        }
        memcpy(outputRecords, stagedOut, sizeof(stagedOut));
    }
    if (logFilterPresent) applyLogFilterConfig();
    // The live outputs follow the committed records, as OUTPUT <id>
    // ENABLE/DISABLE would (until #610 that waited for a reboot). Here, not at
    // the end of the whole load: once systemConfig commits, the rest of it is
    // live too and a SAVE persists it, so a later stage failing must not leave
    // the outputs alone out of step. A file refused before this commit (board
    // fit, bad JSON, a system-block error) touches nothing. Buses first: OBD
    // attaches to a bus that is already in its new mode (#612).
#if INCLUDE_CAN
    syncCanToConfig();
#endif
    syncOutputsToConfig();
#if NUM_SERIAL_PORTS > 0
    for (uint8_t id = 1; id <= NUM_SERIAL_PORTS; id++) {
        if (!(serialKeptMask & (1u << (id - 1)))) continue;
        msg.control.print(JSON_FSTR(kSerialKept1));
        msg.control.print(id);
        msg.control.println(JSON_FSTR(kSerialKept2));
    }
    if (serialChanged) {
        msg.control.println(JSON_FSTR(kSerialReboot));
    }
#endif
    return true;
}

#if INCLUDE_RULES_ENGINE
// Replace the Rule table from a JSON array. Records pack into slots in array
// order, preserving each record's stable `id` (Actions reference rules by id).
// Records that fail validation are skipped. Re-arming is the caller's job.
FLASHMEM void importRulesFromJSON(JsonArrayConst rulesArray) {
    for (int i = 0; i < MAX_RULES; i++) rules[i].id = RULES_ID_NONE;
    int slot = 0;
    for (JsonObjectConst ruleObj : rulesArray) {
        if (slot >= MAX_RULES) {
            msg.debug.warn(TAG_JSON, "rule table full (%d); extra rules dropped", MAX_RULES);
            break;
        }
        Rule r{};
        if (rulesImportRule(ruleObj, r)) {
            rules[slot++] = r;
        } else {
            msg.debug.warn(TAG_JSON, "skipped invalid rule record");
        }
    }
}

FLASHMEM void importActionsFromJSON(JsonArrayConst actionsArray) {
    for (int i = 0; i < MAX_ACTIONS; i++) actions[i].id = RULES_ID_NONE;
    for (int i = 0; i < MAX_ACTION_LINKS; i++) actionLinks[i].actionId = RULES_ID_NONE;
    int slot = 0;
    for (JsonObjectConst actionObj : actionsArray) {
        if (slot >= MAX_ACTIONS) {
            msg.debug.warn(TAG_JSON, "action table full (%d); extra actions dropped", MAX_ACTIONS);
            break;
        }
        Action a{};
        uint8_t extra[MAX_ACTION_LINKS];
        uint8_t nExtra = 0;
        if (rulesImportAction(actionObj, a, extra, MAX_ACTION_LINKS, &nExtra)) {
            actions[slot++] = a;
            // 2nd+ memberships need the action in the table first (they are keyed
            // by its id), so add them after the record lands (#343).
            for (uint8_t e = 0; e < nExtra; e++) {
                if (!rulesActionAddRule(a.id, extra[e])) {
                    msg.debug.warn(TAG_JSON, "action #%u: rule link table full; rule #%u dropped",
                                   a.id, extra[e]);
                }
            }
        } else {
            msg.debug.warn(TAG_JSON, "skipped invalid action record");
        }
    }
}
#endif // INCLUDE_RULES_ENGINE

// Load configuration from JSON string
// Every physical pin the file uses must exist on this board, and an input the
// file labels A<n> must land on this board's A<n> (#567). Pin numbers are
// board pin numbers: a dump from a Teensy 4.1 names pins a 4.0 lacks, and its
// A14 (pin 38) is a plain digital pin on the 4.0. Judged before anything is
// written, so a refusal changes nothing. A dump from this board always fits;
// moving a config to another board is a migration, which the clients do,
// where someone can see the mapping and confirm it. Each refusal line starts
// "ERROR: Pin <n>" so a client can read it.
static const char kFitPin[] PROGMEM = "ERROR: Pin ";
static const char kFitInput[] PROGMEM = " (input)";
static const char kFitAction[] PROGMEM = " (action ";
static const char kFitMissing[] PROGMEM = " does not exist on this board";
static const char kFitInFile[] PROGMEM = " is ";
static const char kFitButHere[] PROGMEM = " in the file but ";
static const char kFitOnBoard[] PROGMEM = " on this board";
static const char kFitFrom[] PROGMEM = "NOTE: The file is from ";
static const char kFitThisBoard[] PROGMEM = "; this board is ";
static const char kFitRefused[] PROGMEM = "ERROR: Config does not fit this board; nothing was changed";

FLASHMEM static bool configFitsBoard(JsonDocument& doc) {
    bool fits = true;
    char here[8];
    for (JsonVariantConst v : doc["inputs"].as<JsonArrayConst>()) {
        // A pin that isn't a whole number 0..255 (300, "A2") is a hand edit
        // the import would read as pin 0: it fits nowhere.
        const bool readable = v["pin"].is<uint8_t>();
        const uint8_t pin = v["pin"];
        if (readable && pin > PIN_PHYSICAL_MAX) continue;   // CAN:/I2C: virtual pins
        const char* label = v["pinLabel"] | "";
        const bool exists = readable && boardHasPin(pin);
        formatPinLabel(pin, here, sizeof(here));
        if (exists && (label[0] != 'A' || strcmp(label, here) == 0)) continue;
        fits = false;
        msg.control.print(JSON_FSTR(kFitPin));
        serializeJson(v["pin"], msg.control);
        msg.control.print(JSON_FSTR(kFitInput));
        if (!exists) {
            msg.control.println(JSON_FSTR(kFitMissing));
        } else {
            msg.control.print(JSON_FSTR(kFitInFile));
            msg.control.print(label);
            msg.control.print(JSON_FSTR(kFitButHere));
            msg.control.print(here);
            msg.control.println(JSON_FSTR(kFitOnBoard));
        }
    }
#if INCLUDE_RULES_ENGINE
    // Only a RELAY's target is a pin; BEEPER/LED/DISPLAY name a channel.
    for (JsonVariantConst v : doc["actions"].as<JsonArrayConst>()) {
        if (v["target"].isNull() || strcasecmp(v["type"] | "", "RELAY") != 0) continue;
        if (v["target"].is<uint8_t>() && boardHasPin(v["target"].as<uint8_t>())) continue;
        fits = false;
        msg.control.print(JSON_FSTR(kFitPin));
        serializeJson(v["target"], msg.control);
        msg.control.print(JSON_FSTR(kFitAction));
        msg.control.print(v["id"] | 0);
        msg.control.print(')');
        msg.control.println(JSON_FSTR(kFitMissing));
    }
#endif
    // Said whether or not the pins fit: a file from another board may be on
    // the wrong copper even where every pin number exists.
    const char* from = doc["firmware"]["platform"] | (const char*)nullptr;
    if (from && strcmp(from, boardName()) != 0) {
        msg.control.print(JSON_FSTR(kFitFrom));
        msg.control.print(from);
        msg.control.print(JSON_FSTR(kFitThisBoard));
        msg.control.println(boardName());
    }
    if (!fits) msg.control.println(JSON_FSTR(kFitRefused));
    return fits;
}

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

    if (!configFitsBoard(doc)) return false;

    // Past validation, the apply below mutates the live config — tell BLE
    // clients (status-frame extension, #455). JSON IMPORT's handler
    // returns before its payload arrives, so the dispatcher's own bump fires
    // too early to cover it.
    configGenerationBump();
    obdQuery_markStale();   // routes and the OBD outputs change too, not only inputs

    // Import system config
    if (doc["system"].isNull() == false) {
        JsonObject system = doc["system"];
        if (!importSystemConfigFromJSON(system, doc["inputs"].isNull() == false)) {
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

#if INCLUDE_RULES_ENGINE
    // Import Rules + Actions after inputs (rules reference input pins; the
    // re-arm below evaluates against live inputs). Each table is replaced only
    // if its array is present, then both are re-armed once — boot-load semantics
    // (#251): rulesManagerInit arms rule contexts, rulesActionsInit drives
    // the target pins to their initial state.
    // Replacing the inputs (#562) can orphan a rule, so it re-arms the engine too.
    bool rulesChanged = doc["inputs"].isNull() == false;
    if (doc["rules"].isNull() == false) {
        importRulesFromJSON(doc["rules"].as<JsonArrayConst>());
        rulesChanged = true;
    }
    if (doc["actions"].isNull() == false) {
        importActionsFromJSON(doc["actions"].as<JsonArrayConst>());
        rulesChanged = true;
    }
    if (rulesChanged) {
        // Referential integrity: an Action may now reference a Rule id absent
        // from the (possibly only partially re-imported) rule table — a
        // rules-only / actions-only payload, or a hand-edited config. The shared
        // sweep drops those references (and any dangling link rows, #343),
        // promoting a surviving member where it can, before re-arm — so the
        // engine never evaluates a dangling ref.
        rulesSweepActionRules();
        rulesManagerInit(millis());
        rulesActionsInit();
        rulesSyncActionPins();  // re-claim imported action pins (registry already populated)
    }
#endif

    // The OBD-II PID table points into inputs[], whose slots this import may
    // have rearranged; boot and SAVE rebuild it, and so must an import (#562).
    if (doc["inputs"].isNull() == false) obdQuery_buildLookupTable();

    msg.control.print(F("Successfully loaded config (schema v"));
    msg.control.print(schemaVer);
    msg.control.println(F(")"));

    return true;
}

#if SUPPORTS_SD

// Save configuration to SD card
bool saveConfigToSD(const char* filename) {
    msg.debug.info(TAG_SD, "Starting save operation");

    // Check if SD card is initialized (done in main setup)
    if (!isSDInitialized()) {
        msg.control.println(F("ERROR: SD card not initialized"));
        msg.debug.warn(TAG_SD, "SD card not available");
        return false;
    }

    // Every SD call below blocks for seconds on a card that has gone away (the
    // #331 trap), and the watchdog runs in CONFIG too (8 s, #541). Hold one
    // scope across the whole operation; scopes nest and only ever widen, so
    // the helpers' own SD scopes can't narrow it.
    WatchdogScope window(SD_WDT_WINDOW_MS, SD_WDT_TAG);

    if (!sdMediaPresent()) {
        msg.control.println(F("ERROR: SD card removed"));
        msg.debug.warn(TAG_SD, "SD card no longer present");
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

    // See saveConfigToSD(): blocking card I/O from CONFIG mode (#331).
    WatchdogScope window(SD_WDT_WINDOW_MS, SD_WDT_TAG);

    if (!sdMediaPresent()) {
        msg.control.println(F("ERROR: SD card removed"));
        msg.debug.warn(TAG_SD, "SD card no longer present");
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
        return false;
    }

    msg.debug.debug(TAG_SD, "File opened successfully");
    msg.debug.debug(TAG_SD, "Reading file...");

    // Read entire file into string
    String jsonString;
    size_t bytesRead = 0;
    while (configFile.available()) {
        jsonString += (char)configFile.read();
        // A multi-KB file read one byte at a time into a String is a long
        // blocking path in its own right, window or not.
        if ((++bytesRead & 0xFF) == 0) watchdogReset();
    }

    msg.debug.debug(TAG_SD, "Read complete: %u bytes total", bytesRead);
    msg.debug.debug(TAG_SD, "Closing file...");
    configFile.close();
    msg.debug.debug(TAG_SD, "File closed");

    msg.debug.debug(TAG_SD, "Parsing JSON...");

    // Load configuration
    bool success = loadConfigFromJSON(jsonString.c_str());

    msg.debug.debug(TAG_SD, "JSON parsing complete");


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

#ifdef INCLUDE_USB_STORAGE
    else if (strcmp(destination, "USB") == 0) {
        // Future: USB storage implementation
        msg.control.println(F("ERROR: USB storage not yet implemented"));
        return false;
    }
#endif

#ifdef INCLUDE_HTTP_STORAGE
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
#ifdef INCLUDE_USB_STORAGE
        msg.control.println(F("  Conditional: USB (if INCLUDE_USB_STORAGE defined)"));
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

#ifdef INCLUDE_USB_STORAGE
    else if (strcmp(destination, "USB") == 0) {
        // Future: USB storage implementation
        msg.control.println(F("ERROR: USB storage not yet implemented"));
        return false;
    }
#endif

#ifdef INCLUDE_HTTP_STORAGE
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
#ifdef INCLUDE_USB_STORAGE
        msg.control.println(F("  Conditional: USB (if INCLUDE_USB_STORAGE defined)"));
#endif
        return false;
    }
}

#endif // SUPPORTS_SD

#endif // SUPPORTS_JSON_CONFIG
