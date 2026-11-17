/*
 * system_config.cpp - System Configuration Implementation
 */

#include "system_config.h"
#include "../outputs/table/output_table.h"   // outputsSeedDefaults (#612)
#include "../core/watchdog.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif
#include "../config.h"
#include "../sensors/units_registry.h"
#include "../core/message_api.h"     // msg, msg.filter; TransportID
#include "../hal/bus_defaults.h"
#include "../buses/pinctrl/bus_wiring.h"
#include "../buses/bus_manager.h"   // getI2CBusName()/getSPIBusName() — Tier-1 wired-bus reservation (#379)
#include "../buses/bus_needs.h"     // BUS_NEED_I2C/BUS_NEED_SPI — Tier-1 gated on the stored vote (#394)
#include "../core/log_tags.h"
#include "../buses/pinctrl/pin_registry.h"
#include "../buses/pinctrl/pin_owner.h"
#include "../outputs/formats/output_format.h"  // SdLogFormat
#include "../sensors/can/can_poll.h"   // canPollMsValid, CAN_POLL_MS_DEFAULT (#633)
#include "eeprom_store.h"
#include "../core/pin_level.h"          // pinOutputAt(): CS deselect, level first (§12.5)
#include "../transports/sd_manager.h"   // SD_CS_OWNER: initSD() looks the claim up by this name (#594)

// Global system config instance
SystemConfig systemConfig;

// Boot diagnostics buffer — loadSystemConfig() runs before the console has a
// transport, so msg.* calls would be silently dropped. Store the events here and flush
// them via flushSystemConfigBootDiagnostics() once the transports are begun (transportsBegin()).
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
    // runs in the same no-transport-yet window as loadSystemConfig(), so its
    // context (a build-time duplicate, or since #594 a configured input on the
    // pin) is lost the same way. One bit per call
    // site below is enough (there are exactly 9, all fixed at compile time) —
    // no array/capacity needed the way pin_owner.cpp's general buffer does.
    uint16_t pinCollisionFlags = 0;
    // Same bits: the colliding pin was held by a configured input, not a
    // duplicate in wiring.ini/board.h, so the fix is CLEAR, not a rebuild (#594).
    uint16_t pinCollisionInputFlags = 0;
} bootDiag;

// BuiltinPinKind lives in system_config.h: builtinPinHeld() takes it.

// Which builtin claims registerSystemPins() won (one bit per BuiltinPinKind).
// Set once at boot and never cleared: a builtin claim is never released or
// re-made, so this is the claim's state for the whole run (§12.5).
static uint16_t builtinHeldFlags = 0;

bool builtinPinHeld(BuiltinPinKind kind) {
    return (builtinHeldFlags & (uint16_t)(1u << kind)) != 0;
}

// Forward declaration: the definition (below, beside registerSystemPins())
// always prints immediately — flushSystemConfigBootDiagnostics() (here) is
// the deferred half of the same message, so it needs the symbol early.
static void reportBuiltinPinCollisionNow(const __FlashStringHelper* what, bool byInput);

FLASHMEM static void replayBuiltinPinCollision(uint8_t bit, const __FlashStringHelper* what) {
    const uint16_t m = (uint16_t)(1u << bit);
    if (bootDiag.pinCollisionInputFlags & m) reportBuiltinPinCollisionNow(what, true);
    if (bootDiag.pinCollisionFlags & m)      reportBuiltinPinCollisionNow(what, false);
}

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
#ifdef BOARD_RESERVED_PINS
    replayBuiltinPinCollision(BPK_RESERVED, F("BOARD_RESERVED_PINS"));
#endif
#if INCLUDE_USER_BUTTON
    replayBuiltinPinCollision(BPK_USER_BUTTON, F("USER_BUTTON_PIN"));
#endif
#if INCLUDE_ALARMS && defined(BUZZER_PIN)
    replayBuiltinPinCollision(BPK_BUZZER, F("BUZZER_PIN"));
