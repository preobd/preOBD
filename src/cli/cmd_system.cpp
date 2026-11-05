/*
 * cmd_system.cpp - SYSTEM command (status, units, intervals, reboot, reset)
 */

#include "../config.h"

#include "command_handlers.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../inputs/input_manager.h"
#include "../storage/input_store.h"  // resetInputConfig (#402 step 2)
#include "input_display.h"
#include "../inputs/input.h"
#include "serial_config.h"
#include "../version.h"
#include "../core/system_mode.h"
#include "../core/reset_reason.h"
#include "../core/watchdog.h"   // WatchdogKickingPrint, DIAG WATCHDOG STALL
#include "../pinctrl/bus_pin_roles.h"   // busPinRole() for the DIAG PINS sweep (#304)
#include "../pinctrl/board_pins.h"     // the analog pins the DIAG PINS sweep walks (#570)
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
#include "../drivers/rgb_led.h"   // SYSTEM DIAG HW reports the LED (#419)
#include "../hal/bus_defaults.h"
#include "../storage/sd_manager.h"
#include "../drivers/serial_manager.h"
#include "../pinctrl/pin_registry.h"
#include "../outputs/output_base.h"
#include "../outputs/output_sdlog.h"
#include "../outputs/sd_status_json.h"
#include "../outputs/output_format.h"   // sdLogFormatName
#include "../outputs/obd_query.h"
#include "../outputs/display/display_manager.h"
#if INCLUDE_RELAY_OUTPUT
#endif
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
#if SUPPORTS_JSON_EXPORT
#include <ArduinoJson.h>
#endif

// FLASHMEM is a Teensy core macro — no-op elsewhere.
#ifndef FLASHMEM
#define FLASHMEM
#endif

