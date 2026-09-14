/*
 * system_config.cpp - System Configuration Implementation
 */

#include "system_config.h"
#include "../config.h"
#include "../sensors/units_registry.h"
#include "message_router.h"  // For TransportID enum
#include "bus_defaults.h"
#include "bus_wiring.h"
#include "bus_manager.h"   // getI2CBusName()/getSPIBusName() — Tier-1 wired-bus reservation (#379)
#include "bus_needs.h"     // BUS_NEED_I2C/BUS_NEED_SPI — Tier-1 gated on the stored vote (#394)
#include "message_api.h"
#include "log_tags.h"
#include "pin_registry.h"
#include "pin_owner.h"
#include "../outputs/output_format.h"  // SdLogFormat
#include <EEPROM.h>

// Global system config instance
SystemConfig systemConfig;

// Boot diagnostics buffer — loadSystemConfig() runs before the message router is
// up, so msg.* calls would be silently dropped. Store the events here and flush
// them via flushSystemConfigBootDiagnostics() once router.begin() has been called.
static struct {
    enum : uint8_t {
        DIAG_NONE             = 0x00,
        DIAG_LOADED_OK        = 0x01,
        DIAG_USED_DEFAULTS    = 0x02,
        DIAG_BAD_MAGIC        = 0x04,
        DIAG_VERSION_MISMATCH = 0x08,
        DIAG_CHECKSUM_FAILED  = 0x10,
    };
    uint8_t flags            = 0;
    uint8_t loadedVersion    = 0;
    uint8_t storedChecksum   = 0;
    uint8_t calcChecksum     = 0;

    // reportBuiltinPinCollision()'s annotation line (#396) — registerSystemPins()
    // runs in the same pre-router-transport window as loadSystemConfig(), so its
    // "build-time pin collision" context is lost the same way. One bit per call
    // site below is enough (there are exactly 8, all fixed at compile time) —
    // no array/capacity needed the way pin_owner.cpp's general buffer does.
    uint8_t pinCollisionFlags = 0;
} bootDiag;

enum BuiltinPinKind : uint8_t {
    BPK_MODE_BUTTON = 0, BPK_BUZZER, BPK_CAN_CS, BPK_CAN_INT,
    BPK_SD, BPK_TEST_MODE, BPK_I2C_WIRED, BPK_SPI_WIRED,
};

// Forward declaration: the definition (below, beside registerSystemPins())
// always prints immediately — flushSystemConfigBootDiagnostics() (here) is
// the deferred half of the same message, so it needs the symbol early.
static void reportBuiltinPinCollisionNow(const __FlashStringHelper* what);