#endif
#if INCLUDE_CAN && !PROFILE_HAS_NATIVE_CAN && defined(CAN_CS_0) && (CAN_CS_0 != 0xFF)
    replayBuiltinPinCollision(BPK_CAN_CS, F("CAN_CS_0"));
#endif
#if INCLUDE_CAN && !PROFILE_HAS_NATIVE_CAN && defined(CAN_INT_0) && (CAN_INT_0 != 0xFF)
    replayBuiltinPinCollision(BPK_CAN_INT, F("CAN_INT_0"));
#endif
#if SUPPORTS_SD
    replayBuiltinPinCollision(BPK_SD, F("SD_PIN"));
#endif
#if INCLUDE_TEST_MODE && defined(TEST_MODE_PIN)
    replayBuiltinPinCollision(BPK_TEST_MODE, F("TEST_MODE_PIN"));
#endif
#if BUS_WIRED_I2C_ID != 0xFF
    replayBuiltinPinCollision(BPK_I2C_WIRED, F("I2C_WIRED_BUS"));
#endif
#if BUS_WIRED_SPI_ID != 0xFF
    replayBuiltinPinCollision(BPK_SPI_WIRED, F("SPI_WIRED_BUS"));
#endif
    bootDiag.pinCollisionFlags = 0;
    bootDiag.pinCollisionInputFlags = 0;
}

float seaLevelPressureHpa() {
    return seaLevelPressureValid(systemConfig.seaLevelPressure)
        ? systemConfig.seaLevelPressure : (float)SEA_LEVEL_PRESSURE_HPA;
}

