/*
 * cmd_set.cpp - SET command (configure inputs)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../inputs/input_manager.h"
#include "input_display.h"
#include "../inputs/input.h"
#include "../inputs/calibration_apply.h"
#include "../inputs/measurement_compat.h"
#include "serial_config.h"
#include "../version.h"
#include "../core/system_mode.h"
#include "../storage/system_config.h"
#include "../storage/json_config.h"
#include "../core/message_router.h"
#include "../core/message_api.h"
#include "../core/log_filter.h"
#include "../core/log_tags.h"
#include "../sensors/units_registry.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../core/platform.h"
#include "../storage/json_registry.h"
#include "../drivers/bus_manager.h"
#include "../hal/bus_defaults.h"
#include "../drivers/serial_manager.h"
#include "../pinctrl/pin_registry.h"
#include "../outputs/output_base.h"
#include "../outputs/obd_query.h"
#include "../outputs/display/display_manager.h"
#include "../rules/rules_engine.h"    // rulesParseWarnToken (#428)
#include "../rules/rules_manager.h"   // §11.5 seed default Rule from application
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
#include <string.h>
#include <ctype.h>

// CLI handlers are cold — keep them in flash, not Teensy ITCM (RAM1).
// FLASHMEM is a Teensy core macro — no-op elsewhere.
#ifndef FLASHMEM
#define FLASHMEM
#endif

//=============================================================================
// SET command — leaves and dispatch
//
// cmd_set parses <pin> once, then dispatches on <field> to one of these leaf
// handlers. Pin is threaded through because parsePin() for the keywords "CAN"
// and "I2C" allocates virtual slots via internal counters and is not
// idempotent — the leaves cannot safely re-parse it.
//=============================================================================

// True iff `s` looks like a numeric PID literal — decimal digits, or hex
// prefixed with 0x / 0X. Used to gate the `SET CAN <pid>` prelude so that
// typos like `SET CAN APPLICATION CHT` fall through to field dispatch
// instead of allocating a phantom CAN slot with PID 0.
static bool looksLikePidLiteral(const char* s) {
    if (!s || !*s) return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        if (!s[2]) return false;
        for (const char* p = s + 2; *p; p++) {
            if (!isxdigit((unsigned char)*p)) return false;
        }
        return true;
    }
    for (const char* p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    return true;
}

typedef int (*SetFieldHandler)(uint8_t pin, int argc, const char* const* argv);
struct SetField {
    const char* token;
    SetFieldHandler handler;
    const char* help;          // PROGMEM-resident one-line help; nullptr to hide
};

// Calibration-profile leaf signature. Reached via `SET <pin> CAL <profile>` so
// the leaf needs to know where its own arguments start in argv (argStart=4
// for the canonical shape: argv[0]=SET, argv[1]=<pin>, argv[2]=CAL,
// argv[3]=<profile>, argv[4..]=profile args). Centralising this offset means
// the leaf bodies stay free of magic indices.
typedef int (*CalProfileHandler)(uint8_t pin, int argc, const char* const* argv,
                                  int argStart);
struct CalProfile {
    const char* token;          // PROGMEM-resident profile keyword
    CalProfileHandler handler;
    const char* help;           // PROGMEM-resident multi-line help (nullptr ok)
    uint8_t type;               // CalibrationType the profile writes; CAL_NONE = any (BIAS, PRESET)
};

#if INCLUDE_RULES_ENGINE
// After setInputApplication(): seed the input's default Rule from the app
// template on a fresh input, re-seed on an application CHANGE, keep a
// customized Rule when the same app is re-applied (§11.5). Shared by
// `SET <pin> APPLICATION` and the combined `SET <pin> <app> <sensor>` form,
// which used to skip it and left the input with no default alarm (#487).
static FLASHMEM void seedRulesForApplication(uint8_t pin, uint8_t appIndex, uint8_t oldApp) {
    if (!rulesRuleExistsForInput(pin)) {
        // Fresh input: seed a canonical default Rule from the app template (§11.5).
        uint8_t ruleId = rulesSeedRuleForInput(pin, appIndex, millis());
        if (ruleId != RULES_ID_NONE) {
            msg.control.print(F("  Seeded Rule #"));
            msg.control.print(ruleId);
            msg.control.println(F(" from application defaults (RULE LIST to view)"));
        } else if (rulesRuleFreeSlot() < 0 || rulesRuleAllocId() == RULES_ID_NONE) {
            // Distinguish the genuine table/id-space-full case from the benign
            // "this application has no default alarm band" (the other reason
            // seeding returns NONE). With caps at the 254-id ceiling this is
            // effectively unreachable, but the original silent-drop bug (#296)
            // is exactly this path — surface it loudly rather than leave the
            // input unmonitored without a word.
            msg.control.println(F("  WARNING: Rule table full — input configured WITHOUT an alarm"));
            msg.control.println(F("  (free a Rule with RULE DEL, then SET <pin> ALARM <min> <max>)"));
        }
    } else if (appIndex != oldApp) {
        // App CHANGED under existing Rule(s): their thresholds are now stale.
        // Re-seed them from the new application's defaults, or warn if the new
        // app has none (the old thresholds would otherwise silently mis-alarm).
        uint8_t n = rulesReseedRulesForInput(pin, millis());
        if (n > 0) {
            msg.control.print(F("  Application changed - re-seeded "));
            msg.control.print(n);
            msg.control.println(F(" Rule(s) from new defaults (RULE LIST to view)"));
        } else {
            msg.control.println(F("  WARNING: existing Rule(s) on this input keep their old"));
            msg.control.println(F("  thresholds (new application has no default band) - review"));
            msg.control.println(F("  with RULE LIST, then RULE RESET / RULE DEL as needed"));
        }
    }
    // else: same app re-applied — preserve existing Rule (§11.5).
}
#endif

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
#if INCLUDE_RULES_ENGINE
    // Capture the prior application so we can tell a fresh assignment from a
    // CHANGE — re-applying the SAME app preserves a customized Rule (§11.5), but
    // changing the measurement type invalidates the old thresholds.
    Input* prev = getInputByPin(pin);
    const uint8_t oldApp = prev ? prev->applicationIndex : 0;
#endif
    if (setInputApplication(pin, appIndex)) {
        printInputFieldSet(argv[1], F("application"), argv[3]);
#if INCLUDE_RULES_ENGINE
        seedRulesForApplication(pin, appIndex, oldApp);
#endif
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
        // getSensorNameByIndex returns a PROGMEM-resident string (see
        // sensor_helpers.h: READ_SENSOR_NAME does pgm_read_ptr on a flash
        // pointer), so the __FlashStringHelper* cast prints correctly on AVR.
        const char* sensorName = getSensorNameByIndex(sensorIndex);
        if (sensorName) {
            printInputFieldSet(argv[1], F("sensor"),
                               (const __FlashStringHelper*)sensorName);
        } else {
            printInputFieldSet(argv[1], F("sensor"), argv[argc >= 5 ? 4 : 3]);
        }
        return 0;
    }
    return 1;
}

// SET <pin> NAME <full_name>
// 32-char display label. Routes through requireInput so an unconfigured pin
// produces the standard error (resolves #205 — silent failure on unknown pin).
static int set_name(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: NAME requires a name string"));
        return 1;
    }
    if (!requireInput(pin)) return 1;
    setInputDisplayName(pin, argv[3]);
    printInputFieldSet(argv[1], F("name"), argv[3]);
    return 0;
}

// SET <pin> ABBR <abbreviation>
// 8-char display abbreviation. Routes through requireInput (#205).
static int set_abbr(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: ABBR requires an abbreviation string"));
        return 1;
    }
    if (!requireInput(pin)) return 1;
    setInputAbbr(pin, argv[3]);
    printInputFieldSet(argv[1], F("abbreviation"), argv[3]);
    return 0;
}

// SET <pin> INPUT ENABLE|DISABLE
static int set_input(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: INPUT requires ENABLE or DISABLE"));
        return 1;
    }
    bool enable;
    if (streq_P(argv[3], PSTR("ENABLE"))) {
        enable = true;
    } else if (streq_P(argv[3], PSTR("DISABLE"))) {
        enable = false;
    } else {
        msg.control.println(F("ERROR: INPUT requires ENABLE or DISABLE"));
        return 1;
    }
    if (!requireInput(pin)) return 1;
    enableInput(pin, enable);
    printInputFlag(argv[1], F("input"), enable);
    return 0;
}

// SET <pin> DISPLAY ENABLE|DISABLE
static int set_display(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: DISPLAY requires ENABLE or DISABLE"));
        return 1;
    }
    if (streq_P(argv[3], PSTR("ENABLE"))) {
        if (enableInputDisplay(pin, true)) {
            printInputFlag(argv[1], F("display"), true);
            return 0;
        }
        return 1;
    }
    if (streq_P(argv[3], PSTR("DISABLE"))) {
        if (enableInputDisplay(pin, false)) {
            printInputFlag(argv[1], F("display"), false);
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
    Input* input = requireInput(pin);
    if (!input) return 1;
    // Strict lookup: getUnitsIndexByName() maps a typo to CELSIUS (#458).
    uint8_t unitsIndex = findUnitsIndexByName(argv[3]);
    if (unitsIndex == UNITS_NOT_FOUND) {
        msg.control.print(F("ERROR: Unknown units '"));
        msg.control.print(argv[3]);
        msg.control.println(F("'"));
        msg.control.println(F("  Hint: LIST UNITS JSON shows every unit and what it measures"));
        return 1;
    }
    MeasurementType want = inputMeasurementTypeOf(*input);
    if (!setInputUnits(pin, unitsIndex)) {
        MeasurementType got = (MeasurementType)pgm_read_byte(&UNITS_REGISTRY[unitsIndex].measurementType);
        msg.control.print(F("ERROR: "));
        msg.control.print(argv[3]);
        msg.control.print(F(" is a "));
        msg.control.print(measurementTypeName(got));
        msg.control.print(F(" unit; this input measures "));
        msg.control.println(measurementTypeName(want));
        return 1;
    }
    printInputFieldSet(argv[1], F("units"), argv[3]);
    return 0;
}

#if INCLUDE_RULES_ENGINE
// One WARN value token, shared by the SET <pin> ALARM WARN facade below.
// Parse one WARN token via the shared rulesParseWarnToken(); on a bad token
// print the error and return false (#428 — a typo used to become SET 0). Same
// wording as cmd_rule.cpp's copy so both surfaces read identically.
static FLASHMEM bool parseWarnToken(const char* tok, RuleWarnMode& mode, float& val) {
    if (rulesParseWarnToken(tok, mode, val)) return true;
    msg.control.print(F("ERROR: WARN value not a number (or NONE/AUTO): "));
    msg.control.println(tok);
    return false;
}

// Side-specific WARN validation error, mirroring cmd_rule.cpp's checkWarnSide
// wording exactly (the operator sees the same message from either surface).
static FLASHMEM void printWarnSideError(uint8_t condition, bool hiSide, float limitLo, float limitHi,
                                          float warnLo, float warnHi) {
    if (condition == RULE_ABOVE) {
        msg.control.println(F("ERROR: WARN must be below the ABOVE limit"));
    } else if (condition == RULE_BELOW) {
        msg.control.println(F("ERROR: WARN must be above the BELOW limit"));
    } else if (condition == RULE_OUTSIDE_BAND) {
        const float warnVal = hiSide ? warnHi : warnLo;
        const bool inBand = (limitLo < warnVal) && (warnVal < limitHi);
        if (!inBand) {
            msg.control.println(F("ERROR: WARN must be inside the OUTSIDE band"));
        } else {
            msg.control.println(F("ERROR: WARN lo must be < hi"));
        }
    } else {
        msg.control.println(F("ERROR: WARN not supported for this rule's condition"));
    }
}
#endif

// SET <pin> ALARM <min> <max> | DISABLE
// Facade over the input's canonical alarm Rule (severity ALARM, §11.5): upserts
// one input-keyed Rule, preserving its id and any Action links. This is the
// per-input alarm surface the webapp and CLI expose, in three shapes (#312):
//   SET <pin> ALARM <min> <max>   band    → OUTSIDE_BAND
//   SET <pin> ALARM MIN <v>       floor   → BELOW  (alarm when value < v)
//   SET <pin> ALARM MAX <v>       ceiling → ABOVE  (alarm when value > v)
// The general engine is still reachable via RULE ADD for WARNING/INFO/NONE Rules.
static int set_alarm(uint8_t pin, int argc, const char* const* argv) {
#if !INCLUDE_RULES_ENGINE
    (void)pin; (void)argc; (void)argv;
    msg.control.println(F("ERROR: alarms require the rules engine (not built into this firmware)"));
    return 1;
#else
    if (argc < 4) {
        msg.control.println(F("ERROR: usage: SET <pin> ALARM <min> <max> | MIN <v> | MAX <v> | WARN <lo> [<hi>] | DISABLE"));
        return 1;
    }
    if (getInputByPin(pin) == nullptr) {
        msg.control.println(F("ERROR: no input configured on that pin (SET its APPLICATION first)"));
        return 1;
    }

    // SET <pin> ALARM WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>] — edit the warn tier
    // of the input's canonical alarm Rule. Matched BEFORE the MIN/MAX/numeric
    // band branches below (else atof("WARN") silently parses as a band edge,
    // #425 amendment A8).
    if (streq_P(argv[3], PSTR("WARN"))) {
        int alarmSlot = rulesAlarmRuleSlotForInput(pin);
        if (alarmSlot < 0) {
            msg.control.println(F("ERROR: no alarm Rule on this input (set one with SET <pin> ALARM <min> <max> | MIN <v> | MAX <v>)"));
            return 1;
        }
        const Rule existing = rules[alarmSlot];  // copy: for the error report after a rejected write
        const bool needsHi = (existing.condition == RULE_OUTSIDE_BAND);
        if (argc < 5) {
            msg.control.println(needsHi ? F("ERROR: WARN needs <lo> <hi>") : F("ERROR: WARN needs <lo>"));
            return 1;
        }
        RuleWarnMode loMode, hiMode = RULE_WARN_AUTO;
        float lo, hi = 0.0f;
        if (!parseWarnToken(argv[4], loMode, lo)) return 1;
        if (needsHi) {
            if (argc < 6) {
                msg.control.println(F("ERROR: WARN needs <lo> <hi>"));
                return 1;
            }
            if (!parseWarnToken(argv[5], hiMode, hi)) return 1;
        }
        // Fixed arity on both shapes — a trailing token is a typo, not something
        // to swallow silently.
        const int expectedArgc = needsHi ? 6 : 5;
        if (argc > expectedArgc) {
            msg.control.print(F("ERROR: unexpected token: "));
            msg.control.println(argv[expectedArgc]);
            return 1;
        }
        uint8_t badSide = 0xFF;
        if (!rulesSetAlarmWarnForInput(pin, loMode, lo, hiMode, hi, &badSide)) {
            if (badSide == 0xFF) {
                msg.control.println(F("ERROR: no alarm Rule on this input (set one with SET <pin> ALARM <min> <max> | MIN <v> | MAX <v>)"));
            } else {
                printWarnSideError(existing.condition, badSide == 1, existing.limitLo, existing.limitHi, lo, hi);
            }
            return 1;
        }
        msg.control.println(F("Alarm warn updated. SAVE to persist."));
        return 0;
    }

    // SET <pin> ALARM DISABLE|OFF — remove the input's alarm Rule.
    if (streq_P(argv[3], PSTR("DISABLE")) || streq_P(argv[3], PSTR("OFF"))) {
        uint8_t released = 0;
        if (rulesRemoveAlarmRuleForInput(pin, &released)) {
            msg.control.println(F("Alarm removed (Rule deleted). SAVE to persist."));
            // Clearing an alarm doesn't look like an action command, so say it
            // out loud when it just dropped a relay someone was holding (#329).
            if (released) {
                msg.control.print(F("  NOTE: released "));
                msg.control.print(released);
                msg.control.println(F(" driven action(s) that followed it; they are now OFF"));
            }
        } else {
            msg.control.println(F("No alarm Rule on that input - nothing to remove."));
        }
        return 0;
    }

    // Resolve the shape: one-sided MIN/MAX keyword, or a two-number band.
    uint8_t condition;
    float lo, hi = 0.0f;
    const bool isMin = streq_P(argv[3], PSTR("MIN"));
    const bool isMax = streq_P(argv[3], PSTR("MAX"));
    if (isMin || isMax) {
        // SET <pin> ALARM MIN|MAX <v> — one-sided floor/ceiling.
        if (argc < 5) {
            msg.control.println(F("ERROR: ALARM MIN|MAX needs a value"));
            return 1;
        }
        condition = isMin ? RULE_BELOW : RULE_ABOVE;  // floor→BELOW, ceiling→ABOVE
        lo = atof(argv[4]);
    } else {
        // SET <pin> ALARM <min> <max> — two-sided band.
        if (argc < 5) {
            msg.control.println(F("ERROR: ALARM needs <min> <max> (or MIN <v> | MAX <v> | DISABLE)"));
            return 1;
        }
        condition = RULE_OUTSIDE_BAND;
        lo = atof(argv[3]);
        hi = atof(argv[4]);
        if (!(lo < hi)) {
            msg.control.println(F("ERROR: ALARM band requires min < max (use MIN/MAX for one-sided)"));
            return 1;
        }
    }

    bool created = (rulesAlarmRuleSlotForInput(pin) < 0);
    uint8_t warnReset = 0;
    uint8_t id = rulesSetAlarmRuleForInput(pin, condition, lo, hi, millis(), &warnReset);
    if (id == RULES_ID_NONE) {
        msg.control.print(F("ERROR: rule table full ("));
        msg.control.print(MAX_RULES);
        msg.control.println(F(") - free a Rule (RULE LIST / RULE DEL) and retry"));
        return 1;
    }
    msg.control.print(F("Alarm Rule #"));
    msg.control.print(id);
    msg.control.println(created ? F(" created. SAVE to persist.")
                                : F(" updated. SAVE to persist."));
    // A limit/shape move can strand a SET warn edge on the wrong side of the
    // new limit, or (shape change) leave a warn setting on a side the new shape
    // doesn't have — applyAlarmShape() degrades/clears it rather than blocking
    // the edit (#425 amendment A7, #427). Say so, or the operator's earlier
    // explicit warn edge just vanishes with no explanation. Sides are named as
    // the operator set them (min/max), not by storage slot.
    if (warnReset & RULE_WARN_RESET_MIN) {
        msg.control.println(F("  NOTE: warn min reset to AUTO (limit moved past it)"));
    }
    if (warnReset & RULE_WARN_RESET_MAX) {
        msg.control.println(F("  NOTE: warn max reset to AUTO (limit moved past it)"));
    }
    if (warnReset & RULE_WARN_DROPPED_MIN) {
        msg.control.println(F("  NOTE: warn min cleared (a MAX-only alarm has no lower side)"));
    }
    if (warnReset & RULE_WARN_DROPPED_MAX) {
        msg.control.println(F("  NOTE: warn max cleared (a MIN-only alarm has no upper side)"));
    }
    return 0;
#endif
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
    Input* input = requireInput(pin);
    if (!input) return 1;
    // A ratio the read path never applies would be accepted and silently do
    // nothing. 1.0 ("no divider") is always allowed so any input can be cleared.
    if (ratio != 1.0f && !inputHonorsDivider(input)) {
        msg.control.println(F("ERROR: DIVIDER only applies to linear voltage-output sensors"));
        msg.control.println(F("  This input's sensor ignores it (resistive, thermistor or VOLTAGE_DIVIDER)"));
        return 1;
    }
    // A custom linear calibration's voltages are sensor-side and judged
    // against SYSTEM_VOLTAGE / ratio (#467): a larger ratio must not leave the
    // curve above what the pin can see.
    if (!calLinearFitsRatio(input, ratio)) {
        msg.control.print(F("ERROR: This input's calibration reaches "));
        msg.control.print(input->customCalibration.pressureLinear.voltage_max);
        msg.control.println(F("V, which this ratio can't read"));
        msg.control.println(F("  Change the calibration first (SET <pin> CAL ...)"));
        return 1;
    }
    input->divider_ratio = ratio;
    printInputFieldSet(argv[1], F("divider ratio"), ratio, 3);
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

    if (streq_P(argv[3], PSTR("STATUS"))) {
        printInputOutputInfo(pin);
        return 0;
    }

    if (argc < 5) {
        msg.control.println(F("ERROR: OUTPUT requires ENABLE or DISABLE"));
        return 1;
    }

    bool enable;
    if (streq_P(argv[4], PSTR("ENABLE"))) {
        enable = true;
    } else if (streq_P(argv[4], PSTR("DISABLE"))) {
        enable = false;
    } else {
        msg.control.println(F("ERROR: Expected ENABLE or DISABLE"));
        return 1;
    }

    if (streq_P(argv[3], PSTR("ALL"))) {
        Input* input = requireInput(pin);
        if (!input) return 1;
        input->outputMask = enable ? OUTPUT_MASK_ALL_DATA : 0x00;
        printInputFlag(argv[1], F("all data outputs"), enable);
        return 0;
    }

    uint8_t outputId;
    if (streq_P(argv[3], PSTR("CAN"))) {
        outputId = OUTPUT_CAN;
    } else if (streq_P(argv[3], PSTR("REALDASH"))) {
        outputId = OUTPUT_REALDASH;
    } else if (streq_P(argv[3], PSTR("SERIAL"))) {
        outputId = OUTPUT_SERIAL;
    } else if (streq_P(argv[3], PSTR("SD_LOG")) || streq_P(argv[3], PSTR("SD"))) {
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
        msg.control.println(enable ? F(" enabled") : F(" disabled"));
        printSaveReminder();
        return 0;
    }
    msg.control.println(F("ERROR: Input not configured"));
    return 1;
}

// SET <pin> CAL PRESET — clear custom calibration, restore preset.
// Same backend as `CLEAR <pin> CALIBRATION`.
static int set_cal_preset(uint8_t pin, int argc, const char* const* argv,
                          int argStart) {
    (void)argc; (void)argStart;
    if (!clearInputCalibration(pin)) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }
    msg.control.print(F("Cleared custom calibration for pin "));
    msg.control.println(argv[1]);
    msg.control.println(F("Using preset calibration from sensor library"));
    printSaveReminder();
    return 0;
}

// SET <pin> CAL RPM <poles> <ratio> [<mult>] <timeout> <min> <max>
static int set_cal_rpm(uint8_t pin, int argc, const char* const* argv,
                       int argStart) {
    // 5 params -> argc == argStart+5; 6 params (with custom mult) -> argStart+6.
    if (argc != argStart + 5 && argc != argStart + 6) {
        msg.control.println(F("ERROR: RPM requires 5 or 6 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL RPM <poles> <ratio> <timeout> <min> <max>"));
        msg.control.println(F("     or: SET <pin> CAL RPM <poles> <ratio> <mult> <timeout> <min> <max>"));
        msg.control.println(F("  Example: SET 5 CAL RPM 12 3.0 2000 100 8000"));
        msg.control.println(F("       or: SET 5 CAL RPM 12 3.0 1.02 2000 100 8000"));
        return 1;
    }

    Input* input = requireInput(pin);
    if (!input) return 1;

    byte poles;
    float pulley_ratio;
    float calibration_mult = 1.0;
    uint16_t timeout_ms;
    uint16_t min_rpm;
    uint16_t max_rpm;

    int paramStart = argStart;
    bool hasCustomMult = (argc == argStart + 6);

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

    CalApplyStatus st = applyCalRpm(input, poles, pulley_ratio, calibration_mult,
                                    timeout_ms, min_rpm, max_rpm);
    switch (st) {
        case CAL_APPLY_OK:
            break;
        case CAL_APPLY_ERR_POLES:
            msg.control.println(F("ERROR: Poles must be between 2 and 32"));
            return 1;
        case CAL_APPLY_ERR_PULLEY_RATIO:
            msg.control.println(F("ERROR: Pulley ratio must be between 0.5 and 10.0"));
            return 1;
        case CAL_APPLY_ERR_CAL_MULT:
            msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
            return 1;
        case CAL_APPLY_ERR_TIMEOUT:
            msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
            return 1;
        default:
            msg.control.println(F("ERROR: min_rpm must be less than max_rpm"));
            return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> CAL SPEED <ppr> <tire_circ_mm> <ratio> [<mult>] <timeout> <max_speed>
static int set_cal_speed(uint8_t pin, int argc, const char* const* argv,
                         int argStart) {
    // 5 params -> argc == argStart+5; 6 params (with custom mult) -> argStart+6.
    if (argc != argStart + 5 && argc != argStart + 6) {
        msg.control.println(F("ERROR: SPEED requires 5 or 6 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> <timeout> <max_speed>"));
        msg.control.println(F("     or: SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> <mult> <timeout> <max_speed>"));
        msg.control.println(F("  Example: SET 2 CAL SPEED 100 2008 3.73 2000 300"));
        msg.control.println(F("       or: SET 2 CAL SPEED 100 2008 3.73 1.05 2000 300"));
        return 1;
    }

    Input* input = requireInput(pin);
    if (!input) return 1;

    uint8_t pulses_per_rev;
    uint16_t tire_circumference_mm;
    float final_drive_ratio;
    float calibration_mult = 1.0;
    uint16_t timeout_ms;
    uint16_t max_speed_kph;

    int paramStart = argStart;
    bool hasCustomMult = (argc == argStart + 6);

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

    CalApplyStatus st = applyCalSpeed(input, pulses_per_rev, tire_circumference_mm,
                                      final_drive_ratio, calibration_mult, timeout_ms,
                                      max_speed_kph);
    switch (st) {
        case CAL_APPLY_OK:
            break;
        case CAL_APPLY_ERR_SPEED_PPR:
            msg.control.println(F("ERROR: Pulses per rev must be between 1 and 250"));
            return 1;
        case CAL_APPLY_ERR_SPEED_TIRE:
            msg.control.println(F("ERROR: Tire circumference must be between 500 and 5000 mm"));
            return 1;
        case CAL_APPLY_ERR_SPEED_RATIO:
            msg.control.println(F("ERROR: Drive ratio must be between 0.5 and 10.0"));
            return 1;
        case CAL_APPLY_ERR_SPEED_MULT:
            msg.control.println(F("ERROR: Calibration multiplier must be between 0.5 and 2.0"));
            return 1;
        case CAL_APPLY_ERR_SPEED_TIMEOUT:
            msg.control.println(F("ERROR: Timeout must be between 100 and 10000 ms"));
            return 1;
        default:
            msg.control.println(F("ERROR: Max speed must be between 50 and 500 km/h"));
            return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> OBD_PID <0xNN|N>
static int set_obd_pid(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: OBD_PID requires 1 parameter"));
        msg.control.println(F("  Usage: SET <pin> OBD_PID <0xNN|N>"));
        msg.control.println(F("  0x00 removes this input from the OBD-II output table"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    bool isHex = (argv[3][0] == '0' && (argv[3][1] == 'x' || argv[3][1] == 'X'));
    if (isHex && !looksLikePidLiteral(argv[3])) {
        msg.control.println(F("ERROR: OBD_PID requires a numeric or 0xNN hex PID"));
        return 1;
    }

    uint16_t pidVal;
    if (isHex) {
        pidVal = (uint16_t)strtoul(argv[3] + 2, nullptr, 16);
    } else {
        pidVal = (uint16_t)atoi(argv[3]);
    }
    if (pidVal > 0xFF) {
        msg.control.println(F("ERROR: PID must be between 0x00 and 0xFF"));
        return 1;
    }
    uint8_t pid = (uint8_t)pidVal;

    // Default length to 1 if this input has never had an OBD length configured,
    // so buildOBD2Frame's dataBytes==0 check doesn't silently drop it.
    uint8_t length = input->obd2length;
    if (pid != 0 && length == 0) length = 1;

    setInputOBD(pin, pid, length);
    obdQuery_buildLookupTable();

    msg.control.print(F("OBD PID set for pin "));
    msg.control.print(argv[1]);
    msg.control.print(F(": 0x"));
    if (pid < 0x10) msg.control.print('0');
    msg.control.print(pid, HEX);
    msg.control.print(F(", length "));
    msg.control.println(length);
    printSaveReminder();
    return 0;
}

// SET <pin> OBD_LENGTH <1-5>
static int set_obd_length(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: OBD_LENGTH requires 1 parameter"));
        msg.control.println(F("  Usage: SET <pin> OBD_LENGTH <1-5>"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    int length = atoi(argv[3]);
    if (length < 1 || length > 5) {
        msg.control.println(F("ERROR: OBD_LENGTH must be between 1 and 5"));
        return 1;
    }

    setInputOBD(pin, input->obd2pid, (uint8_t)length);
    obdQuery_buildLookupTable();

    msg.control.print(F("OBD response length set for pin "));
    msg.control.print(argv[1]);
    msg.control.print(F(": "));
    msg.control.println(length);
    printSaveReminder();
    return 0;
}

#if INCLUDE_CAN
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
    printSaveReminder();
    return 0;
}
#endif // INCLUDE_CAN

// SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>
static int set_cal_pressure_linear(uint8_t pin, int argc, const char* const* argv,
                                   int argStart) {
    if (argc < argStart + 4) {
        msg.control.println(F("ERROR: PRESSURE_LINEAR requires 4 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>"));
        msg.control.println(F("  Example: SET A1 CAL PRESSURE_LINEAR 0.5 4.5 0.0 7.0"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float vmin = atof(argv[argStart]);
    float vmax = atof(argv[argStart + 1]);
    float pmin = atof(argv[argStart + 2]);
    float pmax = atof(argv[argStart + 3]);

    CalApplyStatus st = applyCalLinear(input, vmin, vmax, pmin, pmax);
    switch (st) {
        case CAL_APPLY_OK:
            break;
        case CAL_APPLY_ERR_VOLTAGE_ORDER:
            msg.control.println(F("ERROR: vmin must be less than vmax"));
            return 1;
        case CAL_APPLY_ERR_VOLTAGE_RANGE:
            msg.control.print(F("ERROR: Voltage range must be 0.0-"));
            msg.control.print(calLinearMaxSensorVolts(input));
            msg.control.println(F("V at the sensor on this input"));
            msg.control.println(F("  (behind a divider, SET <pin> DIVIDER first)"));
            return 1;
        case CAL_APPLY_ERR_OUTPUT_ORDER:
            msg.control.println(F("ERROR: pmin must be less than pmax"));
            return 1;
        default:
            msg.control.println(F("ERROR: invalid linear calibration"));
            return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> CAL BIAS <resistor>
// Generic bias resistor — works with Steinhart-Hart, Lookup, Beta, Pressure Polynomial
static int set_cal_bias(uint8_t pin, int argc, const char* const* argv,
                        int argStart) {
    if (argc < argStart + 1) {
        msg.control.println(F("ERROR: BIAS requires a resistor value"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float bias = atof(argv[argStart]);

    CalApplyStatus st = applyCalBias(input, bias);
    switch (st) {
        case CAL_APPLY_OK:
            break;
        case CAL_APPLY_ERR_BIAS_TYPE:
            msg.control.print(F("ERROR: Calibration type "));
            msg.control.print(input->calibrationType);
            msg.control.println(F(" does not use bias resistor"));
            msg.control.println(F("  BIAS works with: Steinhart-Hart, Beta, Table, Pressure Polynomial"));
            return 1;
        default:
            // 10Ω to 10MΩ covers all practical thermistor / pressure sensor bias resistors.
            msg.control.print(F("ERROR: Bias resistor ("));
            msg.control.print(bias, 1);
            msg.control.println(F("Ω) must be between 10Ω and 10MΩ"));
            return 1;
    }

    msg.control.print(F("Bias resistor set for pin "));
    msg.control.print(argv[1]);
    msg.control.print(F(": "));
    msg.control.print(bias, 1);
    msg.control.println(F(" Ω"));
    printSaveReminder();
    return 0;
}

// SET <pin> CAL STEINHART <bias_r> <a> <b> <c>
static int set_cal_steinhart(uint8_t pin, int argc, const char* const* argv,
                             int argStart) {
    if (argc < argStart + 4) {
        msg.control.println(F("ERROR: STEINHART requires 4 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL STEINHART <bias_r> <a> <b> <c>"));
        msg.control.println(F("  Example: SET A0 CAL STEINHART 10000 0.001129 0.0002341 0.00000008775"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float bias_r = atof(argv[argStart]);
    float a = atof(argv[argStart + 1]);
    float b = atof(argv[argStart + 2]);
    float c = atof(argv[argStart + 3]);

    if (a == 0 || b == 0 || c == 0) {
        msg.control.println(F("WARNING: Zero coefficient detected - may indicate error"));
    }

    if (applyCalSteinhart(input, bias_r, a, b, c) != CAL_APPLY_OK) {
        msg.control.println(F("ERROR: bias_r must be > 0"));
        return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> CAL BETA <bias_r> <beta> <r0> <t0>
static int set_cal_beta(uint8_t pin, int argc, const char* const* argv,
                        int argStart) {
    if (argc < argStart + 4) {
        msg.control.println(F("ERROR: BETA requires 4 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL BETA <bias_r> <beta> <r0> <t0>"));
        msg.control.println(F("  Example: SET A0 CAL BETA 10000 3950 10000 25"));
        msg.control.println(F("  Where: bias_r=bias resistor (Ω), beta=β coefficient (K),"));
        msg.control.println(F("         r0=ref resistance (Ω), t0=ref temp (°C, typically 25)"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float bias_r = atof(argv[argStart]);
    float beta = atof(argv[argStart + 1]);
    float r0 = atof(argv[argStart + 2]);
    float t0 = atof(argv[argStart + 3]);

    if (beta < 1000 || beta > 10000) {
        msg.control.println(F("WARNING: Beta typically 2000-6000K. Value may be incorrect."));
    }
    if (t0 < -40 || t0 > 150) {
        msg.control.println(F("WARNING: t0 typically 25°C. Value may be incorrect."));
    }

    CalApplyStatus st = applyCalBeta(input, bias_r, beta, r0, t0);
    if (st == CAL_APPLY_ERR_BIAS_POSITIVE) {
        msg.control.println(F("ERROR: bias_r must be > 0"));
        return 1;
    }
    if (st != CAL_APPLY_OK) {
        msg.control.println(F("ERROR: r0 must be > 0"));
        return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> CAL PRESSURE_POLY <bias_r> <a> <b> <c>
static int set_cal_pressure_poly(uint8_t pin, int argc, const char* const* argv,
                                 int argStart) {
    if (argc < argStart + 4) {
        msg.control.println(F("ERROR: PRESSURE_POLY requires 4 parameters"));
        msg.control.println(F("  Usage: SET <pin> CAL PRESSURE_POLY <bias_r> <a> <b> <c>"));
        msg.control.println(F("  Example: SET A1 CAL PRESSURE_POLY 184 -6.75e-4 2.54e-6 1.87e-9"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float bias_r = atof(argv[argStart]);
    float a = atof(argv[argStart + 1]);
    float b = atof(argv[argStart + 2]);
    float c = atof(argv[argStart + 3]);

    if (applyCalPressurePoly(input, bias_r, a, b, c) != CAL_APPLY_OK) {
        msg.control.println(F("ERROR: bias_r must be > 0"));
        return 1;
    }

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
    printSaveReminder();
    return 0;
}

// SET <pin> CAL VOLTAGE_DIVIDER <r1> <r2> [<correction> [<offset>]]
// Battery / voltage-divider inputs (#478): until now only reachable through a
// JSON import, so a Mega (no JSON) was stuck on the preset 100k/6.8k.
static FLASHMEM int set_cal_voltage_divider(uint8_t pin, int argc, const char* const* argv,
                                            int argStart) {
    if (argc < argStart + 2 || argc > argStart + 4) {
        msg.control.println(F("ERROR: VOLTAGE_DIVIDER requires <r1> <r2> [<correction> [<offset>]]"));
        msg.control.println(F("  Example: SET A9 CAL VOLTAGE_DIVIDER 100000 6800 1.02"));
        return 1;
    }

    Input* input = getInputByPin(pin);
    if (!input || !input->flags.isEnabled) {
        msg.control.println(F("ERROR: Input not configured"));
        return 1;
    }

    float r1 = atof(argv[argStart]);
    float r2 = atof(argv[argStart + 1]);
    float correction = (argc > argStart + 2) ? atof(argv[argStart + 2]) : 1.0f;
    float offset = (argc > argStart + 3) ? atof(argv[argStart + 3]) : 0.0f;

    switch (applyCalVoltageDivider(input, r1, r2, correction, offset)) {
        case CAL_APPLY_OK:
            break;
        case CAL_APPLY_ERR_DIVIDER_R:
            msg.control.println(F("ERROR: r1 must be 0-10M and r2 above 0 up to 10M (ohms)"));
            return 1;
        case CAL_APPLY_ERR_DIVIDER_SCALE:
            msg.control.println(F("ERROR: correction must be 0.1-10 and offset -10 to 10 V"));
            return 1;
        default:
            msg.control.println(F("ERROR: invalid voltage divider calibration"));
            return 1;
    }

    msg.control.print(F("Voltage divider calibration set for pin "));
    msg.control.println(argv[1]);
    msg.control.print(F("  R1: "));
    msg.control.print(r1, 1);
    msg.control.print(F(" Ω  R2: "));
    msg.control.print(r2, 1);
    msg.control.println(F(" Ω"));
    msg.control.print(F("  Correction: "));
    msg.control.print(correction, 3);
    msg.control.print(F("  Offset: "));
    msg.control.print(offset, 3);
    msg.control.println(F(" V"));
    printSaveReminder();
    return 0;
}

//=============================================================================
// CAL profiles — sub-table reached via SET <pin> CAL <profile> ...
//=============================================================================

static const char PSTR_CAL_STEINHART[]       PROGMEM = "STEINHART";
static const char PSTR_CAL_BETA[]            PROGMEM = "BETA";
static const char PSTR_CAL_PRESSURE_LINEAR[] PROGMEM = "PRESSURE_LINEAR";
static const char PSTR_CAL_PRESSURE_POLY[]   PROGMEM = "PRESSURE_POLY";
static const char PSTR_CAL_RPM[]             PROGMEM = "RPM";
static const char PSTR_CAL_SPEED[]           PROGMEM = "SPEED";
static const char PSTR_CAL_VOLTAGE_DIVIDER[] PROGMEM = "VOLTAGE_DIVIDER";
static const char PSTR_CAL_BIAS[]            PROGMEM = "BIAS";
static const char PSTR_CAL_PRESET[]          PROGMEM = "PRESET";

static const char PSTR_HELP_CAL_STEINHART[] PROGMEM =
    "  SET <pin> CAL STEINHART <bias> <a> <b> <c>      Steinhart-Hart thermistor";
static const char PSTR_HELP_CAL_BETA[] PROGMEM =
    "  SET <pin> CAL BETA <bias> <beta> <r0> <t0>      Beta-equation thermistor";
static const char PSTR_HELP_CAL_PRESSURE_LINEAR[] PROGMEM =
    "  SET <pin> CAL PRESSURE_LINEAR <vmin> <vmax> <pmin> <pmax>";
static const char PSTR_HELP_CAL_PRESSURE_POLY[] PROGMEM =
    "  SET <pin> CAL PRESSURE_POLY <bias> <a> <b> <c>  VDO polynomial pressure";
static const char PSTR_HELP_CAL_RPM[] PROGMEM =
    "  SET <pin> CAL RPM <poles> <ratio> [<mult>] <timeout> <min> <max>";
static const char PSTR_HELP_CAL_SPEED[] PROGMEM =
    "  SET <pin> CAL SPEED <ppr> <tire_circ> <ratio> [<mult>] <timeout> <max>";
static const char PSTR_HELP_CAL_VOLTAGE_DIVIDER[] PROGMEM =
    "  SET <pin> CAL VOLTAGE_DIVIDER <r1> <r2> [<corr> [<offset>]]";
static const char PSTR_HELP_CAL_BIAS[] PROGMEM =
    "  SET <pin> CAL BIAS <resistor>                   Bias resistor (Ω)";
static const char PSTR_HELP_CAL_PRESET[] PROGMEM =
    "  SET <pin> CAL PRESET                            Clear custom, restore preset";

static const CalProfile CAL_PROFILES[] PROGMEM = {
    { PSTR_CAL_STEINHART,       set_cal_steinhart,       PSTR_HELP_CAL_STEINHART, CAL_THERMISTOR_STEINHART },
    { PSTR_CAL_BETA,            set_cal_beta,            PSTR_HELP_CAL_BETA, CAL_THERMISTOR_BETA },
    { PSTR_CAL_PRESSURE_LINEAR, set_cal_pressure_linear, PSTR_HELP_CAL_PRESSURE_LINEAR, CAL_LINEAR },
    { PSTR_CAL_PRESSURE_POLY,   set_cal_pressure_poly,   PSTR_HELP_CAL_PRESSURE_POLY, CAL_PRESSURE_POLYNOMIAL },
    { PSTR_CAL_RPM,             set_cal_rpm,             PSTR_HELP_CAL_RPM, CAL_RPM },
    { PSTR_CAL_SPEED,           set_cal_speed,           PSTR_HELP_CAL_SPEED, CAL_SPEED },
    { PSTR_CAL_VOLTAGE_DIVIDER, set_cal_voltage_divider, PSTR_HELP_CAL_VOLTAGE_DIVIDER, CAL_VOLTAGE_DIVIDER },
    { PSTR_CAL_BIAS,            set_cal_bias,            PSTR_HELP_CAL_BIAS, CAL_NONE },
    { PSTR_CAL_PRESET,          set_cal_preset,          PSTR_HELP_CAL_PRESET, CAL_NONE },
};
static const uint8_t NUM_CAL_PROFILES = sizeof(CAL_PROFILES) / sizeof(CalProfile);

// SET <pin> CAL <profile> [<args>]
// Top-level dispatcher into CAL_PROFILES. Reads token at argv[3], calls the
// matched leaf with argStart=4 so the leaf's args land at argv[4..].
static int set_cal(uint8_t pin, int argc, const char* const* argv) {
    if (argc < 4) {
        msg.control.println(F("ERROR: CAL requires a profile keyword"));
        msg.control.println(F("  Usage: SET <pin> CAL <profile> [<args>]"));
        msg.control.println(F("  Profiles: STEINHART, BETA, PRESSURE_LINEAR, PRESSURE_POLY,"));
        msg.control.println(F("            RPM, SPEED, VOLTAGE_DIVIDER, BIAS, PRESET"));
        return 1;
    }
    const char* profile = argv[3];
    for (uint8_t i = 0; i < NUM_CAL_PROFILES; i++) {
        const char* token = (const char*)pgm_read_ptr(&CAL_PROFILES[i].token);
        if (streq_P(profile, token)) {
            // #460: a typed profile must be what this input's sensor reads. The
            // core refuses it too (calibration_apply); this gives the CLI one
            // message for every profile instead of eight.
            const uint8_t want = pgm_read_byte(&CAL_PROFILES[i].type);
            Input* input = getInputByPin(pin);
            if (want != CAL_NONE && input != nullptr && input->calibrationType != want) {
                msg.control.print(F("ERROR: "));
                msg.control.print(profile);
                msg.control.print(F(" calibration doesn't apply to "));
                const SensorInfo* flash = getSensorByIndex(input->sensorIndex);
                if (flash != nullptr) {
                    SensorInfo info;
                    loadSensorInfo(flash, &info);
                    msg.control.println(info.name);
                } else {
                    msg.control.println(F("this input's sensor"));
                }
                msg.control.println(F("  Each sensor reads one calibration type; change the sensor instead"));
                return 1;
            }
            CalProfileHandler h = (CalProfileHandler)pgm_read_ptr(&CAL_PROFILES[i].handler);
            return h(pin, argc, argv, 4);
        }
    }
    msg.control.print(F("ERROR: Unknown CAL profile '"));
    msg.control.print(profile);
    msg.control.println(F("'"));
    msg.control.println(F("  Type 'HELP SET' to see CAL profiles"));
    return 1;
}

// SET field tokens — PROGMEM string literals so the dispatch table itself
// can live in flash on AVR (saves ~70 bytes RAM on mega2560).
static const char PSTR_SET_APPLICATION[]     PROGMEM = "APPLICATION";
static const char PSTR_SET_SENSOR[]          PROGMEM = "SENSOR";
static const char PSTR_SET_NAME[]            PROGMEM = "NAME";
static const char PSTR_SET_ABBR[]            PROGMEM = "ABBR";
static const char PSTR_SET_INPUT[]           PROGMEM = "INPUT";
static const char PSTR_SET_DISPLAY[]         PROGMEM = "DISPLAY";
static const char PSTR_SET_UNITS[]           PROGMEM = "UNITS";
static const char PSTR_SET_ALARM[]           PROGMEM = "ALARM";
static const char PSTR_SET_DIVIDER[]         PROGMEM = "DIVIDER";
static const char PSTR_SET_OUTPUT[]          PROGMEM = "OUTPUT";
static const char PSTR_SET_CAL[]             PROGMEM = "CAL";
static const char PSTR_SET_OBD_PID[]         PROGMEM = "OBD_PID";
static const char PSTR_SET_OBD_LENGTH[]      PROGMEM = "OBD_LENGTH";
#if INCLUDE_CAN
static const char PSTR_SET_CAN_TIMEOUT[]     PROGMEM = "CAN_TIMEOUT";
#endif

// Each help block is printed verbatim by printHelpSet: it bakes in the
// "SET <pin> <field>" prefix and may span multiple lines so leaves with
// sub-forms (ALARM, OUTPUT, CAL) can document every supported shape.
static const char PSTR_HELP_SET_APPLICATION[] PROGMEM =
    "  SET <pin> APPLICATION <app>   Set measurement type (CHT, OIL_PRESSURE, ...)";
static const char PSTR_HELP_SET_SENSOR[] PROGMEM =
    "  SET <pin> SENSOR <sensor>     Set hardware sensor (e.g. MAX6675, VDO_120C_TABLE)";
static const char PSTR_HELP_SET_NAME[] PROGMEM =
    "  SET <pin> NAME <name>         Set full display label (32 chars)";
static const char PSTR_HELP_SET_ABBR[] PROGMEM =
    "  SET <pin> ABBR <abbr>         Set abbreviated label (8 chars)";
static const char PSTR_HELP_SET_INPUT[] PROGMEM =
    "  SET <pin> INPUT ENABLE|DISABLE  Enable or disable this input";
static const char PSTR_HELP_SET_DISPLAY[] PROGMEM =
    "  SET <pin> DISPLAY ENABLE|DISABLE  Toggle LCD display for this input";
static const char PSTR_HELP_SET_UNITS[] PROGMEM =
    "  SET <pin> UNITS <units>       Override display units";
static const char PSTR_HELP_SET_ALARM[] PROGMEM =
    "  SET <pin> ALARM <min> <max>   Set the input's alarm band (rules-engine Rule)\n"
    "  SET <pin> ALARM MIN|MAX <v>   One-sided floor/ceiling alarm\n"
    "  SET <pin> ALARM WARN <lo|NONE|AUTO> [<hi|NONE|AUTO>]  Set the approach-warning edge(s)\n"
    "  SET <pin> ALARM DISABLE       Remove the input's alarm Rule\n"
    "  (INFO/WARNING-severity alarms: RULE ADD <pin> ABOVE|BELOW ... ; status: RULE LIST)";
static const char PSTR_HELP_SET_DIVIDER[] PROGMEM =
    "  SET <pin> DIVIDER <ratio>     Set voltage divider ratio (0-1)";
static const char PSTR_HELP_SET_OUTPUT[] PROGMEM =
    "  SET <pin> OUTPUT STATUS       Show this input's output mask\n"
    "  SET <pin> OUTPUT <tgt> ENABLE|DISABLE  tgt = CAN, RealDash, Serial, SD_Log\n"
    "  SET <pin> OUTPUT ALL ENABLE|DISABLE";
// SET <pin> CAL — the help block lists every profile so HELP SET stays a
// single source of truth. PROGMEM-resident multi-line string built from the
// per-profile help fields would require runtime concat; since the profile
// list is stable, it's cheaper and clearer to enumerate here. printHelpSet
// also walks CAL_PROFILES below so this stays in sync.
static const char PSTR_HELP_SET_CAL[] PROGMEM =
    "  SET <pin> CAL <profile> [<args>]  Custom calibration (see profiles below)";
static const char PSTR_HELP_SET_OBD_PID[] PROGMEM =
    "  SET <pin> OBD_PID <0xNN>      OBD-II Mode 01 PID this input reports as (0x00 = none)";
static const char PSTR_HELP_SET_OBD_LENGTH[] PROGMEM =
    "  SET <pin> OBD_LENGTH <1-5>    OBD-II response data length in bytes";
#if INCLUDE_CAN
static const char PSTR_HELP_SET_CAN_TIMEOUT[] PROGMEM =
    "  SET <pin> CAN_TIMEOUT <ms>    CAN-imported sensor timeout (100-30000)";
#endif

// SET field dispatch table — PROGMEM-resident on AVR; access via pgm_read_ptr.
static const SetField SET_FIELDS[] PROGMEM = {
    { PSTR_SET_APPLICATION,     set_application,     PSTR_HELP_SET_APPLICATION     },
    { PSTR_SET_SENSOR,          set_sensor,          PSTR_HELP_SET_SENSOR          },
    { PSTR_SET_NAME,            set_name,            PSTR_HELP_SET_NAME            },
    { PSTR_SET_ABBR,            set_abbr,            PSTR_HELP_SET_ABBR            },
    { PSTR_SET_INPUT,           set_input,           PSTR_HELP_SET_INPUT           },
    { PSTR_SET_DISPLAY,         set_display,         PSTR_HELP_SET_DISPLAY         },
    { PSTR_SET_UNITS,           set_units,           PSTR_HELP_SET_UNITS           },
    { PSTR_SET_ALARM,           set_alarm,           PSTR_HELP_SET_ALARM           },
    { PSTR_SET_DIVIDER,         set_divider,         PSTR_HELP_SET_DIVIDER         },
    { PSTR_SET_OUTPUT,          set_output,          PSTR_HELP_SET_OUTPUT          },
    { PSTR_SET_CAL,             set_cal,             PSTR_HELP_SET_CAL             },
    { PSTR_SET_OBD_PID,         set_obd_pid,         PSTR_HELP_SET_OBD_PID         },
    { PSTR_SET_OBD_LENGTH,      set_obd_length,      PSTR_HELP_SET_OBD_LENGTH      },
#if INCLUDE_CAN
    { PSTR_SET_CAN_TIMEOUT,     set_can_timeout,     PSTR_HELP_SET_CAN_TIMEOUT     },
#endif
};
static const uint8_t NUM_SET_FIELDS = sizeof(SET_FIELDS) / sizeof(SetField);

void printHelpSet() {
    msg.control.println();
    msg.control.println(F("=== SET Commands ==="));
    msg.control.println(F("Configure input pins (application, sensor, names, units, alarms, calibration)"));
    msg.control.println();
    msg.control.println(F("  SET <pin> <app> <sensor>      Combined config (e.g. SET 6 CHT MAX6675)"));
    // Each entry's help string is printed verbatim — bakes in the "SET <pin>
    // <field>" prefix and may span multiple lines for sub-form leaves.
    for (uint8_t i = 0; i < NUM_SET_FIELDS; i++) {
        const char* help = (const char*)pgm_read_ptr(&SET_FIELDS[i].help);
        if (!help) continue;
        msg.control.println((const __FlashStringHelper*)help);
    }
    // Enumerate the CAL sub-table so HELP SET shows every calibration profile
    // without forcing the user into a separate HELP step.
    msg.control.println(F("CAL profiles:"));
    for (uint8_t i = 0; i < NUM_CAL_PROFILES; i++) {
        const char* help = (const char*)pgm_read_ptr(&CAL_PROFILES[i].help);
        if (!help) continue;
        msg.control.println((const __FlashStringHelper*)help);
    }
    msg.control.println();
}

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
            // Check sensor/application measurement type compatibility. This is
            // a courtesy message only -- the real gate is inside
            // setInputApplication()/setInputSensor() below (#383), which is
            // what also covers the two-step `SET <pin> APPLICATION ...` then
            // `SET <pin> SENSOR ...` form and JSON import. Route through the
            // same predicate so this message can never disagree with the
            // enforcement point.
            if (!sensorMatchesApplication(sensorIndex, appIndex)) {
                const SensorInfo* sensorInfo = getSensorByIndex(sensorIndex);
                const ApplicationPreset* appPreset = getApplicationByIndex(appIndex);
                MeasurementType sensorMeasType = sensorInfo
                    ? (MeasurementType)pgm_read_byte(&sensorInfo->measurementType)
                    : MEASURE_TEMPERATURE;
                MeasurementType appMeasType = appPreset
                    ? (MeasurementType)pgm_read_byte(&appPreset->expectedMeasurementType)
                    : MEASURE_TEMPERATURE;

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

#if INCLUDE_RULES_ENGINE
            const Input* prev = getInputByPin(pin);
            const uint8_t oldApp = prev ? prev->applicationIndex : 0;
#endif
            // First set application (which also calls setInputSensor with preset sensor)
            if (setInputApplication(pin, appIndex)) {
                // Then override sensor if different from preset
                Input* input = getInputByPin(pin);
                if (input && input->sensorIndex != sensorIndex) {
                    if (!setInputSensor(pin, sensorIndex)) {
                        // Should not happen -- sensorMatchesApplication() above
                        // already confirmed the pair, but setInputSensor() is
                        // the authoritative check (#383); honour a refusal
                        // rather than report false success.
                        msg.control.println(F("ERROR: Failed to set sensor"));
                        return 1;
                    }
                }

                msg.control.print(F("Input "));
                msg.control.print(argv[1]);
                msg.control.print(F(" configured as "));
                msg.control.print(field);
                msg.control.print(F(" with "));
                msg.control.println(argv[3]);
                printSaveReminder();
#if INCLUDE_RULES_ENGINE
                // Same seeding as SET <pin> APPLICATION (#487): this form used
                // to leave the input with no default alarm.
                seedRulesForApplication(pin, appIndex, oldApp);
#endif
                return 0;
            }
            return 1;
        }
    }

    // SET CAN <pid> [ecu]  -  Import CAN sensor by PID (ecu: response ID, default 0x7E8)
    // Example: SET CAN 0x0C  (imports Engine RPM from OBD-II)
    // This automatically assigns the next available CAN virtual pin (CAN:0, CAN:1, etc.)
#if INCLUDE_CAN
    // Bare "CAN" (no :N suffix) is *only* valid as the import prelude — there's
    // no other operation that takes a freshly-allocated virtual pin and a
    // non-PID arg. So if argv[2] isn't a PID literal, this is a typo and we
    // error here rather than falling through to field dispatch (which would
    // run set_application/set_sensor/etc. against the freshly-allocated CAN
    // pin and produce a different phantom — the original issue, but with a
    // different shape).
    //
    // Use SET CAN:N <field> ... to configure an existing CAN sensor.
    if (streq_P(argv[1], PSTR("CAN")) && argc >= 3 && !looksLikePidLiteral(argv[2])) {
        msg.control.println(F("ERROR: 'SET CAN <pid>' requires a numeric or 0xNN hex PID"));
        msg.control.println(F("  Use 'SET CAN:N <field> ...' to configure an existing CAN sensor"));
        return 1;
    }
    if (streq_P(argv[1], PSTR("CAN")) && argc >= 3) {
        // Parse PID (supports hex like 0x0C or decimal like 12)
        uint8_t pid;
        if (argv[2][0] == '0' && (argv[2][1] == 'x' || argv[2][1] == 'X')) {
            pid = (uint8_t)strtoul(argv[2] + 2, nullptr, 16);
        } else {
            pid = (uint8_t)atoi(argv[2]);
        }

        // Optional ECU response ID (#511): SCAN PIDS lists each ECU's PIDs,
        // and one only a second ECU (0x7E9 transmission, ...) answers must be
        // requested from and read on that ECU's ID. Default: engine, 0x7E8.
        uint16_t ecuId = 0x7E8;
        if (argc >= 4) {
            char* end = nullptr;
            unsigned long id = strtoul(argv[3], &end, 0);
            if (argc > 4 || end == argv[3] || *end != '\0' || id < 0x7E8 || id > 0x7EF) {
                msg.control.println(F("ERROR: Usage: SET CAN <pid> [ecu]"));
                msg.control.println(F("  ecu is the ECU's response ID, 0x7E8-0x7EF (default 0x7E8)"));
                return 1;
            }
            ecuId = (uint16_t)id;
        }

        // pin was already allocated by parsePin("CAN") at the top of cmd_set
        uint8_t virtualPin = pin;

        // Lookup standard PID info. The table is PROGMEM: copy the row to RAM
        // (reading its fields through the flash pointer returned garbage on
        // AVR, #496). Its name/abbr/units pointers stay PROGMEM addresses.
        const StandardPIDInfo* pidRow = lookupStandardPID(pid);
        StandardPIDInfo pidBuf;
        if (pidRow) memcpy_P(&pidBuf, pidRow, sizeof(pidBuf));
        const StandardPIDInfo* pidInfo = pidRow ? &pidBuf : nullptr;

        // Configure as CAN_IMPORT sensor
        uint8_t canSensorIndex = getSensorIndexByName("CAN_IMPORT");
        if (canSensorIndex == 0) {
            msg.control.println(F("ERROR: CAN_IMPORT sensor not found in library"));
            return 1;
        }

        // Slot + sensor + the defaults every new input gets (outputs, display):
        // allocateInputSlot() alone left outputMask 0, so a SET CAN input
        // reached no output and never displayed (#501)
        Input* input = createCanImportInput(virtualPin);
        if (!input) {
            msg.control.println(F("ERROR: Failed to create CAN input"));
            return 1;
        }

        // Configure CAN calibration
        if (pidInfo) {
            // Use standard PID info for automatic configuration
            input->customCalibration.can.source_can_id = ecuId;
            input->customCalibration.can.source_pid = pid;
            input->customCalibration.can.data_offset = 0;
            input->customCalibration.can.data_length = pidInfo->data_length;
            input->customCalibration.can.is_big_endian = true;
            input->customCalibration.can.scale_factor = pidInfo->scale_factor;
            input->customCalibration.can.offset = pidInfo->offset;
            // The table's pressure PIDs are in kPa; preOBD's pressure base unit
            // is bar (display/OBD output convert from bar), so store bar.
            if (pidInfo->measurementType == MEASURE_PRESSURE) {
                input->customCalibration.can.scale_factor *= 0.01f;
                input->customCalibration.can.offset *= 0.01f;
            }
            input->customCalibration.can.timeout_ms = CAN_DEFAULT_TIMEOUT_MS;
            input->flags.useCustomCalibration = true;

            // Measurement type and units from the standard PID table (#496):
            // the CAN_IMPORT preset left every PID labelled CELSIUS.
            input->measurementType = pidInfo->measurementType;
            {
                char unitName[12];
                strncpy_P(unitName, pidInfo->units, sizeof(unitName) - 1);
                unitName[sizeof(unitName) - 1] = '\0';
                uint8_t u = findUnitsIndexByName(unitName);
                input->unitsIndex = unitsMatchMeasurement(u, pidInfo->measurementType)
                                    ? u : defaultUnitsForMeasurement(pidInfo->measurementType);
            }

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
            input->customCalibration.can.source_can_id = ecuId;
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

        if (ecuId != 0x7E8) {
            // By hand: HEX prints lowercase on Teensy, and a printf format
            // string would sit in RAM on AVR
            msg.control.print(F("  from ECU 0x7E"));
            uint8_t d = ecuId & 0x0F;
            msg.control.println((char)(d < 10 ? '0' + d : 'A' + d - 10));
        }

        input->flags.isEnabled = true;
        return 0;
    }
#endif // INCLUDE_CAN

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

#if INCLUDE_SELFTEST
// Validate every SET_FIELDS entry, then every CAL_PROFILES entry. Reads through
// pgm_read_ptr — the same path the dispatcher takes — so a regression that
// breaks PROGMEM access fails this check immediately.
int selftestSetTable() {
    int failures = 0;
    for (uint8_t i = 0; i < NUM_SET_FIELDS; i++) {
        const char*     tok = (const char*)pgm_read_ptr(&SET_FIELDS[i].token);
        SetFieldHandler h   = (SetFieldHandler)pgm_read_ptr(&SET_FIELDS[i].handler);
        failures += validateDispatchEntry(F("SET"), i, tok, (const void*)h, true);
    }
    for (uint8_t i = 0; i < NUM_CAL_PROFILES; i++) {
        const char*       tok = (const char*)pgm_read_ptr(&CAL_PROFILES[i].token);
        CalProfileHandler h   = (CalProfileHandler)pgm_read_ptr(&CAL_PROFILES[i].handler);
        failures += validateDispatchEntry(F("SET CAL"), i, tok, (const void*)h, true);
    }
    return failures;
}
#endif // INCLUDE_SELFTEST
