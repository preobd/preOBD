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
#include "../lib/sd_manager.h"
#include "../lib/serial_manager.h"
#include "../lib/pin_registry.h"
#include "../outputs/output_base.h"
#include "../lib/obd_query.h"
#include "../lib/display_manager.h"
#if ENABLE_RELAY_OUTPUT
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

// SYSTEM PINS subcommand handler — global pin map only.
// Per-pin detail lives at `INFO <pin>`; the old `SYSTEM PINS <pin>` shape
// duplicated INFO and was retired in the grammar redesign (#189).
static int system_pins(int argc, const char* const* argv, int /*tokenIndex*/) {
    (void)argv;
    if (argc != 2) {
        msg.control.println(F("ERROR: SYSTEM PINS takes no arguments"));
        msg.control.println(F("  Use 'INFO <pin>' for per-pin detail"));
        return 1;
    }
    printPinStatus();
    return 0;
}

// SYSTEM DUMP subcommand handler
static int system_dump(int argc, const char* const* argv, int /*tokenIndex*/) {
    // SYSTEM DUMP REGISTRY JSON — export static firmware catalogs
    if (argc == 4 && streq_P(argv[2], PSTR("REGISTRY")) && streq_P(argv[3], PSTR("JSON"))) {
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
    if (argc == 3 && streq_P(argv[2], PSTR("JSON"))) {
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

    if (streq_P(unitType, PSTR("TEMP"))) {
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
    } else if (streq_P(unitType, PSTR("PRESSURE"))) {
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
    } else if (streq_P(unitType, PSTR("ELEVATION"))) {
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
    } else if (streq_P(unitType, PSTR("SPEED"))) {
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
    if (streq_P(argv[2], PSTR("SENSOR"))) {
        systemConfig.sensorReadInterval = interval;
        msg.control.print(F("Sensor read interval set to "));
        msg.control.print(interval);
        msg.control.println(F(" ms"));
    } else if (streq_P(argv[2], PSTR("ALARM"))) {
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
    if (argc == 3 && streq_P(argv[2], PSTR("CONFIRM"))) {
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


// ===== SYSTEM DIAG — observe family (read-only, RUN-safe) =====
// Three sections, each printing its OWN verdict line so the two kinds of
// failure stay distinct: an INTEGRITY fail means a bad firmware build, a
// HARDWARE fail means the rig isn't wired the way it's configured.
//   INTEGRITY  dispatch-table self-consistency (was the SELFTEST command)
//   HW         presence/health of detectable peripherals
//   INPUTS     per-input reading sanity (range / rail)

static void diagSectionIntegrity() {
#if ENABLE_SELFTEST
    int failures = runDiagIntegrity();   // prints per-entry failures inline
    msg.control.print(F("[integrity]   dispatch tables ...... "));
    if (failures == 0) {
        msg.control.println(F("OK"));
    } else {
        msg.control.print(F("FAIL ("));
        msg.control.print(failures);
        msg.control.println(F(" issue(s))"));
    }
#else
    msg.control.println(F("[integrity]   (disabled in this build)"));
#endif
}

// I2C presence probe on the active I2C bus.
// Returns 1 = device ACKed, 0 = bus present but no ACK (FAIL), -1 = no I2C bus.
// getActiveI2C() is nullptr until a bus is actually initialized, so this never
// touches an un-begun Wire.
static int i2cProbe(uint8_t addr) {
    TwoWire* wire = getActiveI2C();
    if (!wire) return -1;
    wire->beginTransmission(addr);
    return (wire->endTransmission() == 0) ? 1 : 0;
}

// Hardware presence/POST. Only peripherals compiled into this build appear; a
// FAIL means "configured but not responding" (the actionable case). CAN-bus and
// nRF/BLE-link probes are a deferred follow-up — CAN presence needs a loopback
// to assert honestly, and the nRF link is host/coprocessor-specific.
static void diagSectionHw() {
    bool any = false;
    (void)any;

#if ENABLE_LCD
    any = true;
    msg.control.print(F("[hardware]    LCD (I2C 0x27) ....... "));
    switch (i2cProbe(0x27)) {
        case 1:  msg.control.println(F("OK")); break;
        case 0:  msg.control.println(F("FAIL (no ACK)")); break;
        default: msg.control.println(F("off (no I2C bus)")); break;
    }
#endif

#if ENABLE_BME280
    any = true;
    msg.control.print(F("[hardware]    BME280 (I2C) ......... "));
    {
        int r = i2cProbe(0x76);
        if (r == 0) r = i2cProbe(0x77);   // common alternate address
        switch (r) {
            case 1:  msg.control.println(F("OK")); break;
            case 0:  msg.control.println(F("FAIL (no ACK @0x76/0x77)")); break;
            default: msg.control.println(F("off (no I2C bus)")); break;
        }
    }
#endif

#if SUPPORTS_SD
    any = true;
    msg.control.print(F("[hardware]    SD card .............. "));
    msg.control.println(isSDInitialized() ? F("OK") : F("FAIL (not mounted)"));
#endif

    if (!any) {
        msg.control.println(F("[hardware]    (no probeable peripherals in this build)"));
    }
}

// Diagnostics are serial-command driven, never hot-path. On Teensy 4.x code
// defaults to fast ITCM (RAM1, scarce); keep these helpers in FLASH instead.
// FLASHMEM is a Teensy core macro — no-op elsewhere.
#ifndef FLASHMEM
#define FLASHMEM
#endif

// Per-input fault detection (#238).
//
// The runtime read functions are tuned for *display*: out-of-range inputs are
// deliberately clamped to a sane value (e.g. a lookup table saturates a
// disconnected sensor's resistance to its coldest entry, yielding a believable
// 0 degC). That is correct for a dashboard gauge but blinds diagnostics, which
// want the opposite — the raw electrical truth, where out-of-range *is* the
// signal. So diag reads the pin itself (un-clamped) and walks the chain
// raw ADC -> resistance/voltage -> verdict, deliberately separate from the
// read functions. See issue #238.

// Diagnostic class — how a given input is electrically probed.
enum DiagClass {
    DC_RESISTIVE,    // divider sense: raw ADC -> ohms (thermistor / resistive pressure / level)
    DC_VOLTAGE,      // direct/divider voltage at the pin
    DC_FREQUENCY,    // edge-counted (RPM / speed) — passive for now
    DC_CAN,          // CAN-imported frame — passive for now
    DC_THERMOCOUPLE, // SPI amp; runtime read already returns NaN on open-circuit bit
    DC_OTHER         // digital / environmental — passive
};

// Verdict — names the electrical condition observed, never a guess.
enum DiagVerdict {
    DV_IN_RANGE, DV_OUT_OF_RANGE, DV_OPEN, DV_SHORTED,
    DV_RAIL_HIGH, DV_RAIL_LOW, DV_OPEN_CIRCUIT, DV_NO_SIGNAL,
    DV_NEAR_LIMIT, DV_NO_READING
};

struct InputProbe {
    DiagClass cls;
    int   rawAdc;        // un-clamped analogRead (analog classes only)
    float pctFS;         // rawAdc as % of full scale
    bool  nearRailHi;    // pegged near ADC max
    bool  nearRailLo;    // pegged near ADC min
    bool  nearLimit;     // close to a rail but not pegged — ambiguous extreme zone
    bool  hasR;          // resistance computed
    float resistance;    // ohms (INFINITY at the rail)
    bool  hasTable;      // calibration carries a resistance table
    float rLo, rHi;      // table resistance endpoints
    bool  saturated;     // resistance beyond the table window (the clamp case)
    bool  hasV;          // voltage computed
    float voltage;       // volts at the pin
    float cooked;        // the runtime/display value (shown, labeled)
    DiagVerdict verdict;
};

static FLASHMEM DiagClass diagClassify(const Input* in) {
    switch (in->calibrationType) {
        case CAL_THERMISTOR_STEINHART:
        case CAL_THERMISTOR_BETA:
        case CAL_THERMISTOR_TABLE:
        case CAL_PRESSURE_POLYNOMIAL:
        case CAL_PRESSURE_TABLE:
        case CAL_LEVEL_TABLE:      return DC_RESISTIVE;
        case CAL_VOLTAGE_DIVIDER:
        case CAL_LINEAR:           return DC_VOLTAGE;
        case CAL_RPM:
        case CAL_SPEED:            return DC_FREQUENCY;
        case CAL_CAN_IMPORT:       return DC_CAN;
        case CAL_NONE:
        default:
            // Thermocouples register as CAL_NONE + temperature (MAX6675/31855).
            if (in->measurementType == MEASURE_TEMPERATURE) return DC_THERMOCOUPLE;
            return DC_OTHER;
    }
}

// Bias resistor is field 0 of every resistive calibration variant, in both the
// PROGMEM preset and the custom-override union — so it reads uniformly.
static FLASHMEM float diagBias(const Input* in) {
    if (in->flags.useCustomCalibration) {
        return *reinterpret_cast<const float*>(in->customCalibration.raw);
    }
    if (in->presetCalibration) {
        return pgm_read_float(reinterpret_cast<const float*>(in->presetCalibration));
    }
    return NAN;
}

// Read a table calibration's resistance endpoints (order-agnostic: lo/hi).
static FLASHMEM bool diagTableRange(const Input* in, float* lo, float* hi) {
    if (!in->presetCalibration) return false;
    const float* rt = nullptr;
    byte n = 0;
    switch (in->calibrationType) {
        case CAL_THERMISTOR_TABLE: {
            const ThermistorLookupCalibration* c =
                (const ThermistorLookupCalibration*)in->presetCalibration;
            rt = (const float*)pgm_read_ptr(&c->resistance_table);
            n  = pgm_read_byte(&c->table_size);
            break;
        }
        case CAL_PRESSURE_TABLE: {
            const PressureTableCalibration* c =
                (const PressureTableCalibration*)in->presetCalibration;
            rt = (const float*)pgm_read_ptr(&c->resistance_table);
            n  = pgm_read_byte(&c->table_size);
            break;
        }
        case CAL_LEVEL_TABLE: {
            const LevelTableCalibration* c =
                (const LevelTableCalibration*)in->presetCalibration;
            rt = (const float*)pgm_read_ptr(&c->resistance_table);
            n  = pgm_read_byte(&c->table_size);
            break;
        }
        default: return false;
    }
    if (!rt || n < 2) return false;
    float a = pgm_read_float(&rt[0]);
    float b = pgm_read_float(&rt[n - 1]);
    *lo = (a < b) ? a : b;
    *hi = (a < b) ? b : a;
    return true;
}

static FLASHMEM bool diagValuePlausible(MeasurementType m, float v);  // defined below

// Gather raw electrical evidence and classify, without clamping. Reads the pin
// directly for analog classes; falls back to the runtime read for the others.
static FLASHMEM void diagProbe(Input* in, InputProbe* p) {
    memset(p, 0, sizeof(*p));
    p->resistance = NAN;
    p->voltage = NAN;
    p->cooked = NAN;
    p->cls = diagClassify(in);

    // Diag uses a wider rail band than the runtime's 3-count ADC_RAIL_MARGIN:
    // its job is to be suspicious of the edges, not lenient like the display path.
    const int railLo = (int)(ADC_MAX_VALUE * 0.02f);
    const int railHi = (int)(ADC_MAX_VALUE * 0.98f);
    // Caution band just inside the hard rails: not pegged, but close enough that
    // a no-table sensor's reading can't be told apart from a fault (e.g. a wrong
    // bias resistor faking extreme cold reads the same as a genuinely cold NTC).
    const int cautLo = (int)(ADC_MAX_VALUE * 0.07f);
    const int cautHi = (int)(ADC_MAX_VALUE * 0.93f);

    if (p->cls == DC_RESISTIVE || p->cls == DC_VOLTAGE) {
        analogRead(in->pin);                 // discard: mux settling
        p->rawAdc = analogRead(in->pin);
        p->pctFS = 100.0f * p->rawAdc / (float)ADC_MAX_VALUE;
        p->nearRailLo = p->rawAdc <= railLo;
        p->nearRailHi = p->rawAdc >= railHi;
        p->nearLimit = !p->nearRailLo && !p->nearRailHi &&
                       (p->rawAdc <= cautLo || p->rawAdc >= cautHi);
    }

    if (p->cls == DC_RESISTIVE) {
        float bias = diagBias(in);
        p->hasR = !isnan(bias);
        if (p->hasR) {
            p->resistance = (p->rawAdc >= ADC_MAX_VALUE)
                ? INFINITY
                : (float)p->rawAdc * bias / (ADC_MAX_VALUE - p->rawAdc);
        }
        p->hasTable = diagTableRange(in, &p->rLo, &p->rHi);
        if (p->hasTable && p->hasR && !isinf(p->resistance)) {
            p->saturated = (p->resistance < p->rLo || p->resistance > p->rHi);
        }
        // Topology is fixed by calculateResistance (bias->Vcc, sensor->GND):
        // rail-high == open, rail-low == short.
        if      (p->nearRailHi) p->verdict = DV_OPEN;
        else if (p->nearRailLo) p->verdict = DV_SHORTED;
        else if (p->saturated)  p->verdict = DV_OUT_OF_RANGE;
        else                    p->verdict = DV_IN_RANGE;
    } else if (p->cls == DC_VOLTAGE) {
        p->hasV = true;
        p->voltage = (float)p->rawAdc * AREF_VOLTAGE / (float)ADC_MAX_VALUE;
        if      (p->nearRailHi) p->verdict = DV_RAIL_HIGH;
        else if (p->nearRailLo) p->verdict = DV_RAIL_LOW;
        else                    p->verdict = DV_IN_RANGE;
    } else {
        // Passive classes (v1): the runtime read's NaN is the only signal.
        if (in->readFunction) in->readFunction(in);
        p->cooked = in->value;
        bool bad = isnan(in->value);
        switch (p->cls) {
            case DC_THERMOCOUPLE: p->verdict = bad ? DV_OPEN_CIRCUIT : DV_IN_RANGE; break;
            case DC_FREQUENCY:
            case DC_CAN:          p->verdict = bad ? DV_NO_SIGNAL    : DV_IN_RANGE; break;
            default:              p->verdict = bad ? DV_NO_READING   : DV_IN_RANGE; break;
        }
        return;
    }

    // Analog classes: capture the cooked value too (shown, but labeled).
    if (in->readFunction) in->readFunction(in);
    p->cooked = in->value;

    // A non-finite display value (NaN or INF) means the calibration math blew
    // up — wrong bias/coefficients, or a resistance outside the curve's domain.
    // That is never "in range"; it overrides only the benign verdict (rail/open
    // and table-saturation verdicts are already more specific).
    if (!isfinite(p->cooked) && p->verdict == DV_IN_RANGE) {
        p->verdict = DV_NO_READING;
    }
    // Value-plausibility: a finite but physically-impossible reading is out of
    // range too. This is what catches the no-table (steinhart/beta) case the
    // raw electrical evidence can't — a believable-looking pin that resolves to
    // an unbelievable value. Only overrides the benign verdict.
    else if (p->verdict == DV_IN_RANGE &&
             !diagValuePlausible(in->measurementType, p->cooked)) {
        p->verdict = DV_OUT_OF_RANGE;
    }
    // No-table sensor sitting in the caution band: the value is plausible but
    // near the sensor's electrical limit, where a genuine extreme and a wiring/
    // bias fault are indistinguishable passively. Don't claim IN RANGE — flag it.
    // Table sensors are exempt: their saturation check already verifies the edge.
    else if (p->verdict == DV_IN_RANGE && p->cls == DC_RESISTIVE &&
             !p->hasTable && p->nearLimit) {
        p->verdict = DV_NEAR_LIMIT;
    }
}

static FLASHMEM const __FlashStringHelper* diagVerdictLabel(DiagVerdict v) {
    switch (v) {
        case DV_IN_RANGE:     return F("IN RANGE");
        case DV_OUT_OF_RANGE: return F("OUT OF RANGE");
        case DV_OPEN:         return F("OPEN");
        case DV_SHORTED:      return F("SHORTED");
        case DV_RAIL_HIGH:    return F("RAIL HIGH");
        case DV_RAIL_LOW:     return F("RAIL LOW");
        case DV_OPEN_CIRCUIT: return F("OPEN CIRCUIT");
        case DV_NO_SIGNAL:    return F("NO SIGNAL");
        case DV_NEAR_LIMIT: return F("NEAR LIMIT");
        default:              return F("NO READING");
    }
}

static FLASHMEM const __FlashStringHelper* diagUnitLabel(MeasurementType m) {
    switch (m) {
        case MEASURE_TEMPERATURE: return F("degC");
        case MEASURE_PRESSURE:    return F("bar");
        case MEASURE_VOLTAGE:     return F("V");
        case MEASURE_RPM:         return F("rpm");
        case MEASURE_SPEED:       return F("kph");
        case MEASURE_HUMIDITY:
        case MEASURE_PERCENT:     return F("%");
        case MEASURE_ELEVATION:   return F("m");
        default:                  return F("");
    }
}

static FLASHMEM const __FlashStringHelper* diagClassLabel(DiagClass c) {
    switch (c) {
        case DC_RESISTIVE:    return F("resistive divider");
        case DC_VOLTAGE:      return F("analog voltage");
        case DC_FREQUENCY:    return F("frequency (passive)");
        case DC_CAN:          return F("CAN import (passive)");
        case DC_THERMOCOUPLE: return F("thermocouple (passive)");
        default:              return F("other (passive)");
    }
}

// Generous outer physical limits per measurement type. Inside this band a value
// is believable; outside it the reading is almost certainly a fault, even when
// the pin is electrically inconclusive (e.g. a high-Z NTC that extrapolates to
// -41.7 degC is either genuinely that cold or miswired — flag it either way).
// These are deliberately wide sanity rails, NOT operating ranges.
static FLASHMEM bool diagValuePlausible(MeasurementType m, float v) {
    switch (m) {
        case MEASURE_TEMPERATURE: return v >= -40.0f  && v <= 1500.0f;  // NTC floor .. thermocouple ceiling
        case MEASURE_PRESSURE:    return v >= -1.0f   && v <= 50.0f;    // bar
        case MEASURE_VOLTAGE:     return v >= 0.0f    && v <= 60.0f;
        case MEASURE_RPM:         return v >= 0.0f    && v <= 20000.0f;
        case MEASURE_SPEED:       return v >= 0.0f    && v <= 400.0f;   // kph
        case MEASURE_HUMIDITY:
        case MEASURE_PERCENT:     return v >= 0.0f    && v <= 100.0f;
        case MEASURE_ELEVATION:   return v >= -500.0f && v <= 10000.0f;
        default:                  return true;
    }
}

// Short parenthetical context for the compact one-liner.
static FLASHMEM void diagPrintDetail(const Input* in, const InputProbe* p) {
    switch (p->verdict) {
        case DV_OUT_OF_RANGE:
            msg.control.print(p->cooked, 1);
            msg.control.print(' ');
            msg.control.print(diagUnitLabel(in->measurementType));
            // Saturated table = clamped display; otherwise an implausible value
            // (the high-Z "extreme cold or miswired" case).
            msg.control.print((p->hasTable && p->saturated)
                ? F("; R outside table")
                : F(" beyond plausible - check wiring"));
            break;
        case DV_OPEN:
        case DV_SHORTED:
        case DV_RAIL_HIGH:
        case DV_RAIL_LOW:
            msg.control.print(p->pctFS, 0);
            msg.control.print(F("% FS"));
            break;
        case DV_NEAR_LIMIT:
            msg.control.print(p->cooked, 1);
            msg.control.print(' ');
            msg.control.print(diagUnitLabel(in->measurementType));
            msg.control.print(F(" - genuine extreme or wiring/bias error"));
            break;
        case DV_OPEN_CIRCUIT:
            msg.control.print(F("thermocouple"));
            break;
        case DV_NO_SIGNAL:
            msg.control.print(F("no signal"));
            break;
        case DV_NO_READING:
            msg.control.print(F("no reading"));
            break;
        default:  // IN RANGE
            msg.control.print(p->cooked, 1);
            msg.control.print(' ');
            msg.control.print(diagUnitLabel(in->measurementType));
            break;
    }
}

// Verbose: the full evidence chain, then the verdict.
static FLASHMEM void diagPrintVerbose(const Input* in, const InputProbe* p) {
    msg.control.print(F("[inputs]      "));
    msg.control.print(in->abbrName);
    msg.control.print(F("   pin "));
    msg.control.print(in->pin);
    msg.control.print(F("   "));
    msg.control.println(diagClassLabel(p->cls));

    if (p->cls == DC_RESISTIVE || p->cls == DC_VOLTAGE) {
        msg.control.print(F("                raw      "));
        msg.control.print(p->rawAdc);
        msg.control.print(F(" / "));
        msg.control.print((int)ADC_MAX_VALUE);
        msg.control.print(F("   ("));
        msg.control.print(p->pctFS, 1);
        msg.control.print(F("% FS)"));
        if (p->nearRailHi)      msg.control.print(F("   near rail HI"));
        else if (p->nearRailLo) msg.control.print(F("   near rail LO"));
        else if (p->nearLimit)  msg.control.print(F("   near limit"));
        msg.control.println();
    }
    if (p->cls == DC_RESISTIVE && p->hasR) {
        msg.control.print(F("                sense    "));
        if (isinf(p->resistance)) msg.control.print(F("open (rail)"));
        else { msg.control.print(p->resistance, 0); msg.control.print(F(" ohm")); }
        if (p->hasTable) {
            msg.control.print(F("   table "));
            msg.control.print(p->rLo, 0);
            msg.control.print(F("-"));
            msg.control.print(p->rHi, 0);
            msg.control.print(F(" ohm"));
            if (p->saturated) msg.control.print(F("   OUT OF RANGE"));
        }
        msg.control.println();
    }
    if (p->cls == DC_VOLTAGE && p->hasV) {
        msg.control.print(F("                sense    "));
        msg.control.print(p->voltage, 2);
        msg.control.println(F(" V"));
    }
    msg.control.print(F("                display  "));
    if (!isfinite(p->cooked)) {
        msg.control.print(isnan(p->cooked) ? F("NaN") : F("INF"));
    } else {
        msg.control.print(p->cooked, 1);
        msg.control.print(' ');
        msg.control.print(diagUnitLabel(in->measurementType));
    }
    if (p->verdict == DV_OUT_OF_RANGE) msg.control.print(F("   (clamped - suspect)"));
    msg.control.println();

    msg.control.print(F("              -> "));
    msg.control.println(diagVerdictLabel(p->verdict));
}

static FLASHMEM void diagSectionInputs(bool verbose) {
    bool any = false;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        Input* in = &inputs[i];
        if (!in->flags.isEnabled) continue;
        any = true;

        InputProbe p;
        diagProbe(in, &p);

        if (verbose) {
            diagPrintVerbose(in, &p);
            continue;
        }

        // Compact: verdict-forward one-liner.
        msg.control.print(F("[inputs]      "));
        msg.control.print(in->abbrName);
        msg.control.print(F("   pin "));
        msg.control.print(in->pin);
        msg.control.print(F("   "));
        msg.control.print(diagVerdictLabel(p.verdict));
        msg.control.print(F("   ("));
        diagPrintDetail(in, &p);
        msg.control.println(F(")"));
    }
    if (!any) {
        msg.control.println(F("[inputs]      (no inputs configured)"));
    }
}

// Analog-pin discovery sweep (#280).
//
// DIAG INPUTS only walks configured inputs[], so it can't answer the bench
// question "which analog pin did I wire this sensor to?". This sweeps every
// analog-capable pin (ANALOG_PINS[], platform.cpp) and reports its live state,
// whether or not an Input is configured on it. A floating (unconnected) pin
// jitters; a wired sensor/divider holds a steady value — that contrast is the
// discovery signal.

// Jitter above this many ADC counts reads as "floating" (high-Z, unconnected).
// Platform-dependent: Teensy averages 4 samples in setupADC() so its noise floor
// is low; ESP32's unaveraged ADC is noisy; AVR sits between. Bench-tunable.
#if defined(__IMXRT1062__) || defined(__MK64FX512__) || defined(__MK66FX1M0__) || \
    defined(__MK20DX256__) || defined(__MK20DX128__)
static const int JITTER_FLOATING_COUNTS = (int)(ADC_MAX_VALUE * 0.015f);  // Teensy (averaged)
#elif defined(ESP32)
static const int JITTER_FLOATING_COUNTS = (int)(ADC_MAX_VALUE * 0.05f);   // ESP32 (noisy)
#else
static const int JITTER_FLOATING_COUNTS = (int)(ADC_MAX_VALUE * 0.03f);   // AVR / unknown
#endif

static FLASHMEM void diagPrintAnalogLabel(uint8_t analogIndex, uint8_t pin) {
    msg.control.print(F("[pins]        A"));
    msg.control.print(analogIndex);
    msg.control.print(F("   pin "));
    msg.control.print(pin);
    msg.control.print(F("   "));
}

static FLASHMEM void diagSectionPins() {
    msg.control.println(F("=== ANALOG PIN SWEEP ==="));
    for (uint8_t i = 0; i < ANALOG_PIN_COUNT; i++) {
        uint8_t pin = pgm_read_byte(&ANALOG_PINS[i]);

        // 1) Configured analog Input on this pin? Reuse diagProbe so DIAG PINS and
        //    DIAG INPUTS never disagree about the same pin.
        Input* owner = nullptr;
        for (uint8_t k = 0; k < MAX_INPUTS; k++) {
            if (inputs[k].flags.isEnabled && inputs[k].pin == pin) {
                owner = &inputs[k];
                break;
            }
        }
        if (owner) {
            InputProbe p;
            diagProbe(owner, &p);
            diagPrintAnalogLabel(i, pin);
            msg.control.print(diagVerdictLabel(p.verdict));
            msg.control.print(F("   -> "));
            msg.control.print(owner->abbrName);
            msg.control.print(F(" ("));
            diagPrintDetail(owner, &p);
            msg.control.println(F(")"));
            continue;
        }

        // 2) Pin claimed for any non-input function (button, buzzer, output, bus,
        //    chip-select) — don't disturb it with a read (on ESP32 analogRead
        //    reconfigures the pad, dropping e.g. a button's INPUT_PULLUP). An
        //    enabled analog Input is already handled above and self-tracks rather
        //    than registering, so a non-UNUSED entry here is always something else.
        //    Deny-list by exclusion so future PinUsageType values are safe too.
        PinUsageType usage = getPinUsage(pin);
        if (usage != PIN_UNUSED) {
            diagPrintAnalogLabel(i, pin);
            msg.control.print(F("in use - "));
            msg.control.println(getPinUsageTypeName(usage));
            continue;
        }

        // 3) Discovery candidate — sample the raw pin. Two throwaway reads with a
        //    settle defuse sequential-mux ghosting (a floating high-Z pin can
        //    otherwise hold a neighbour's charge and masquerade as driven).
        analogRead(pin);
        delayMicroseconds(20);
        analogRead(pin);
        int rawMin = (int)ADC_MAX_VALUE + 1, rawMax = -1, last = 0;
        for (uint8_t s = 0; s < 8; s++) {
            int r = analogRead(pin);
            last = r;
            if (r < rawMin) rawMin = r;
            if (r > rawMax) rawMax = r;
            delayMicroseconds(20);
        }
        int jitter = rawMax - rawMin;
        float pctFS   = 100.0f * last / (float)ADC_MAX_VALUE;
        float voltage = (float)last * AREF_VOLTAGE / (float)ADC_MAX_VALUE;
        const int railLo = (int)(ADC_MAX_VALUE * 0.02f);
        const int railHi = (int)(ADC_MAX_VALUE * 0.98f);

        const __FlashStringHelper* verdict;
        if      (jitter > JITTER_FLOATING_COUNTS) verdict = F("floating?");
        else if (last <= railLo)                  verdict = F("pulled low");
        else if (last >= railHi)                  verdict = F("pulled high");
        else                                      verdict = F("driven");

        diagPrintAnalogLabel(i, pin);
        msg.control.print(verdict);
        msg.control.print(F("   ("));
        msg.control.print(last);
        msg.control.print('/');
        msg.control.print((int)ADC_MAX_VALUE);
        msg.control.print(F("  "));
        msg.control.print(pctFS, 1);
        msg.control.print(F("% FS  "));
        msg.control.print(voltage, 2);
        msg.control.print(F(" V"));
        if (jitter > JITTER_FLOATING_COUNTS) {
            msg.control.print(F("  jitter "));
            msg.control.print(jitter);
        }
        msg.control.println(F(")"));
    }
}

// SYSTEM DIAG [INTEGRITY|HW|INPUTS|PINS] — bare form runs integrity+hw+inputs.
// FLASHMEM: diagnostics are serial-driven (never hot path) and this keeps the
// inlined diagSectionPins sweep out of scarce ITCM (RAM1) on Teensy 4.x.
static FLASHMEM int system_diag(int argc, const char* const* argv, int /*tokenIndex*/) {
    if (argc >= 3) {
        if (streq_P(argv[2], PSTR("INTEGRITY"))) { diagSectionIntegrity(); return 0; }
        if (streq_P(argv[2], PSTR("HW")))        { diagSectionHw();        return 0; }
        if (streq_P(argv[2], PSTR("INPUTS"))) {
            bool verbose = false;
            if (argc >= 4) {
                if (streq_P(argv[3], PSTR("VERBOSE")) || streq_P(argv[3], PSTR("FULL"))) {
                    verbose = true;
                } else {
                    msg.control.print(F("ERROR: Unknown DIAG INPUTS option '"));
                    msg.control.print(argv[3]);
                    msg.control.println(F("'"));
                    msg.control.println(F("  Valid: VERBOSE (or FULL), or bare INPUTS for compact"));
                    return 1;
                }
            }
            diagSectionInputs(verbose);
            return 0;
        }
        if (streq_P(argv[2], PSTR("PINS")))      { diagSectionPins();      return 0; }
        msg.control.print(F("ERROR: Unknown DIAG scope '"));
        msg.control.print(argv[2]);
        msg.control.println(F("'"));
        msg.control.println(F("  Valid: INTEGRITY, HW, INPUTS, PINS (or bare DIAG for all)"));
        return 1;
    }
    msg.control.println(F("=== SYSTEM DIAG ==="));
    diagSectionIntegrity();
    diagSectionHw();
    diagSectionInputs(false);
    return 0;
}

// SYSTEM subcommand tokens (PROGMEM)
static const char PSTR_SYS_STATUS[] PROGMEM = "STATUS";
static const char PSTR_SYS_PINS[] PROGMEM = "PINS";
static const char PSTR_SYS_DUMP[] PROGMEM = "DUMP";
static const char PSTR_SYS_DIAG[] PROGMEM = "DIAG";
static const char PSTR_SYS_SEA_LEVEL[] PROGMEM = "SEA_LEVEL";
static const char PSTR_SYS_UNITS[] PROGMEM = "UNITS";
static const char PSTR_SYS_INTERVAL[] PROGMEM = "INTERVAL";
static const char PSTR_SYS_REBOOT[] PROGMEM = "REBOOT";
static const char PSTR_SYS_RESET[] PROGMEM = "RESET";

// Each help block is printed verbatim by printSubcommandTable, so it bakes in
// the "SYSTEM <leaf>" prefix and lists every supported sub-form.
static const char PSTR_HELP_SYS_STATUS[] PROGMEM =
    "  SYSTEM STATUS                 Show all global configuration";
static const char PSTR_HELP_SYS_PINS[] PROGMEM =
    "  SYSTEM PINS                   Show all pin allocations (use INFO <pin> for per-pin detail)";
static const char PSTR_HELP_SYS_DUMP[] PROGMEM =
    "  SYSTEM DUMP                   Complete configuration dump (human-readable)\n"
    "  SYSTEM DUMP JSON              Export active configuration as JSON\n"
    "  SYSTEM DUMP REGISTRY JSON     Export static firmware catalogs as JSON";
static const char PSTR_HELP_SYS_DIAG[] PROGMEM =
    "  SYSTEM DIAG                   Run all diagnostics (integrity + hardware + inputs)\n"
    "  SYSTEM DIAG INTEGRITY         Validate dispatch-table self-consistency\n"
    "  SYSTEM DIAG HW                Probe presence/health of configured peripherals\n"
    "  SYSTEM DIAG INPUTS            Per-input fault detection (open/short/range)\n"
    "  SYSTEM DIAG INPUTS VERBOSE    Per-input fault detection with raw evidence chain\n"
    "  SYSTEM DIAG PINS              Sweep all analog pins (live raw/voltage; find wiring)";
static const char PSTR_HELP_SYS_SEA_LEVEL[] PROGMEM =
    "  SYSTEM SEA_LEVEL <hPa>        Set sea-level pressure (for altitude calc)";
static const char PSTR_HELP_SYS_UNITS[] PROGMEM =
    "  SYSTEM UNITS TEMP <C|F>\n"
    "  SYSTEM UNITS PRESSURE <BAR|PSI|KPA|INHG>\n"
    "  SYSTEM UNITS ELEVATION <M|FT>\n"
    "  SYSTEM UNITS SPEED <KPH|MPH>";
static const char PSTR_HELP_SYS_INTERVAL[] PROGMEM =
    "  SYSTEM INTERVAL SENSOR <ms>   Sensor read interval\n"
    "  SYSTEM INTERVAL ALARM <ms>    Alarm check interval";
static const char PSTR_HELP_SYS_REBOOT[] PROGMEM =
    "  SYSTEM REBOOT                 Restart the device";
static const char PSTR_HELP_SYS_RESET[] PROGMEM =
    "  SYSTEM RESET CONFIRM          Factory reset (erase config + reboot)";

// runModeAllowed: STATUS / PINS / DUMP are read-only and useful in RUN mode.
// SEA_LEVEL / UNITS / INTERVAL mutate persistent config; REBOOT / RESET are
// destructive. All four belong in CONFIG mode only.
static const Subcommand SYSTEM_SUBCOMMANDS[] PROGMEM = {
    { PSTR_SYS_STATUS,    system_status,    true,  PSTR_HELP_SYS_STATUS    },
    { PSTR_SYS_PINS,      system_pins,      true,  PSTR_HELP_SYS_PINS      },
    { PSTR_SYS_DUMP,      system_dump,      true,  PSTR_HELP_SYS_DUMP      },
    { PSTR_SYS_DIAG,      system_diag,      true,  PSTR_HELP_SYS_DIAG      },
    { PSTR_SYS_SEA_LEVEL, system_sea_level, false, PSTR_HELP_SYS_SEA_LEVEL },
    { PSTR_SYS_UNITS,     system_units,     false, PSTR_HELP_SYS_UNITS     },
    { PSTR_SYS_INTERVAL,  system_interval,  false, PSTR_HELP_SYS_INTERVAL  },
    { PSTR_SYS_REBOOT,    system_reboot,    false, PSTR_HELP_SYS_REBOOT    },
    { PSTR_SYS_RESET,     system_reset,     false, PSTR_HELP_SYS_RESET     },
};
static const uint8_t NUM_SYSTEM_SUBCOMMANDS = sizeof(SYSTEM_SUBCOMMANDS) / sizeof(Subcommand);

void printHelpSystem() {
    msg.control.println();
    msg.control.println(F("=== SYSTEM Commands ==="));
    msg.control.println(F("Global configuration affecting all subsystems"));
    msg.control.println();
    printSubcommandTable(SYSTEM_SUBCOMMANDS, NUM_SYSTEM_SUBCOMMANDS);
    msg.control.println();
}

int cmd_system(int argc, const char* const* argv) {
    if (argc < 2) {
        // Bare SYSTEM — same behavior as bare BUS: show full help from the
        // dispatch table rather than a hand-maintained one-line summary.
        printHelpSystem();
        return 0;
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