// SYSTEM STATUS subcommand handler
static int system_status(int argc, const char* const* argv, int /*tokenIndex*/) {
    printSystemStatus();
    printResetReason(msg.control);
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

// SYSTEM SD [JSON] (#536): is the drive being logged? Read-only, RUN-legal,
// never bumps configGeneration, never touches the card — the app polls it
// while its System tab is open. SYSTEM DIAG HW is the probing version.
static FLASHMEM int system_sd(int argc, const char* const* argv, int /*tokenIndex*/) {
    const bool json = argc == 3 && streq_P(argv[2], PSTR("JSON"));
    if (argc != 2 && !json) {
        msg.control.println(F("ERROR: Usage: SYSTEM SD [JSON]"));
        return 1;
    }
    // A build without SD logging answers too ("none"), so a client polling
    // every board gets JSON back rather than an ERROR line.
    SdStatusSnapshot s;
    memset(&s, 0, sizeof(s));
#if INCLUDE_SD_LOGGING
    sdLogStatusSnapshot(s);
#endif
    if (json) {
#if SUPPORTS_JSON_EXPORT
        JsonDocument doc;
        sdStatusToJson(s, doc.to<JsonObject>());
        msg.control.println();
        serializeJson(doc, msg.control);
        msg.control.println();
        return 0;
#else
        msg.control.println(F("ERROR: JSON export not supported on this platform"));
        return 1;
#endif
    }
    // One line, from the same snapshot as the JSON so the two cannot disagree.
    const SdStatusState state = sdStatusState(s);
    msg.control.print(F("SD: "));
    msg.control.print(sdStatusStateName(state));
    if (state == SD_STATE_NONE) {
        msg.control.println(F(" (no SD logging in this build)"));
        return 0;
    }
    if (s.fileName) {
        msg.control.print(F(", "));
        msg.control.print(s.fileName);
    }
    msg.control.print(F(", "));
    msg.control.print((unsigned long)s.rows);
    msg.control.print(F(" rows, "));
    msg.control.print(s.errors);
    msg.control.print(F(" errors"));
    if (s.capacityKnown && state != SD_STATE_NOCARD && state != SD_STATE_REMOVED) {
        msg.control.print(F(", "));
        msg.control.print((unsigned long)(s.freeBytes / 1000000ULL));
        msg.control.print(F(" of "));
        msg.control.print((unsigned long)(s.totalBytes / 1000000ULL));
        msg.control.print(F(" MB free"));
    }
    msg.control.println();
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
    char* end;
    const float hPa = (float)strtod(argv[2], &end);   // strtod: avr-libc has no strtof
    if (end == argv[2] || *end != '\0' || !seaLevelPressureValid(hPa)) {
        msg.control.print(F("ERROR: '"));
        msg.control.print(argv[2]);
        msg.control.print(F("' is not a sea-level pressure; use "));
        msg.control.print((int)SEA_LEVEL_PRESSURE_MIN_HPA);
        msg.control.print(F("-"));
        msg.control.print((int)SEA_LEVEL_PRESSURE_MAX_HPA);
        msg.control.println(F(" hPa"));
        return 1;
    }
    systemConfig.seaLevelPressure = hPa;
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
    // Strict: getUnitsIndexByName() maps a typo to CELSIUS, which then passed
    // the TEMP check below (#458).
    uint8_t index = findUnitsIndexByName(unitStr);
    const UnitsInfo* info = (index == UNITS_NOT_FOUND) ? nullptr : getUnitsByIndex(index);

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
#if PLATFORM_HAS_BOOTLOADER_REBOOT
    if (argc == 3 && streq_P(argv[2], PSTR("BOOTLOADER"))) {
        msg.control.println(F("Rebooting into the USB DFU bootloader..."));
        platformRebootToBootloader();
        return 0;
    }
#endif
    msg.control.println(F("Rebooting system..."));
    platformReboot();
    return 0;
}

// SYSTEM RESET subcommand handler
//
//   SYSTEM RESET CONFIRM          full factory reset
//   SYSTEM RESET CONFIG CONFIRM   erase the configuration, keep the hardware setup (#551)
//
// Both keep the serial ports and the transport router: they are how this
// board is wired, and the reset may have arrived over a BLE bridge (nRF on
// Serial1 in framed mode, another port, another baud) — resetting them cuts
// the very link it came over (#549). The CONFIG form also keeps the buses (I2C/SPI/CAN selection,
// clocks, baud rates) and the display, so SPI/I2C sensors and the LCD still
// work after starting the configuration over.
static int system_reset(int argc, const char* const* argv, int /*tokenIndex*/) {
    const bool full = (argc == 3 && streq_P(argv[2], PSTR("CONFIRM")));
    const bool configOnly = (argc == 4 && streq_P(argv[2], PSTR("CONFIG")) && streq_P(argv[3], PSTR("CONFIRM")));
    if (full || configOnly) {
        msg.control.println(full ? F("Factory reset: Erasing all configuration...")
                                 : F("Erasing configuration (hardware setup kept)..."));
        {
            EepromBatch batch;   // one flash erase for both, on flash-backed stores
            resetInputConfig();
            resetSystemConfig(configOnly ? (RESET_KEEP_LINK | RESET_KEEP_HARDWARE) : RESET_KEEP_LINK);
            saveSystemConfig();
        }
        if (eepromCommitFailed()) {
            // The previous configuration is still in flash; rebooting would
            // bring it back, so stay up and say so.
            msg.control.println(F("ERROR: reset NOT saved; the stored configuration is unchanged"));
            msg.control.println(F("  Not rebooting. Retry, or SYSTEM REBOOT to return to the stored configuration"));
            return 1;
        }
        msg.control.println(F("Configuration reset complete"));
        msg.control.println(full ? F("Serial port settings kept (BUS SERIAL shows them)")
                                 : F("Serial ports, buses and display kept"));
        msg.control.println(F("Rebooting..."));
        platformReboot();
        return 0;
    }

    const bool askedConfig = (argc >= 3 && streq_P(argv[2], PSTR("CONFIG")));
    msg.control.println();
    msg.control.println(F("========================================"));
    if (askedConfig) {
        msg.control.println(F("  WARNING: Erase Configuration"));
        msg.control.println(F("  This will erase inputs, rules, actions"));
        msg.control.println(F("  and settings, keep serial ports, buses"));
        msg.control.println(F("  and display, and reboot the device"));
        msg.control.println(F("  Type: SYSTEM RESET CONFIG CONFIRM"));
    } else {
        msg.control.println(F("  WARNING: Factory Reset"));
        msg.control.println(F("  This will erase ALL configuration"));
        msg.control.println(F("  except serial port settings,"));
        msg.control.println(F("  and reboot the device"));
        msg.control.println(F("  Type: SYSTEM RESET CONFIRM"));
        msg.control.println(F("  To keep buses and display instead:"));
        msg.control.println(F("    SYSTEM RESET CONFIG CONFIRM"));
    }
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
#if INCLUDE_SELFTEST
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

#if INCLUDE_LED
    // What the LED is doing and which priority won. Without this, #419 --
    // SEVERITY_NORMAL claiming PRIORITY_WARNING and masking the CONFIG-mode
    // indication -- was only diagnosable by looking at the board, which is no
    // use over a serial link. F() here rather than a PROGMEM table: measured,
    // PROGMEM cost mega2560 88 bytes of RAM for no Teensy gain (the 1 KB RAM1
    // step this block adds on Teensy 4 is a DTCM allocation granule, not the
    // strings -- mega2560's byte-accurate RAM figure is unchanged).
    any = true;
    msg.control.print(F("[hardware]    RGB LED .............. "));
    {
        RGBColor c; RGBEffect e; RGBPriority p;
        if (!rgbLedGetActive(&c, &e, &p)) {
            msg.control.println(F("off"));
        } else {
            switch (e) {
                case EFFECT_SOLID: msg.control.print(F("solid")); break;
                case EFFECT_BLINK: msg.control.print(F("blink")); break;
                case EFFECT_PULSE: msg.control.print(F("pulse")); break;
                default:           msg.control.print(F("?")); break;
            }
            msg.control.print(F(" rgb("));
            msg.control.print((int)c.r); msg.control.print(',');
            msg.control.print((int)c.g); msg.control.print(',');
            msg.control.print((int)c.b);
            msg.control.print(F(") @ "));
            switch (p) {
                case PRIORITY_IDLE:     msg.control.println(F("IDLE")); break;
                case PRIORITY_MODE:     msg.control.println(F("MODE")); break;
                case PRIORITY_ACTIVITY: msg.control.println(F("ACTIVITY")); break;
                case PRIORITY_WARNING:  msg.control.println(F("WARNING")); break;
                case PRIORITY_ALARM:    msg.control.println(F("ALARM")); break;
                default:                msg.control.println(F("?")); break;
            }
        }
    }
#endif

#if INCLUDE_LCD
    any = true;
    {
    // The address the LCD is driven at now (#590): a DISPLAY ADDRESS change
    // waits for the next boot.
    extern uint8_t lcdDrivenAddress();
    const uint8_t lcdAddr = lcdDrivenAddress();
    msg.control.print(F("[hardware]    LCD (I2C 0x"));
    msg.control.print(lcdAddr, HEX);
    msg.control.print(F(") ....... "));
    switch (i2cProbe(lcdAddr)) {
        case 1:  msg.control.println(F("OK")); break;
        case 0:  msg.control.println(F("FAIL (no ACK)")); break;
        default: msg.control.println(F("off (no I2C bus)")); break;
    }
    }
#endif

#if INCLUDE_OLED
    any = true;
    msg.control.print(F("[hardware]    OLED (I2C 0x3C) ...... "));
    switch (i2cProbe(0x3C)) {
        case 1:  msg.control.println(F("OK")); break;
        case 0:  msg.control.println(F("FAIL (no ACK)")); break;
        default: msg.control.println(F("off (no I2C bus)")); break;
    }
#endif

#if INCLUDE_BME280
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
    // isSDInitialized() is a boot-time latch, so it kept reporting OK for a card
    // that had since been pulled (#331). Probe the slot where the platform's SD
    // library can; where it cannot, the boot-time answer is all there is.
    msg.control.print(F("[hardware]    SD card .............. "));
    if (!isSDInitialized()) {
        msg.control.println(F("FAIL (not mounted)"));
    } else if (!sdMediaPresent()) {
        msg.control.println(F("FAIL (removed since boot)"));
    } else {
        msg.control.println(F("OK"));
    }
#endif

#if INCLUDE_SD_LOGGING
    // A mounted card says nothing about whether rows are actually landing: a
    // full card, or one pulled mid-drive, fails every write while the output
    // still reports "Enabled" in OUTPUT STATUS.
    msg.control.print(F("[hardware]    SD logging ........... "));
    if (sdLogCardRemoved()) {
        // Detected by the pre-write probe, so no write was ever attempted and
        // the error count is legitimately zero — say why, rather than printing
        // "FAULTED after 0 write error(s)" and looking like a lost reason.
        msg.control.print(F("FAULTED (card removed after "));
        msg.control.print((unsigned long)sdLogRowsWritten());
        msg.control.println(F(" rows)"));
    } else if (sdLogIsFaulted()) {
        msg.control.print(F("FAULTED after "));
        msg.control.print(sdLogWriteErrors());
        msg.control.println(F(" write error(s)"));
    } else if (sdLogIsActive()) {
        msg.control.print(F("OK ("));
        msg.control.print(sdLogFileName());
        msg.control.print(F(", "));
        msg.control.print(sdLogFormatName(systemConfig.sdLogFormat));
        msg.control.print(F(", "));
        msg.control.print((unsigned long)sdLogRowsWritten());
        msg.control.print(F(" rows, "));
        msg.control.print(sdLogWriteErrors());
        msg.control.println(F(" errors)"));
    } else {
        msg.control.print(F("off (output disabled or no file open), format "));
        msg.control.println(sdLogFormatName(systemConfig.sdLogFormat));
    }
#endif

    if (!any) {
        msg.control.println(F("[hardware]    (no probeable peripherals in this build)"));
    }
}

// Diagnostics are serial-command driven, never hot-path. On Teensy 4.x code
// defaults to fast ITCM (RAM1, scarce); keep these helpers in FLASH instead.

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
        platformAnalogRead(in->pin);                 // discard: mux settling
        p->rawAdc = platformAnalogRead(in->pin);
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
        case MEASURE_DISTANCE:    return F("km");
        case MEASURE_TIME:        return F("s");
        case MEASURE_MASS_FLOW:   return F("g/s");
        case MEASURE_FUEL_RATE:   return F("L/h");
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
        case MEASURE_HUMIDITY:    return v >= 0.0f    && v <= 100.0f;
        case MEASURE_PERCENT:     return v >= -125.0f && v <= 130.0f;   // J1979 signed % (0x8E torque)
        case MEASURE_ELEVATION:   return v >= -500.0f && v <= 10000.0f;
        case MEASURE_DISTANCE:    return v >= 0.0f    && v <= 429496730.0f;  // J1979 odometer max, km
        case MEASURE_TIME:        return v >= 0.0f    && v <= 3932100.0f;    // 65535 min, s
        case MEASURE_MASS_FLOW:   return v >= 0.0f    && v <= 2048.0f;       // g/s
        case MEASURE_FUEL_RATE:   return v >= 0.0f    && v <= 3276.75f;      // L/h
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
// analog pin (analogPinForIndex(), pinctrl/board_pins) and reports its live state,
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

// Three-way pin classification shared by the text sweep and its JSON twin
// (SYSTEM DIAG PINS [JSON]) so the two can never disagree about a pin.
enum PinDiscovery : uint8_t { PD_FLOATING, PD_PULLED_LOW, PD_PULLED_HIGH, PD_DRIVEN };

// The enabled Input configured on this pin, if any. Enabled analog Inputs
// self-track rather than registering in the pin registry, so this lookup and
// getPinUsage() are disjoint by construction.
static FLASHMEM Input* diagInputOnPin(uint8_t pin) {
    for (uint8_t k = 0; k < MAX_INPUTS; k++) {
        if (inputs[k].flags.isEnabled && inputs[k].pin == pin) {
            return &inputs[k];
        }
    }
    return nullptr;
}

// Discovery-candidate sampler. Two throwaway reads with a settle defuse
// sequential-mux ghosting (a floating high-Z pin can otherwise hold a
// neighbour's charge and masquerade as driven).
static FLASHMEM PinDiscovery diagSamplePin(uint8_t pin, int& last, int& jitter) {
    platformAnalogRead(pin);
    delayMicroseconds(20);
    platformAnalogRead(pin);
    int rawMin = (int)ADC_MAX_VALUE + 1, rawMax = -1;
    last = 0;
    for (uint8_t s = 0; s < 8; s++) {
        int r = platformAnalogRead(pin);
        last = r;
        if (r < rawMin) rawMin = r;
        if (r > rawMax) rawMax = r;
        delayMicroseconds(20);
    }
    jitter = rawMax - rawMin;
    const int railLo = (int)(ADC_MAX_VALUE * 0.02f);
    const int railHi = (int)(ADC_MAX_VALUE * 0.98f);
    if (jitter > JITTER_FLOATING_COUNTS) return PD_FLOATING;
    if (last <= railLo)                  return PD_PULLED_LOW;
    if (last >= railHi)                  return PD_PULLED_HIGH;
    return PD_DRIVEN;
}

static FLASHMEM void diagSectionPins() {
    msg.control.println(F("=== ANALOG PIN SWEEP ==="));
    for (uint8_t i = 0; i < analogPinCount(); i++) {
        uint8_t pin = analogPinForIndex(i);

        // 1) Configured analog Input on this pin? Reuse diagProbe so DIAG PINS and
        //    DIAG INPUTS never disagree about the same pin.
        Input* owner = diagInputOnPin(pin);
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

        // 3) Discovery candidate — sample the raw pin.
        int last, jitter;
        PinDiscovery d = diagSamplePin(pin, last, jitter);
        float pctFS   = 100.0f * last / (float)ADC_MAX_VALUE;
        float voltage = (float)last * AREF_VOLTAGE / (float)ADC_MAX_VALUE;

        const __FlashStringHelper* verdict;
        switch (d) {
            case PD_PULLED_LOW:  verdict = F("pulled low");  break;
            case PD_PULLED_HIGH: verdict = F("pulled high"); break;
            case PD_DRIVEN:      verdict = F("driven");      break;
            default:             verdict = F("floating?");   break;
        }

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
        if (d == PD_FLOATING) {
            msg.control.print(F("  jitter "));
            msg.control.print(jitter);
        }
        msg.control.print(F(")"));

        // This pin is PIN_UNUSED by construction (branch 2 above already
        // returned otherwise), so no bus currently holds it — but it may
        // still be a platform's hard-wired I2C/SPI/CAN pin that simply has
        // no active bus right now (never selected, or selected but not yet
        // started). Say what the pin IS, not why it reads this way: #304.
        const __FlashStringHelper* role = busPinRole(pin);
        if (role) {
            msg.control.print(F("  ["));
            msg.control.print(role);
            msg.control.print(F(" - bus not active]"));
        }
        msg.control.println();
    }
}

#if SUPPORTS_JSON_EXPORT
// JSON twin of diagSectionPins for machine consumers (mobile app pin picker).
// Same three-way classification: {"label":"A3","pin":17,"state":"input","owner":"EOP"}
// | {"state":"in_use","usage":"Reserved"} | {"state":"floating|pulled_low|
// pulled_high|driven","raw":…,"voltage":…,"jitter":…}. Clients treat any
// discovery state as an open-pin candidate.
static FLASHMEM void diagSectionPinsJson(Print& out) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < analogPinCount(); i++) {
        uint8_t pin = analogPinForIndex(i);
        JsonObject obj = arr.add<JsonObject>();
        char label[6];
        snprintf(label, sizeof(label), "A%u", (unsigned)i);
        obj["label"] = label;  // non-const char* → copied into the document
        obj["pin"]   = pin;

        Input* owner = diagInputOnPin(pin);
        if (owner) {
            obj["state"] = "input";
            obj["owner"] = owner->abbrName;
            continue;
        }
        PinUsageType usage = getPinUsage(pin);
        if (usage != PIN_UNUSED) {
            obj["state"] = "in_use";
            obj["usage"] = getPinUsageTypeName(usage);
            continue;
        }
        int last, jitter;
        PinDiscovery d = diagSamplePin(pin, last, jitter);
        switch (d) {
            case PD_PULLED_LOW:  obj["state"] = "pulled_low";  break;
            case PD_PULLED_HIGH: obj["state"] = "pulled_high"; break;
            case PD_DRIVEN:      obj["state"] = "driven";      break;
            default:             obj["state"] = "floating";    break;
        }
        obj["raw"]     = last;
        obj["voltage"] = (float)last * AREF_VOLTAGE / (float)ADC_MAX_VALUE;
        obj["jitter"]  = jitter;
        // Additive, optional field for clients: this platform's hard-wired
        // bus role for the pin, if any, regardless of whether that bus is
        // currently running. Omitted (not null) when there is no role.
        const __FlashStringHelper* role = busPinRole(pin);
        if (role) {
            obj["busRole"] = role;
        }
    }
    WatchdogKickingPrint wdOut(out);
    serializeJson(doc, wdOut);
}
#endif // SUPPORTS_JSON_EXPORT