void flushSystemConfigBootDiagnostics() {
    if (bootDiag.flags & bootDiag.DIAG_BAD_MAGIC) {
        msg.debug.warn(TAG_SYSTEM, "System config: bad magic, used defaults");
    }
    if (bootDiag.flags & bootDiag.DIAG_VERSION_MISMATCH) {
        msg.debug.warn(TAG_SYSTEM, "System config: version mismatch (got %u, need %u), used defaults",
                       bootDiag.loadedVersion, SYSTEM_CONFIG_VERSION);
    }
    if (bootDiag.flags & bootDiag.DIAG_CHECKSUM_FAILED) {
        msg.debug.warn(TAG_SYSTEM, "System config: checksum failed (got 0x%02X, calc 0x%02X), used defaults",
                       bootDiag.storedChecksum, bootDiag.calcChecksum);
    }
    if (bootDiag.flags & bootDiag.DIAG_LOADED_OK) {
        msg.debug.info(TAG_SYSTEM, "System config loaded from EEPROM (v%u)", systemConfig.version);
    }
    if (bootDiag.flags & bootDiag.DIAG_USED_DEFAULTS) {
        msg.debug.info(TAG_SYSTEM, "Using default system config");
    }
    bootDiag.flags = 0;

    // #396: replay any build-time pin-collision annotations buffered by
    // reportBuiltinPinCollision() during registerSystemPins(). Guarded by the
    // same #if as the call site in registerSystemPins() below, so a board that
    // never compiles a given claim never carries its (unused) flag bit's flash
    // cost either.
#if INCLUDE_MODE_BUTTON
    if (bootDiag.pinCollisionFlags & (1u << BPK_MODE_BUTTON))
        reportBuiltinPinCollisionNow(F("MODE_BUTTON_PIN"));
#endif
#if INCLUDE_ALARMS && defined(BUZZER_PIN)
    if (bootDiag.pinCollisionFlags & (1u << BPK_BUZZER))
        reportBuiltinPinCollisionNow(F("BUZZER_PIN"));
#endif
#if (INCLUDE_CAN_HYBRID || !PROFILE_HAS_NATIVE_CAN) && defined(CAN_CS_0) && (CAN_CS_0 != 0xFF)
    if (bootDiag.pinCollisionFlags & (1u << BPK_CAN_CS))
        reportBuiltinPinCollisionNow(F("CAN_CS_0"));
#endif
#if (INCLUDE_CAN_HYBRID || !PROFILE_HAS_NATIVE_CAN) && defined(CAN_INT_0) && (CAN_INT_0 != 0xFF)
    if (bootDiag.pinCollisionFlags & (1u << BPK_CAN_INT))
        reportBuiltinPinCollisionNow(F("CAN_INT_0"));
#endif
#if SUPPORTS_SD
    if (bootDiag.pinCollisionFlags & (1u << BPK_SD))
        reportBuiltinPinCollisionNow(F("SD_PIN"));
#endif
#if INCLUDE_TEST_MODE && defined(TEST_MODE_PIN)
    if (bootDiag.pinCollisionFlags & (1u << BPK_TEST_MODE))
        reportBuiltinPinCollisionNow(F("TEST_MODE_PIN"));
#endif
#if BUS_WIRED_I2C_ID != 0xFF
    if (bootDiag.pinCollisionFlags & (1u << BPK_I2C_WIRED))
        reportBuiltinPinCollisionNow(F("I2C_WIRED_BUS"));
#endif
#if BUS_WIRED_SPI_ID != 0xFF
    if (bootDiag.pinCollisionFlags & (1u << BPK_SPI_WIRED))
        reportBuiltinPinCollisionNow(F("SPI_WIRED_BUS"));
#endif
    bootDiag.pinCollisionFlags = 0;
}

void printSystemStatus() {
    msg.control.println(F("=== System Configuration ==="));

    msg.control.print(F("Sea Level Pressure: "));
    msg.control.print(systemConfig.seaLevelPressure);
    msg.control.println(F(" hPa"));

    msg.control.print(F("Global Intervals: Sensor="));
    msg.control.print(systemConfig.sensorReadInterval);
    msg.control.print(F("ms, Alarm="));
    msg.control.print(systemConfig.alarmCheckInterval);
    msg.control.println(F("ms"));

    msg.control.print(F("Default Units: Temp="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultTempUnits));
    msg.control.print(F(", Pressure="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultPressUnits));
    msg.control.print(F(", Elevation="));
    msg.control.print((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultElevUnits));
    msg.control.print(F(", Speed="));
    msg.control.println((__FlashStringHelper*)getUnitStringByIndex(systemConfig.defaultSpeedUnits));
}

/**
 * Calculate XOR checksum for SystemConfig structure
 */
uint8_t calculateChecksum(SystemConfig* cfg) {
    uint8_t checksum = 0;
    uint8_t* bytes = (uint8_t*)cfg;

    // XOR all bytes except the checksum field itself
    for (size_t i = 0; i < sizeof(SystemConfig); i++) {
        if (i != offsetof(SystemConfig, checksum)) {
            checksum ^= bytes[i];
        }
    }

    return checksum;
}

/**
 * Initialize system configuration
 * Try loading from EEPROM, fallback to defaults
 */