void printSystemStatus() {
    msg.control.println(F("=== System Configuration ==="));

    msg.control.print(F("Sea Level Pressure: "));
    msg.control.print(systemConfig.seaLevelPressure);
    msg.control.println(F(" hPa"));

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
    // Salted with the layout: an image of an earlier layout under the same
    // SYSTEM_CONFIG_VERSION fails here and loads defaults (#612 phase 2b-1)
    uint8_t checksum = SYSTEM_CONFIG_LAYOUT;
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
 * Reset system configuration to defaults, except the sections in `keep`
 * (ResetKeep in system_config.h): SYSTEM RESET leaves the link it may have
 * arrived over alone (#549). Skipping sections here rather than snapshotting
 * and restoring them keeps the structs off the CLI handler's stack, which
 * matters on mega2560 (~630 B peak).
 */
void resetSystemConfig(uint8_t keep) {
    systemConfig.magic = SYSTEM_CONFIG_MAGIC;
    systemConfig.version = SYSTEM_CONFIG_VERSION;

    // The outputs you add are the config-store arena's (output_table.h):
    // a reset seeds their defaults there. The alarm is built in, on when built.
#if INCLUDE_ALARMS
    systemConfig.alarmEnabled = 1;   // ON - safety critical
#else
    systemConfig.alarmEnabled = 0;
#endif
    outputsSeedDefaults();

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
    // Hardware setup (#551): SYSTEM RESET CONFIG keeps the display.
    if (!(keep & RESET_KEEP_HARDWARE)) {
        // Same for the OLED: INCLUDE_OLED doesn't mean one is fitted.
        systemConfig.displayEnabled = 1;   // on/off toggle; type is what gates bring-up
        systemConfig.displayType = DISPLAY_NONE;

    systemConfig.lcdI2CAddress = LCD_I2C_ADDRESS_DEFAULT;
    }  // RESET_KEEP_HARDWARE (display)

    // Resolve unit names to indices at boot time
    systemConfig.defaultTempUnits = getUnitsIndexByName(DEFAULT_TEMPERATURE_UNITS);
    systemConfig.defaultPressUnits = getUnitsIndexByName(DEFAULT_PRESSURE_UNITS);
    systemConfig.defaultElevUnits = getUnitsIndexByName(DEFAULT_ELEVATION_UNITS);
    systemConfig.defaultSpeedUnits = getUnitsIndexByName(DEFAULT_SPEED_UNITS);

    systemConfig.alarmSilence = 0;   // buzzer only, SILENCE_DURATION (#433)

    // Physical constants
    systemConfig.seaLevelPressure = SEA_LEVEL_PRESSURE_HPA;

    // The console and the log (§14.7). The console on USB and Serial1, the
    // log on USB. Part of the link: the console reads commands only from its
    // transports, so a bridge on Serial2+ carrying it would go deaf if this
    // were reset (#549).
    if (!(keep & RESET_KEEP_LINK)) {
    systemConfig.console.primary = TRANSPORT_USB_SERIAL;
    systemConfig.console.secondary = TRANSPORT_SERIAL1;
    systemConfig.log.primary = TRANSPORT_USB_SERIAL;
    systemConfig.log.secondary = TRANSPORT_NONE;
    }  // RESET_KEEP_LINK (console, log)

    // NB (v13): the control-engine Rule/Action tables are no longer part of
    // SystemConfig — they are reset via rulesResetTables() and persisted by the
    // front config-store arena (input_manager.cpp), independent of this block.

    // Bus Configuration defaults (simplified "pick one" model). Hardware
    // setup (#551): SYSTEM RESET CONFIG keeps the whole block.
    if (!(keep & RESET_KEEP_HARDWARE)) {
    systemConfig.buses.active_i2c = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.i2c_clock = DEFAULT_I2C_CLOCK;
    systemConfig.buses.active_spi = 0xFF;  // NONE — not initialized until explicitly selected
    systemConfig.buses.spi_clock = DEFAULT_SPI_CLOCK;

    // CAN: one set of settings per bus (#612 phase 2a). The wired bus
    // (CAN_WIRED_BUS, wiring.ini) is on at 500k, with a (disabled) OBD output
    // on it in the outputs' defaults; nothing is assumed about an unwired bus
    // (#377). A bus that is on starts only when something attaches, so this
    // alone never ACKs on a car's bus.
    for (uint8_t b = 0; b < CAN_BUS_SLOTS; b++) {
        systemConfig.buses.can[b].mode = CAN_BUS_OFF;
        systemConfig.buses.can[b].baudIndex = CAN_BAUD_500K;
    }
    systemConfig.buses.canInputBus = 0xFF;
    systemConfig.buses.canPollMs = CAN_POLL_MS_DEFAULT;
#if BUS_WIRED_CAN_ID != 0xFF
    systemConfig.buses.can[BUS_WIRED_CAN_ID].mode = CAN_BUS_NORMAL;
#endif
    }  // RESET_KEEP_HARDWARE (buses)

    // Serial Port Configuration defaults
    // USB Serial is always available; Serial1 enabled by default, others disabled
    if (!(keep & RESET_KEEP_LINK)) {
    systemConfig.serial.enabled_mask = 0x01;  // Bit 0 = Serial1 enabled
    for (int i = 0; i < 8; i++) {
        systemConfig.serial.baudrate_index[i] = BAUD_115200;  // Default 115200 baud
    }
    systemConfig.serial.framed_enabled_mask = 0;  // transparent mode default
    for (size_t i = 0; i < sizeof(systemConfig.serial.reserved); i++) {
        systemConfig.serial.reserved[i] = 0;
    }
    }  // RESET_KEEP_LINK (serial)

    // Log Filter Configuration defaults
    // Default to INFO level (show ERROR, WARN, INFO but not DEBUG)
    systemConfig.logFilter.console_level = 3;  // LOG_LEVEL_INFO
    systemConfig.logFilter.debug_level = 3;    // LOG_LEVEL_INFO
    systemConfig.logFilter.enabledTags = 0xFFFFFFFF;  // All tags enabled

    // Calculate checksum
    systemConfig.checksum = calculateChecksum(&systemConfig);
}

/**
 * Save system configuration to EEPROM
 * @return true if successful
 */
FLASHMEM bool saveSystemConfig(bool verbose) {
    WatchdogScope wd(EEPROM_WDT_WINDOW_MS, "EEPROM");   // see saveInputConfig()
    EepromBatch batch;   // flash-backed stores write here (eeprom_store.h)

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

// ---------------------------------------------------------------------------
// The log filter (#561). The console's and the log's transports moved to
// console/console_routing.cpp's routingApplyConfig()/routingCaptureConfig() in
// #612 phase 2b-2 (as routingApplyConfig/routingCaptureConfig; they were MessageRouter::loadConfig/syncConfig, defined
// here since #418); this file keeps applying the filter, which JSON IMPORT
// calls on its own.
// ---------------------------------------------------------------------------

// Also called by JSON IMPORT (#561): the live filter is msg.filter, and the
// LOG commands copy it back over systemConfig.logFilter (routingCaptureConfig), so an
// import that set only the stored copy would be undone by the next LOG command.
void applyLogFilterConfig() {
    LogFilter& f = msg.filter;
    f.setLevel(PLANE_CONTROL, (LogLevel)systemConfig.logFilter.console_level);
    f.setLevel(PLANE_DEBUG,   (LogLevel)systemConfig.logFilter.debug_level);
    f.setEnabledTags(systemConfig.logFilter.enabledTags);
}


// Gap 5 (pin ownership design audit): every claimPins() call below used to
// discard its return value, so two build-time pins colliding in wiring.ini /
// board.h (e.g. BUZZER_PIN == USER_BUTTON_PIN) was accepted in silence — the
// second claimant simply never existed, with no diagnostic pointing at why.
// claimPins() already prints the generic pin-ownership refusal; this adds the
// one thing that's missing — that the SOURCE was a build-time declaration, so
// the fix is wiring.ini/board.h, not a runtime SET/BUS command.
// __FlashStringHelper*, not const char*: every call site below passes an F()
// literal, and a plain const char* parameter would still cost AVR RAM for the
// literal even though the callee never mutates it — F() only keeps a string
// in flash when both the literal AND everything that touches it agree to.
// byInput (#594): the pin is held by a configured input, which the claimant
// outranks. The build is fine; the stored input is what has to move, so
// blaming wiring.ini sent people to the wrong file.
FLASHMEM static void reportBuiltinPinCollisionNow(const __FlashStringHelper* what, bool byInput) {
    if (byInput) {
        msg.control.print(F("ERROR: "));
        msg.control.print(what);
        msg.control.println(F(" not reserved: a configured input holds a pin it needs — CLEAR that input or move it to another pin"));
        return;
    }
    msg.control.print(F("ERROR: build-time pin collision reserving "));
    msg.control.print(what);
    msg.control.println(F(" — check wiring.ini / board.h for a duplicate pin"));
}

// registerSystemPins() runs before transportsBegin() (main.cpp), same as
// loadSystemConfig() — printing straight to msg.control here is the other
// half of #396's silent-discard bug. pin_owner.h's pinOwnerBootDiagBuffering()
// is the single source of truth for "does the console have no transport yet";
// buffer here too so this annotation and pin_owner's own conflict message
// (buffered separately, pin_owner.cpp) both survive to flush time instead of
// only one of them being fixed.
FLASHMEM static void recordBuiltinPinCollision(const __FlashStringHelper* what, uint8_t bit, bool byInput) {
    if (pinOwnerBootDiagBuffering()) {
        if (byInput) bootDiag.pinCollisionInputFlags |= (uint16_t)(1u << bit);
        else         bootDiag.pinCollisionFlags      |= (uint16_t)(1u << bit);
    } else {
        reportBuiltinPinCollisionNow(what, byInput);
    }
}

// Call right after the refused claimPins(): it reads that claim's verdict.
// A full pin table is not a duplicate pin: claimPins() has already said what
// happened, and "check wiring.ini" would be the wrong advice (#599).
FLASHMEM static void reportBuiltinPinCollision(const __FlashStringHelper* what, uint8_t bit, bool byInput) {
    if (!byInput && lastClaimFailedRegistryFull()) return;
    recordBuiltinPinCollision(what, bit, byInput);
}

// initSD() needs to know why ITS claim failed, and by then the registry and
// the stored bus selections can look different, so keep the verdict (#599).
static bool sdCsRefusedTableFull = false;
bool sdCsClaimRefusedTableFull() { return sdCsRefusedTableFull; }

// A multi-pin claim (I2C/SPI wired bus) can be refused by an input on one pin
// and a registry holder on another, so check every pin and report each cause
// that is present; pinsConflict() itself stops at the first.
FLASHMEM static void reportBuiltinPinsCollision(const __FlashStringHelper* what, uint8_t bit,
                                       const uint8_t* pins, uint8_t count) {
    bool byInput = false, byBuild = false;
    for (uint8_t i = 0; i < count; i++) {
        if (pinHeldByConfiguredInput(pins[i])) byInput = true;
        else if (!isPinAvailable(pins[i])) byBuild = true;
    }
    if (byInput) reportBuiltinPinCollision(what, bit, true);
    if (byBuild || !byInput) reportBuiltinPinCollision(what, bit, false);
}

/**
 * Register system pins in the pin registry
 * This reserves these pins and makes them visible in the registry export
 */
FLASHMEM void registerSystemPins(uint8_t busNeeds) {
#ifdef BOARD_RESERVED_PINS
    // First: pins wired to the board's own module (board.h, #570) are taken
    // before anything wired.ini declares, so a wired pin that lands on one is
    // the claim that fails, and is reported under its own name.
    // One pin at a time: claimPins() takes at most 8, and a pin already held
    // (a stored input from before this reservation existed) must not leave
    // the others unclaimed.
    { static const uint8_t p[] = { BOARD_RESERVED_PINS };
      bool byInput = false, byBuild = false;
      for (uint8_t i = 0; i < sizeof(p); i++) {
          if (!claimPins(&p[i], 1, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, BOARD_RESERVED_NAME)) {
              if (pinHeldByConfiguredInput(p[i])) byInput = true;
              else if (!lastClaimFailedRegistryFull()) byBuild = true;
          }
      }
      if (byInput) recordBuiltinPinCollision(F("BOARD_RESERVED_PINS"), BPK_RESERVED, true);
      if (byBuild) recordBuiltinPinCollision(F("BOARD_RESERVED_PINS"), BPK_RESERVED, false); }
#endif
#if INCLUDE_USER_BUTTON
    { const uint8_t p = USER_BUTTON_PIN;
      if (claimPins(&p, 1, PIN_BUTTON, PIN_RANK_BUILTIN, TAG_SYSTEM, "User Button"))
          builtinHeldFlags |= (uint16_t)(1u << BPK_USER_BUTTON);
      else
          reportBuiltinPinCollision(F("USER_BUTTON_PIN"), BPK_USER_BUTTON, pinHeldByConfiguredInput(p)); }
#endif

#if INCLUDE_ALARMS
    #ifdef BUZZER_PIN
    { const uint8_t p = BUZZER_PIN;
      if (claimPins(&p, 1, PIN_BUZZER, PIN_RANK_BUILTIN, TAG_SYSTEM, "Buzzer"))
          builtinHeldFlags |= (uint16_t)(1u << BPK_BUZZER);
      else
          reportBuiltinPinCollision(F("BUZZER_PIN"), BPK_BUZZER, pinHeldByConfiguredInput(p)); }
    #endif
#endif

#if INCLUDE_CAN && !PROFILE_HAS_NATIVE_CAN
    // An MCP2515 is wiring (wiring.ini CAN_CS_0, #658): claimed only when its
    // driver is in the image, like every other wired peripheral.
    #if defined(CAN_CS_0) && (CAN_CS_0 != 0xFF)
    { const uint8_t p = CAN_CS_0;
      if (claimPins(&p, 1, PIN_CS, PIN_RANK_BUILTIN, TAG_SYSTEM, "CAN CS")) {
          builtinHeldFlags |= (uint16_t)(1u << BPK_CAN_CS);
          // Deselect the MCP2515 now, as SD CS below: an SPI sensor can talk
          // before the CAN bus starts, and a floating CS would let the
          // controller answer. Its driver is built only when the bus starts
          // (can_mcp2515.h), and only once this claim has held.
          pinOutputAt(p, HIGH);
      } else {
          reportBuiltinPinCollision(F("CAN_CS_0"), BPK_CAN_CS, pinHeldByConfiguredInput(p));
      } }
    #endif
    #if defined(CAN_INT_0) && (CAN_INT_0 != 0xFF)
    { const uint8_t p = CAN_INT_0;
      if (!claimPins(&p, 1, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, "CAN INT"))
          reportBuiltinPinCollision(F("CAN_INT_0"), BPK_CAN_INT, pinHeldByConfiguredInput(p)); }
    #endif
#endif

#if SUPPORTS_SD
    #ifndef BUILTIN_SDCARD
        #define BUILTIN_SDCARD 254
    #endif
    #if SD_PIN != BUILTIN_SDCARD
    { const uint8_t p = SD_PIN;
      if (!claimPins(&p, 1, PIN_CS, PIN_RANK_BUILTIN, TAG_SYSTEM, SD_CS_OWNER)) {
          sdCsRefusedTableFull = lastClaimFailedRegistryFull();
          reportBuiltinPinCollision(F("SD_PIN"), BPK_SD, pinHeldByConfiguredInput(p));
      }
      else {
          // Deselect the card now: other devices on the same SPI bus may
          // talk before initSD() runs, and a floating CS would let the
          // card answer them. HIGH reaches the latch before the driver
          // turns on, so CS never passes through LOW (§12.5).
          builtinHeldFlags |= (uint16_t)(1u << BPK_SD);
          pinOutputAt(p, HIGH);
      } }
    #endif
#endif

#if INCLUDE_TEST_MODE
    #ifdef TEST_MODE_PIN
    { const uint8_t p = TEST_MODE_PIN;
      if (claimPins(&p, 1, PIN_BUTTON, PIN_RANK_BUILTIN, TAG_SYSTEM, "Test Mode Trigger"))
          builtinHeldFlags |= (uint16_t)(1u << BPK_TEST_MODE);
      else
          reportBuiltinPinCollision(F("TEST_MODE_PIN"), BPK_TEST_MODE, pinHeldByConfiguredInput(p)); }
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
            reportBuiltinPinsCollision(F("I2C_WIRED_BUS"), BPK_I2C_WIRED, p, 2);
    }
#endif
#if BUS_WIRED_SPI_ID != 0xFF
    if ((busNeeds & BUS_NEED_SPI) &&
        wiredBusIsInEffect(systemConfig.buses.active_spi, BUS_WIRED_SPI_ID)) {
        uint8_t p[4];
        const uint8_t n = getSPIBusPins(BUS_WIRED_SPI_ID, p);   // + SS on AVR (#663)
        if (!claimPins(p, n, PIN_RESERVED, PIN_RANK_BUILTIN, TAG_SYSTEM, getSPIBusName(BUS_WIRED_SPI_ID)))
            reportBuiltinPinsCollision(F("SPI_WIRED_BUS"), BPK_SPI_WIRED, p, n);
    }
#endif
    // CAN_WIRED_BUS is deliberately NOT reserved here: unlike I2C/SPI, CAN has
    // no resolveWiredBusId() fallback bring-up path (acquireI2C()/acquireSPI()
    // have one, there is no acquireCAN() equivalent) — the declaration is
    // the default it seeds (the wired bus on, OBD on it) until a consumer
    // attaches. Reserving it here would claim pins for a permission nothing
    // has a mechanism to exercise.
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
    // An EEPROM written before #590 could hold any 0x03-0x77 LCD address.
    // Correct it here, once, so the driver, DISPLAY STATUS, DIAG HW and JSON
    // export all see the address that is actually used.
    if (!isValidLcdI2CAddress(systemConfig.lcdI2CAddress)) {
        systemConfig.lcdI2CAddress = LCD_I2C_ADDRESS_DEFAULT;
    }
    // Same for the CAN poll rate: a corrupt value would stall or flood the poller
    if (!canPollMsValid(systemConfig.buses.canPollMs)) {
        systemConfig.buses.canPollMs = CAN_POLL_MS_DEFAULT;
    }
    return true;
}