#if INCLUDE_WATCHDOG_STALL
// SYSTEM DIAG WATCHDOG STALL <ms> [SCOPE <ms>] (#543, bench builds only):
// busy-wait without feeding, optionally inside a WatchdogScope, then report. If
// the watchdog works, a stall past its timeout never returns: the board resets
// and VERSION shows WATCHDOG (on Teensy 4 with WRSR 0x2, TOUT).
// tools/watchdog_bench.py runs the checks.
//
// Works in both modes: the watchdog runs in CONFIG too (8 s there, #541), so
// the expected outcome follows the mode's timeout.
static FLASHMEM int diagWatchdogStall(int argc, const char* const* argv) {
    const bool scoped = argc == 7 && streq_P(argv[5], PSTR("SCOPE"));
    if (argc < 5 || !streq_P(argv[3], PSTR("STALL")) || (argc != 5 && !scoped)) {
        msg.control.println(F("ERROR: Usage: SYSTEM DIAG WATCHDOG STALL <ms> [SCOPE <ms>]"));
        return 1;
    }
    const uint32_t ms = strtoul(argv[4], nullptr, 10);
    const uint32_t scopeMs = scoped ? strtoul(argv[6], nullptr, 10) : 0;
    if (ms == 0 || ms > 120000UL || (scoped && (scopeMs == 0 || scopeMs > 65535UL))) {
        msg.control.println(F("ERROR: STALL takes 1..120000 ms, SCOPE 1..65535 ms"));
        return 1;
    }
    msg.control.print(F("Stalling "));
    msg.control.print((unsigned long)ms);
    if (scoped) {
        msg.control.print(F(" ms inside a "));
        msg.control.print((unsigned long)scopeMs);
        msg.control.println(F(" ms scope, not feeding"));
    } else {
        msg.control.println(F(" ms, not feeding"));
    }
    // Get the line out before a reset can eat it, on whichever transport asked.
    if (TransportInterface* t = router.getActiveControlTransport()) t->flush();
    // Fed once here, so the stall is measured from now, not from the last loop pass.
    watchdogReset();
    {
        // Constructed either way; a 0 ms scope never widens anything.
        WatchdogScope wd((uint16_t)scopeMs, "STALL");
        const uint32_t start = millis();
        while (millis() - start < ms) { }
    }
    msg.control.print(F("Stall survived ("));
    msg.control.print((unsigned long)ms);
    msg.control.println(F(" ms)"));
    return 0;
}
#endif

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
        if (streq_P(argv[2], PSTR("PINS"))) {
            if (argc >= 4) {
                if (!streq_P(argv[3], PSTR("JSON")) || argc != 4) {
                    // Reject unknown options and trailing junk alike (mirrors
                    // the DIAG INPUTS option validation). The offending token
                    // is argv[3] unless it was a valid JSON with junk after.
                    msg.control.print(F("ERROR: Unknown DIAG PINS option '"));
                    msg.control.print(argv[streq_P(argv[3], PSTR("JSON")) ? 4 : 3]);
                    msg.control.println(F("'"));
                    msg.control.println(F("  Valid: JSON, or bare PINS for the text sweep"));
                    return 1;
                }
#if SUPPORTS_JSON_EXPORT
                msg.control.println();
                diagSectionPinsJson(msg.control);
                msg.control.println();
                return 0;
#else
                msg.control.println(F("ERROR: JSON export not supported on this platform"));
                return 1;
#endif
            }
            diagSectionPins();
            return 0;
        }