void initSystemConfig() {
    if (loadSystemConfig()) {
        bootDiag.flags |= bootDiag.DIAG_LOADED_OK;
        return;
    }
    bootDiag.flags |= bootDiag.DIAG_USED_DEFAULTS;
    resetSystemConfig();
}

/**
 * Reset system configuration to defaults
 */
void resetSystemConfig() {
    systemConfig.magic = SYSTEM_CONFIG_MAGIC;
    systemConfig.version = SYSTEM_CONFIG_VERSION;

    // Output defaults. INCLUDE_* only says the module is in the image; these
    // outputEnabled[] values are what actually turns it on at runtime.
    // Data outputs default OFF to keep USB clean - user must explicitly enable
    // Safety outputs default ON when compiled in

    #if INCLUDE_CAN
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_CAN] = CAN_OUTPUT_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_CAN] = 0;
    systemConfig.outputInterval[OUTPUT_CAN] = 100;
    #endif

    #if INCLUDE_REALDASH
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;  // OFF - data plane output
    systemConfig.outputInterval[OUTPUT_REALDASH] = REALDASH_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_REALDASH] = 0;
    systemConfig.outputInterval[OUTPUT_REALDASH] = 100;
    #endif

    #if INCLUDE_SERIAL_OUTPUT
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;  // OFF - clogs USB serial
    systemConfig.outputInterval[OUTPUT_SERIAL] = SERIAL_CSV_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SERIAL] = 0;
    systemConfig.outputInterval[OUTPUT_SERIAL] = 1000;
    #endif

    #if INCLUDE_SD_LOGGING
    systemConfig.outputEnabled[OUTPUT_SD] = 0;  // OFF - requires SD card hardware
    systemConfig.outputInterval[OUTPUT_SD] = SD_LOG_INTERVAL_MS;
    #else
    systemConfig.outputEnabled[OUTPUT_SD] = 0;
    systemConfig.outputInterval[OUTPUT_SD] = 5000;
    #endif

    #if INCLUDE_ALARMS
    systemConfig.outputEnabled[OUTPUT_ALARM] = 1;  // ON - safety critical
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;  // 10Hz check rate
    #else
    systemConfig.outputEnabled[OUTPUT_ALARM] = 0;
    systemConfig.outputInterval[OUTPUT_ALARM] = 100;
    #endif

    #if INCLUDE_RELAY_OUTPUT
    systemConfig.outputEnabled[OUTPUT_RELAY] = 1;  // ON - user-configured relay control
    systemConfig.outputInterval[OUTPUT_RELAY] = 100;  // 10Hz check rate
    #endif

    // Display defaults.
    //
    // INCLUDE_LCD says the driver is in the image; it cannot say whether a
    // screen is plugged in, and nothing at compile time can. So the default must
    // be NONE: assuming a display would make every capable board reserve SDA/SCL
    // (18/19 = A4/A5 on Teensy 4.x) whether or not one exists, denying those pins
    // to inputs (#351). A display is declared at runtime with `DISPLAY TYPE LCD`,
    // which is also what lets you attach one to a running unit without a reflash.
    //
    // A stored config still wins; this is only the fresh-EEPROM default.
    #if defined(INCLUDE_OLED)
        systemConfig.displayEnabled = 1;
        systemConfig.displayType = DISPLAY_OLED;
    #else
        systemConfig.displayEnabled = 1;   // on/off toggle; type is what gates bring-up
        systemConfig.displayType = DISPLAY_NONE;
    #endif

    systemConfig.lcdI2CAddress = 0x27;

    // Resolve unit names to indices at boot time
    systemConfig.defaultTempUnits = getUnitsIndexByName(DEFAULT_TEMPERATURE_UNITS);
    systemConfig.defaultPressUnits = getUnitsIndexByName(DEFAULT_PRESSURE_UNITS);
    systemConfig.defaultElevUnits = getUnitsIndexByName(DEFAULT_ELEVATION_UNITS);
    systemConfig.defaultSpeedUnits = getUnitsIndexByName(DEFAULT_SPEED_UNITS);

    // Timing intervals
    systemConfig.sensorReadInterval = SENSOR_READ_INTERVAL_MS;
    systemConfig.alarmCheckInterval = ALARM_CHECK_INTERVAL_MS;
    systemConfig.lcdUpdateInterval = LCD_UPDATE_INTERVAL_MS;
    systemConfig.sdLogFormat = SDLOG_FORMAT_WIDE;
    systemConfig.reserved1 = 0;

    // Physical constants
    systemConfig.seaLevelPressure = SEA_LEVEL_PRESSURE_HPA;

    // Transport Router Configuration (NEW in v4)
    // Default: All planes → USB Serial
    systemConfig.router.control_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.control_secondary = TRANSPORT_SERIAL1;
    systemConfig.router.data_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.data_secondary = TRANSPORT_NONE;
    systemConfig.router.debug_primary = TRANSPORT_USB_SERIAL;
    systemConfig.router.debug_secondary = TRANSPORT_NONE;

    // Bluetooth defaults (disabled)
    systemConfig.router.bt_type = 0;  // BT_TYPE_NONE
    systemConfig.router.bt_auth_required = 0;  // Disabled by default
    systemConfig.router.bt_pin = 0;  // Not set

    // Reserved router space
    for (int i = 0; i < 6; i++) {
        systemConfig.router.reserved_router[i] = 0;
    }

    // NB (v13): the control-engine Rule/Action tables are no longer part of
    // SystemConfig — they are reset via controlResetTables() and persisted by the
    // front config-store arena (input_manager.cpp), independent of this block.

    // Bus Configuration defaults (simplified "pick one" model)
    systemConfig.buses.active_i2c = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.i2c_clock = DEFAULT_I2C_CLOCK;
    systemConfig.buses.active_spi = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.spi_clock = DEFAULT_SPI_CLOCK;

    // CAN configuration.
    //
    // Output used to default ON (output_can_bus = DEFAULT_CAN_BUS, bus 0) on
    // every board unconditionally — an INFERENCE that a transceiver is
    // attached (#377). On teensy40 that claimed pins 22/23 for CAN1 with
    // nothing wired there, losing two analog pins to a guess. Now it is earned
    // by a build-time CAN_WIRED_BUS declaration (boards/<board>/wiring.ini or
    // board.h for a purpose-built PCB) — same rule as I2C/SPI (bus_wiring.h).
    // A declaration GRANTS PERMISSION; `BUS CAN <n>` still overrides it either
    // way. This is a DEFAULTS-only change (no BusConfig layout change), so no
    // EEPROM version bump: a board with a stored config keeps whatever it has;
    // only fresh boards and SYSTEM RESET see the new default.
    systemConfig.buses.input_can_bus = 0xFF;  // Disabled by default