#if INCLUDE_WATCHDOG_STALL
        if (streq_P(argv[2], PSTR("WATCHDOG"))) return diagWatchdogStall(argc, argv);
#endif
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
static const char PSTR_SYS_SD[] PROGMEM = "SD";
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
    "  SYSTEM DIAG PINS              Sweep all analog pins (live raw/voltage; find wiring)\n"
    "  SYSTEM DIAG PINS JSON         Same sweep as JSON (mobile app pin picker)"
#if INCLUDE_WATCHDOG_STALL
    "\n  SYSTEM DIAG WATCHDOG STALL <ms> [SCOPE <ms>]  Bench only: stall without feeding (resets the board)"
#endif
    ;
static const char PSTR_HELP_SYS_SD[] PROGMEM =
    "  SYSTEM SD                     SD card + logging status (no card access)\n"
    "  SYSTEM SD JSON                Same as JSON (mobile app System tab)";
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
    "  SYSTEM REBOOT                 Restart the device"
#if PLATFORM_HAS_BOOTLOADER_REBOOT
    "\n  SYSTEM REBOOT BOOTLOADER      Restart into the USB DFU bootloader (firmware upload)"
#endif
    ;
static const char PSTR_HELP_SYS_RESET[] PROGMEM =
    "  SYSTEM RESET CONFIRM          Factory reset (erase config + reboot)\n"
    "  SYSTEM RESET CONFIG CONFIRM   Erase config, keep buses/serial/display";

// runModeAllowed: STATUS / PINS / DUMP / DIAG / SD are read-only and useful in RUN mode.
// SEA_LEVEL / UNITS / INTERVAL mutate persistent config; REBOOT / RESET are
// destructive. All four belong in CONFIG mode only.
static const Subcommand SYSTEM_SUBCOMMANDS[] PROGMEM = {
    { PSTR_SYS_STATUS,    system_status,    true,  PSTR_HELP_SYS_STATUS    },
    { PSTR_SYS_PINS,      system_pins,      true,  PSTR_HELP_SYS_PINS      },
    { PSTR_SYS_DUMP,      system_dump,      true,  PSTR_HELP_SYS_DUMP      },
    { PSTR_SYS_DIAG,      system_diag,      true,  PSTR_HELP_SYS_DIAG      },
    { PSTR_SYS_SD,        system_sd,        true,  PSTR_HELP_SYS_SD        },
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


#if INCLUDE_SELFTEST
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