#if BUS_WIRED_CAN_ID != 0xFF
    systemConfig.buses.output_can_bus = BUS_WIRED_CAN_ID;
    systemConfig.buses.can_output_enabled = 1;   // today's behavior, now earned
#else
    systemConfig.buses.output_can_bus = 0xFF;
    systemConfig.buses.can_output_enabled = 0;
#endif
    systemConfig.buses.can_input_baudrate = DEFAULT_CAN_BAUDRATE;
    systemConfig.buses.can_output_baudrate = DEFAULT_CAN_BAUDRATE;
    systemConfig.buses.can_input_mode = CAN_INPUT_OFF;  // Disabled by default
    systemConfig.buses.elm327_serial_port = 0xFF;  // Disabled by default

    // Serial Port Configuration defaults
    // USB Serial is always available; Serial1 enabled by default, others disabled
    systemConfig.serial.enabled_mask = 0x01;  // Bit 0 = Serial1 enabled
    for (int i = 0; i < 8; i++) {
        systemConfig.serial.baudrate_index[i] = BAUD_115200;  // Default 115200 baud
    }
    systemConfig.serial.framed_enabled_mask = 0;  // transparent mode default
    for (size_t i = 0; i < sizeof(systemConfig.serial.reserved); i++) {
        systemConfig.serial.reserved[i] = 0;
    }

    // Log Filter Configuration defaults
    // Default to INFO level (show ERROR, WARN, INFO but not DEBUG)
    systemConfig.logFilter.control_level = 3;  // LOG_LEVEL_INFO
    systemConfig.logFilter.data_level = 3;     // LOG_LEVEL_INFO
    systemConfig.logFilter.debug_level = 3;    // LOG_LEVEL_INFO
    systemConfig.logFilter.enabledTags = 0xFFFFFFFF;  // All tags enabled
    for (int i = 0; i < 5; i++) {
        systemConfig.logFilter.reserved[i] = 0;
    }

    // Calculate checksum
    systemConfig.checksum = calculateChecksum(&systemConfig);
}

/**
 * Save system configuration to EEPROM
 * @return true if successful
 */
bool saveSystemConfig(bool verbose) {
    // Update checksum before saving
    systemConfig.checksum = calculateChecksum(&systemConfig);

    // Write to EEPROM
    EEPROM.put(SYSTEM_CONFIG_ADDRESS, systemConfig);

    if (verbose) {
        msg.control.print(F("✓ System config saved (addr=0x"));
        char buf[8];
        snprintf(buf, sizeof(buf), "%04X", (unsigned)SYSTEM_CONFIG_ADDRESS);
        msg.control.print(buf);
        msg.control.print(F(", v"));
        msg.control.print(systemConfig.version);
        msg.control.print(F(", cksum=0x"));
        snprintf(buf, sizeof(buf), "%02X", systemConfig.checksum);
        msg.control.println(buf);
    } else {
        msg.debug.info(TAG_SYSTEM, "Config saved (addr=0x%04X, v%u, cksum=0x%02X)",
                       (unsigned)SYSTEM_CONFIG_ADDRESS, systemConfig.version, systemConfig.checksum);
    }
    return true;
}

// Gap 5 (pin ownership design audit): every claimPins() call below used to
// discard its return value, so two build-time pins colliding in wiring.ini /
// board.h (e.g. BUZZER_PIN == MODE_BUTTON_PIN) was accepted in silence — the
// second claimant simply never existed, with no diagnostic pointing at why.
// claimPins() already prints the generic pin-ownership refusal; this adds the
// one thing that's missing — that the SOURCE was a build-time declaration, so
// the fix is wiring.ini/board.h, not a runtime SET/BUS command.
// __FlashStringHelper*, not const char*: every call site below passes an F()
// literal, and a plain const char* parameter would still cost AVR RAM for the
// literal even though the callee never mutates it — F() only keeps a string
// in flash when both the literal AND everything that touches it agree to.
static void reportBuiltinPinCollisionNow(const __FlashStringHelper* what) {
    msg.control.print(F("ERROR: build-time pin collision reserving "));
    msg.control.print(what);
    msg.control.println(F(" — check wiring.ini / board.h for a duplicate pin"));
}

// registerSystemPins() runs before router.begin() (main.cpp), same as
// loadSystemConfig() — printing straight to msg.control here is the other
// half of #396's silent-discard bug. pin_owner.h's pinOwnerBootDiagBuffering()
// is the single source of truth for "is the router transport-less right now";
// buffer here too so this annotation and pin_owner's own conflict message
// (buffered separately, pin_owner.cpp) both survive to flush time instead of
// only one of them being fixed.
static void reportBuiltinPinCollision(const __FlashStringHelper* what, uint8_t bit) {
    if (pinOwnerBootDiagBuffering()) {
        bootDiag.pinCollisionFlags |= (uint8_t)(1u << bit);
    } else {
        reportBuiltinPinCollisionNow(what);
    }
}

/**
 * Register system pins in the pin registry
 * This reserves these pins and makes them visible in the registry export
 */
void registerSystemPins(uint8_t busNeeds) {
#if INCLUDE_MODE_BUTTON
    { const uint8_t p = MODE_BUTTON_PIN;
      if (!claimPins(&p, 1, PIN_BUTTON, PIN_RANK_BUILTIN, TAG_SYSTEM, "Mode Button"))
          reportBuiltinPinCollision(F("MODE_BUTTON_PIN"), BPK_MODE_BUTTON); }
#endif

#if INCLUDE_ALARMS
    #ifdef BUZZER_PIN
    { const uint8_t p = BUZZER_PIN;
      if (!claimPins(&p, 1, PIN_BUZZER, PIN_RANK_BUILTIN, TAG_SYSTEM, "Buzzer"))
          reportBuiltinPinCollision(F("BUZZER_PIN"), BPK_BUZZER); }
    #endif
#endif

#if INCLUDE_CAN_HYBRID || !PROFILE_HAS_NATIVE_CAN
    #if defined(CAN_CS_0) && (CAN_CS_0 != 0xFF)
    { const uint8_t p = CAN_CS_0;
      if (!claimPins(&p, 1, PIN_CS, PIN_RANK_BUILTIN, TAG_SYSTEM, "CAN CS"))
          reportBuiltinPinCollision(F("CAN_CS_0"), BPK_CAN_CS); }
    #endif
    #if defined(CAN_INT_0) && (CAN_INT_0 != 0xFF)
    { const uint8_t p = CAN_INT_0;
      if (!claimPins(&p, 1, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, "CAN INT"))
          reportBuiltinPinCollision(F("CAN_INT_0"), BPK_CAN_INT); }
    #endif
#endif

#if SUPPORTS_SD
    #ifndef BUILTIN_SDCARD
        #define BUILTIN_SDCARD 254
    #endif
    #if SD_PIN != BUILTIN_SDCARD
    { const uint8_t p = SD_PIN;
      if (!claimPins(&p, 1, PIN_CS, PIN_RANK_BUILTIN, TAG_SYSTEM, "SD CS"))
          reportBuiltinPinCollision(F("SD_PIN"), BPK_SD); }
    #endif
#endif

#if INCLUDE_TEST_MODE
    #ifdef TEST_MODE_PIN
    { const uint8_t p = TEST_MODE_PIN;
      if (!claimPins(&p, 1, PIN_BUTTON, PIN_RANK_BUILTIN, TAG_SYSTEM, "Test Mode Trigger"))
          reportBuiltinPinCollision(F("TEST_MODE_PIN"), BPK_TEST_MODE); }
    #endif
#endif

    // ---- #379/#394 Tier 1: a *_WIRED_BUS declaration, but ONLY when it is
    // the bus actually in effect AND the stored configuration needs it ------
    //
    // #377's ladder: an explicit stored `BUS I2C/SPI <n>` selection always
    // wins; a *_WIRED_BUS declaration is a FALLBACK, consulted only when
    // there is no explicit selection; absent both, refuse. A declaration
    // GRANTS PERMISSION, it never VETOES an explicit choice (bus_wiring.h).
    //
    // So this must NOT reserve unconditionally: if systemConfig.buses.active_i2c
    // already names a different bus, that selection is the one in effect —
    // reserving the WIRED bus's pins here too would block copper nothing
    // uses, for a declaration that granted permission nobody is exercising.
    // Only when there is NO explicit selection does the wired declaration
    // become the effective bus, and only then is it a Tier-1 physical fact
    // worth defending before serial (:264 in main.cpp) can claim over it.
    // An explicit selection instead stays a rank-BUS claim made later at
    // initConfiguredBuses(), as before — and is defended against everything
    // in between by storedBusHoldsPin() (#379's other half, pin_owner.cpp).
    //
    // #394: "in effect" alone is not enough — acquireRequiredBuses() only
    // STARTS a bus something votes for (a stored input's sensor requirement,
    // or the display, #358), so a declared-but-in-effect bus that nothing
    // needs never comes up, and reserving its pins here anyway drops any
    // stored input sitting on that copper for a bus that never runs. `busNeeds`
    // is the SAME vote (main.cpp computes it once, before this call, and
    // reuses it at acquireRequiredBuses() — the two must never disagree), so
    // the reservation now requires both: declaration in effect AND needed.
    //
    // Description strings MUST match what initI2CBus()/initSPIBus() claim
    // under (bus_manager.cpp) byte for byte: getI2CBusName()/getSPIBusName()
    // are the single shared source for both call sites, specifically so this
    // reservation and that later claim can never drift apart — a mismatch
    // here would make the bus refuse to start against its own reservation.
#if BUS_WIRED_I2C_ID != 0xFF
    if ((busNeeds & BUS_NEED_I2C) &&
        wiredBusIsInEffect(systemConfig.buses.active_i2c, BUS_WIRED_I2C_ID)) {
        const uint8_t p[2] = { getDefaultI2CSDA(BUS_WIRED_I2C_ID), getDefaultI2CSCL(BUS_WIRED_I2C_ID) };
        if (!claimPins(p, 2, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, getI2CBusName(BUS_WIRED_I2C_ID)))
            reportBuiltinPinCollision(F("I2C_WIRED_BUS"), BPK_I2C_WIRED);
    }
#endif
#if BUS_WIRED_SPI_ID != 0xFF
    if ((busNeeds & BUS_NEED_SPI) &&
        wiredBusIsInEffect(systemConfig.buses.active_spi, BUS_WIRED_SPI_ID)) {
        const uint8_t p[3] = { getDefaultSPIMOSI(BUS_WIRED_SPI_ID), getDefaultSPIMISO(BUS_WIRED_SPI_ID),
                               getDefaultSPISCK(BUS_WIRED_SPI_ID) };
        if (!claimPins(p, 3, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, getSPIBusName(BUS_WIRED_SPI_ID)))
            reportBuiltinPinCollision(F("SPI_WIRED_BUS"), BPK_SPI_WIRED);
    }
#endif
    // CAN_WIRED_BUS is deliberately NOT reserved here: unlike I2C/SPI, CAN has
    // no resolveWiredBusId() fallback bring-up path (acquireI2C()/acquireSPI()
    // have one, there is no acquireCAN() equivalent) — the declaration is
    // informational only (BUS CAN STATUS) until something explicitly selects
    // input_can_bus/output_can_bus. Reserving it here would claim pins for a
    // permission nothing has a mechanism to exercise.
}

/**
 * Load system configuration from EEPROM
 * @return true if successful (valid config found)
 */
bool loadSystemConfig() {
    SystemConfig temp;
    EEPROM.get(SYSTEM_CONFIG_ADDRESS, temp);

    // Validate magic number
    if (temp.magic != SYSTEM_CONFIG_MAGIC) {
        bootDiag.flags |= bootDiag.DIAG_BAD_MAGIC;
        return false;
    }

    // Check version
    if (temp.version != SYSTEM_CONFIG_VERSION) {
        bootDiag.flags |= bootDiag.DIAG_VERSION_MISMATCH;
        bootDiag.loadedVersion = temp.version;
        return false;
    }

    // Verify checksum
    uint8_t calculatedChecksum = calculateChecksum(&temp);
    if (temp.checksum != calculatedChecksum) {
        bootDiag.flags |= bootDiag.DIAG_CHECKSUM_FAILED;
        bootDiag.storedChecksum = temp.checksum;
        bootDiag.calcChecksum   = calculatedChecksum;
        return false;
    }

    // Valid config - copy to global
    memcpy(&systemConfig, &temp, sizeof(SystemConfig));
    return true;
}
