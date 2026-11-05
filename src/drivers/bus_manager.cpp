/*
 * bus_manager.cpp - Bus Initialization Manager Implementation
 *
 * Simplified "pick one" model - each bus type has one active instance.
 */

#include "bus_manager.h"
#include "../storage/bus_config.h"
#include "../hal/bus_defaults.h"
#include "../pinctrl/bus_wiring.h"
#include "../pinctrl/pin_registry.h"
#include "../pinctrl/pin_owner.h"
#include "../storage/bus_needs.h"
#include "../storage/system_config.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include <string.h>
#include <Wire.h>
#include <SPI.h>

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Active bus instances
static TwoWire* active_i2c = nullptr;
static SPIClass* active_spi = nullptr;

// Active bus IDs (for reference)
static uint8_t active_i2c_id = 0;
// Clock the active I2C bus was brought up at. systemConfig.buses.i2c_clock may
// already hold a pending `BUS I2C CLOCK` change that only applies at reboot.
static uint16_t active_i2c_khz = 0;
static uint8_t active_spi_id = 0;
static uint8_t active_can_id = 0;

// CAN ready flag (actual FlexCAN objects are in output_can.cpp)
static bool can_ready = false;
// Bit n: initCANBus(n) claimed bus n's pins. Input and output only start a
// controller on a claimed bus: on Teensy 4, FlexCAN2's begin() remuxes pins
// 0/1 even when Serial1 holds them (review of #560).
static uint8_t can_claimed_mask = 0;

// Selections whose claim was REFUSED this boot (#564). A refused bus stays down
// until the next reboot, so its stored selection must stop defending its pins:
// otherwise the input that blocked it is then dropped by storedBusHoldsPin(),
// and the pin ends up held by nobody. One id per bus kind is enough, as only
// the stored selection is ever brought up; CAN has two directions, so a mask.
static uint8_t i2c_refused_id = 0xFF;
static uint8_t spi_refused_id = 0xFF;
static uint8_t can_refused_mask = 0;

// ============================================================================
// PIN CLAIMING
// ============================================================================

// A bus rests on "bus up implies its pins are reserved" — that is what stops an
// input being assigned to a pin a bus is clocking (#351). The decision itself
// lives in pin_owner.cpp (claimPins), shared with every other pin claimant and
// with the CLI so the boot path and `BUS I2C <n>` cannot drift apart, and so
// every bus — not just I2C, and not just at runtime — consults the configured
// inputs as well as the registry (#358).
//
// One shared format string: msg.debug.* format strings are RAM-resident on AVR,
// so three per-bus variants would cost real bytes on the 8 KB mega.
static const char BUS_STARTED[] = "%s up";

// ============================================================================
// MAIN INITIALIZATION
// ============================================================================

void initConfiguredBuses() {

    // No FALLBACK TO BUS 0 on any of these three (#377).
    //
    // Each used to substitute bus 0 when the selected bus failed to start. That
    // is the same inference this branch deletes, wearing a different hat and in
    // its worst form: the user named bus 2, the firmware quietly took bus 0
    // instead, and on Teensy 4.x bus 0 is 18/19 — so a board told to use Wire2
    // could end up claiming SDA/SCL out from under configured inputs on A4/A5,
    // for a bus nobody selected. A named bus that cannot start is a fault to
    // report, never a licence to pick a different one.

    // Initialize the active I2C bus (only if explicitly selected)
    if (systemConfig.buses.active_i2c != 0xFF) {
        if (!initI2CBus(systemConfig.buses.active_i2c, systemConfig.buses.i2c_clock)) {
            reportI2CBusUnavailable(systemConfig.buses.active_i2c);
        }
    }

    // Initialize the active SPI bus (only if explicitly selected)
    if (systemConfig.buses.active_spi != 0xFF) {
        if (!initSPIBus(systemConfig.buses.active_spi, systemConfig.buses.spi_clock)) {
            reportSPIBusUnavailable(systemConfig.buses.active_spi);
        }
    }

    // Initialize CAN output bus if enabled
    if (systemConfig.buses.can_output_enabled && systemConfig.buses.output_can_bus != 0xFF) {
        if (!initCANBus(systemConfig.buses.output_can_bus, systemConfig.buses.can_output_baudrate)) {
            msg.debug.warn(TAG_CAN, "CAN output: bus %d did not start; no substitute chosen",
                           systemConfig.buses.output_can_bus);
        }
    }

    // Initialize CAN input bus if enabled
    // Supports two modes:
    // 1. SHARED BUS: input_can_bus == output_can_bus (same physical bus)
    //    - Both input and output use same CAN peripheral (e.g., both on CAN1)
    //    - Common on single-bus platforms (AVR with MCP2515, ESP32 with TWAI)
    //    - HAL handles concurrent access safely (non-blocking reads, queued writes)
    // 2. DUAL BUS: input_can_bus != output_can_bus (separate physical buses)
    //    - Input on CAN1, output on CAN2 (Teensy 3.6/4.x only)
    //    - Allows isolation of sensor import from OBD-II output traffic
    if (systemConfig.buses.can_input_mode != CAN_INPUT_OFF && systemConfig.buses.input_can_bus != 0xFF) {
        // Only initialize if different bus than output (avoid double-init in shared mode)
        if (systemConfig.buses.input_can_bus != systemConfig.buses.output_can_bus) {
            if (!initCANBus(systemConfig.buses.input_can_bus, systemConfig.buses.can_input_baudrate)) {
                msg.debug.warn(TAG_CAN, "CAN input: Failed to initialize bus %d", systemConfig.buses.input_can_bus);
            }
        } else {
            // Shared bus mode - already initialized by output, just log status
            msg.debug.info(TAG_CAN, "CAN input sharing bus with output (bus %d)", systemConfig.buses.input_can_bus);
        }
    }
}

// ============================================================================
// I2C BUS INITIALIZATION
// ============================================================================

bool initI2CBus(uint8_t bus_id, uint16_t clock_khz) {
    if (bus_id >= NUM_I2C_BUSES) {
        msg.debug.error(TAG_I2C, "I2C bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus
    const uint8_t i2c_pins[2] = { getDefaultI2CSDA(bus_id), getDefaultI2CSCL(bus_id) };
    // getI2CBusName() is THE canonical owner description for this bus — also
    // used by registerSystemPins() (system_config.cpp, #379 Tier-1 wired-bus
    // reservation) and storedBusHoldsPin() (#379 stored-selection defense).
    // One source, not three copies of "Wire"/"Wire1"/"Wire2" that could drift:
    // a mismatch here would make this claim refuse against its OWN earlier
    // reservation instead of self-skipping through it.
    const char* i2c_desc = getI2CBusName(bus_id);

    // Claim SDA/SCL first: if the copper is already someone else's, the bus must
    // not come up at all. Refusing here rather than after Wire.begin() also means
    // there is nothing to un-begin — TwoWire has no portable end().
    if (!claimPins(i2c_pins, 2, PIN_RESERVED, PIN_RANK_BUS, TAG_I2C, i2c_desc)) {
        i2c_refused_id = bus_id;
        return false;
    }
    if (i2c_refused_id == bus_id) i2c_refused_id = 0xFF;

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Fixed hardware pins, multiple Wire instances
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
        case 2:
            Wire2.begin();
            Wire2.setClock(clock_khz * 1000UL);
            active_i2c = &Wire2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__) || defined(__MK20DX256__)
    // Teensy 3.x: Multiple Wire instances
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
#endif
#if NUM_I2C_BUSES >= 3
        case 2:
            Wire2.begin();
            Wire2.setClock(clock_khz * 1000UL);
            active_i2c = &Wire2;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Wire.begin() uses default pins
    switch (bus_id) {
        case 0:
            Wire.begin();
            Wire.setClock(clock_khz * 1000UL);
            active_i2c = &Wire;
            success = true;
            break;
#if NUM_I2C_BUSES >= 2
        case 1:
            Wire1.begin();
            Wire1.setClock(clock_khz * 1000UL);
            active_i2c = &Wire1;
            success = true;
            break;
#endif
    }

#else
    // Generic Arduino (Uno, Mega, etc.): Single Wire bus
    if (bus_id == 0) {
        Wire.begin();
        Wire.setClock(clock_khz * 1000UL);
        active_i2c = &Wire;
        success = true;
    }
#endif

    if (success) {
        active_i2c_id = bus_id;
        active_i2c_khz = clock_khz;
        msg.debug.info(TAG_I2C, BUS_STARTED, i2c_desc);
    } else {
        // Unreachable bus id for this platform: hand the pins back rather than
        // leaving them reserved to a bus that never started.
        releasePins(i2c_pins, 2, i2c_desc);
    }

    return success;
}

// ============================================================================
// SPI BUS INITIALIZATION
// ============================================================================

bool initSPIBus(uint8_t bus_id, uint32_t clock_hz) {
    if (bus_id >= NUM_SPI_BUSES) {
        msg.debug.error(TAG_SPI, "SPI bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus
    const uint8_t spi_pins[3] = { getDefaultSPIMOSI(bus_id),
                                  getDefaultSPIMISO(bus_id),
                                  getDefaultSPISCK(bus_id) };
    // See initI2CBus()'s comment: one canonical source, shared with
    // registerSystemPins() and storedBusHoldsPin() (#379).
    const char* spi_desc = getSPIBusName(bus_id);

    // Same contract as I2C: claim the bus pins before SPI.begin() drives them.
    if (!claimPins(spi_pins, 3, PIN_RESERVED, PIN_RANK_BUS, TAG_SPI, spi_desc)) {
        spi_refused_id = bus_id;
        return false;
    }
    if (spi_refused_id == bus_id) spi_refused_id = 0xFF;

    // Platform-specific initialization
    bool success = false;

#if defined(__IMXRT1062__)
    // Teensy 4.x: Multiple SPI instances with fixed pins
    switch (bus_id) {
        case 0:
            SPI.begin();
            active_spi = &SPI;
            success = true;
            break;
        case 1:
            SPI1.begin();
            active_spi = &SPI1;
            success = true;
            break;
        case 2:
            SPI2.begin();
            active_spi = &SPI2;
            success = true;
            break;
    }

#elif defined(__MK66FX1M0__) || defined(__MK64FX512__)
    // Teensy 3.6/3.5: Multiple SPI instances
    switch (bus_id) {
        case 0:
            SPI.begin();
            active_spi = &SPI;
            success = true;
            break;
#if NUM_SPI_BUSES >= 2
        case 1:
            SPI1.begin();
            active_spi = &SPI1;
            success = true;
            break;
#endif
    }

#elif defined(ESP32)
    // ESP32: Single SPI bus with default pins
    if (bus_id == 0) {
        SPI.begin();
        active_spi = &SPI;
        success = true;
    }

#else
    // Generic Arduino: Single SPI bus
    if (bus_id == 0) {
#if defined(ARDUINO_ARCH_STM32)
        // STM32 core 3.0.0: begin() skips spi_init() when its settings equal the
        // cached default (4 MHz, MODE0), so the peripheral is never clocked and
        // the first transfer() spins forever -- a MAX31855 read hung until the
        // watchdog fired (#100). Configuring another speed first makes begin()'s
        // settings a change. Upstream: stm32duino #3052, fixed by PR #3049 after
        // 3.0.0; scripts/stm32_core_workarounds.py says when this can go.
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
#endif
        SPI.begin();
        active_spi = &SPI;
        success = true;
    }
#endif

    if (success) {
        active_spi_id = bus_id;
        msg.debug.info(TAG_SPI, BUS_STARTED, spi_desc);
    } else {
        releasePins(spi_pins, 3, spi_desc);
    }

    return success;
}

// ============================================================================
// CAN BUS INITIALIZATION
// ============================================================================

bool initCANBus(uint8_t bus_id, uint32_t baudrate) {
#if NUM_CAN_BUSES == 0
    (void)bus_id;
    (void)baudrate;
    msg.debug.warn(TAG_CAN, "CAN not available on this platform");
    return false;
#else
    if (bus_id >= NUM_CAN_BUSES) {
        msg.debug.error(TAG_CAN, "CAN bus %d not available on this platform", bus_id);
        return false;
    }

    // Get default pins for this bus. 0xFF entries (SPI-attached controllers
    // have no fixed TX/RX) are skipped by the claim.
    const uint8_t can_pins[2] = { getDefaultCANTX(bus_id), getDefaultCANRX(bus_id) };
    // See initI2CBus()'s comment: one canonical source, shared with
    // storedBusHoldsPin() (#379).
    const char* can_desc = getCANBusName(bus_id);

    // Claim before declaring the bus ready: can_ready is what the rest of the
    // firmware reads as "this bus is mine", so it must not be set over pins the
    // registry attributes to another function.
    if (!claimPins(can_pins, 2, PIN_RESERVED, PIN_RANK_BUS, TAG_CAN, can_desc)) {
        if (bus_id < 8) can_refused_mask |= (uint8_t)(1u << bus_id);
        return false;
    }
    if (bus_id < 8) can_refused_mask &= (uint8_t)~(1u << bus_id);

    // CAN buses are started by CAN input and output through the HAL (hal_can.h)
    // Here we just mark the bus as ready
    active_can_id = bus_id;
    can_ready = true;
    if (bus_id < 8) can_claimed_mask |= (uint8_t)(1u << bus_id);

    msg.debug.info(TAG_CAN, BUS_STARTED, can_desc);

    return true;
#endif
}

bool canBusClaimed(uint8_t bus_id) {
    return bus_id < 8 && (can_claimed_mask & (1u << bus_id));
}

// ============================================================================
// ACTIVE BUS ACCESS
// ============================================================================

TwoWire* getActiveI2C() {
    return active_i2c;
}

// De-duplicate reportNo{I2C,SPI}Bus() WITHIN one episode, per bus type. Boot is
// one episode: acquireRequiredBuses(), main.cpp's diagnosis and initLCD() can
// each reach the same refusal, and a board with several stored I2C sensors and
// no bus must not print the same guidance once per sensor.
//
// The latch must NOT survive the episode, though. It is re-armed at the end of
// setup() (rearmBusDiagnostics), because the CLI is the OTHER place this
// guidance has to appear: configuring an I2C input at runtime with no bus
// available must say what to run, and a boot that already printed once would
// otherwise silence that answer for the rest of the session — exactly when the
// user is acting on it.
// One latch per MESSAGE, not per bus: "I refused to guess" and "the bus you
// named could not start" are different answers, and a shared latch would let
// whichever fired first suppress the other — making them indistinguishable to a
// caller and to the tests. They are mutually exclusive per acquire*() call
// anyway, so two latches cost nothing in practice.
static bool i2c_no_bus_reported = false;
static bool spi_no_bus_reported = false;
static bool i2c_unavailable_reported = false;
static bool spi_unavailable_reported = false;

void rearmBusDiagnostics() {
    i2c_no_bus_reported = false;
    spi_no_bus_reported = false;
    i2c_unavailable_reported = false;
    spi_unavailable_reported = false;
}

// ONE print call site for all four notes, deliberately.
//
// Every additional msg.control.print(F(...)) call site inlines a full copy of
// MessageStream's Flash-string path (primary/secondary transport lookups), and
// that overload is not cloned/shared by the linker the way print(const char*)
// is. Four sites cost ~380 B of RAM1 code on Teensy 4.x — enough to push this
// translation unit across an ITCM page boundary, where the linker answers with
// 32 KB of padding and the build fails outright ("free for local variables"
// goes negative). Funnelling them through one noinline body keeps the cost flat
// no matter how many notes get added later. Not a micro-optimization: teensy41
// RAM1 padding is measured in tens of bytes, and this is what keeps it linking.
//
// The text stays in F() rather than becoming a plain literal because mega2560
// has ~140 B of RAM free; on AVR a non-F() string is RAM-resident.
__attribute__((noinline))
static bool emitBusNote(bool& latch, const __FlashStringHelper* text) {
    if (latch) return false;
    latch = true;
    msg.control.print(text);
    return true;
}

bool reportNoI2CBus() {
    return emitBusNote(i2c_no_bus_reported,
        F("\r\nNote: no I2C bus declared or selected. Run: BUS I2C <n>\r\n"
          "  (SAVE + reboot), or add -D I2C_WIRED_BUS=<n> to wiring.ini\r\n"
          "  if I2C is permanently wired on this unit.\r\n"));
}

bool reportNoSPIBus() {
    return emitBusNote(spi_no_bus_reported,
        F("\r\nNote: no SPI bus declared or selected. Run: BUS SPI <n>\r\n"
          "  (SAVE + reboot), or add -D SPI_WIRED_BUS=<n> to wiring.ini\r\n"
          "  if SPI is permanently wired on this unit.\r\n"));
}

// The OTHER way acquire*() can hand back nullptr. Keeping it distinct from
// reportNo{I2C,SPI}Bus() is the point: "I refused to guess" and "you told me
// which bus and it could not start" are opposite problems with opposite fixes,
// and saying the first when the second happened is the exact misdirection #377
// exists to remove — it would tell a user their declaration does not exist.
// The conflict itself has already been named by pinsConflict() (which pin,
// which owner); this says what it means and what to do about it.
bool reportI2CBusUnavailable(uint8_t bus_id) {
    (void)bus_id;
    return emitBusNote(i2c_unavailable_reported,
        F("\r\nNote: the selected/declared I2C bus could not start (see above).\r\n"
          "  Free the pin it needs, or choose another bus with BUS I2C <n>.\r\n"));
}

bool reportSPIBusUnavailable(uint8_t bus_id) {
    (void)bus_id;
    return emitBusNote(spi_unavailable_reported,
        F("\r\nNote: the selected/declared SPI bus could not start (see above).\r\n"
          "  Free the pin it needs, or choose another bus with BUS SPI <n>.\r\n"));
}

TwoWire* acquireI2C() {
    // Already up — initI2CBus() reserved SDA/SCL when it brought the bus up.
    if (active_i2c) return active_i2c;

    // A bus may be brought up ONLY if the stored CONFIG explicitly names it (a
    // user statement) or WIRING declares it (a build-time user statement, see
    // bus_wiring.h). Anything else — a compiled-in default standing in for a
    // choice the user never made — is INFERENCE, and inference is the bug
    // (#377): DEFAULT_I2C_BUS is what silently claimed Wire (18/19 on Teensy
    // 4.x) out from under configured inputs on A4/A5. There is deliberately no
    // fallback to it here.
    uint8_t bus_id = resolveWiredBusId(systemConfig.buses.active_i2c, BUS_WIRED_I2C_ID);
    if (bus_id == 0xFF) {
        reportNoI2CBus();
        return nullptr;
    }

    if (!initI2CBus(bus_id, systemConfig.buses.i2c_clock)) {
        msg.debug.error(TAG_I2C, "I2C acquire failed");
        reportI2CBusUnavailable(bus_id);
        return nullptr;
    }
    return active_i2c;
}

void clearI2CFault() {
#if defined(ARDUINO_ARCH_STM32)
    if (!active_i2c) return;
    active_i2c->begin();
    active_i2c->setClock((uint32_t)active_i2c_khz * 1000UL);
#endif
}

SPIClass* getActiveSPI() {
    return active_spi;
}

SPIClass* acquireSPI() {
    if (active_spi) return active_spi;

    // Same rule as acquireI2C() — see its comment.
    uint8_t bus_id = resolveWiredBusId(systemConfig.buses.active_spi, BUS_WIRED_SPI_ID);
    if (bus_id == 0xFF) {
        reportNoSPIBus();
        return nullptr;
    }

    if (!initSPIBus(bus_id, systemConfig.buses.spi_clock)) {
        msg.debug.error(TAG_SPI, "SPI acquire failed");
        reportSPIBusUnavailable(bus_id);
        return nullptr;
    }
    return active_spi;
}

void acquireRequiredBuses(uint8_t needs) {
    // A refused bus is a degraded board, never a dead one: acquire*() returns
    // nullptr, the clients that need it report and no-op (display_grid.cpp,
    // bme280.cpp, max6675.cpp), and boot continues to the CLI where the user can
    // fix the conflict. Nothing here may abort or reset.
    if (needs & BUS_NEED_I2C) acquireI2C();
    if (needs & BUS_NEED_SPI) acquireSPI();
}

uint8_t getActiveI2CId() {
    return active_i2c_id;
}

uint8_t getActiveSPIId() {
    return active_spi_id;
}

uint8_t getActiveCANId() {
    return active_can_id;
}

// ============================================================================
// BUS NAME HELPERS
// ============================================================================

const char* getI2CBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "Wire";
        case 1: return "Wire1";
        case 2: return "Wire2";
        default: return "I2C?";
    }
}

const char* getSPIBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "SPI";
        case 1: return "SPI1";
        case 2: return "SPI2";
        default: return "SPI?";
    }
}

const char* getCANBusName(uint8_t bus_id) {
    switch (bus_id) {
        case 0: return "CAN1";
        case 1: return "CAN2";
        case 2: return "CAN3";
        default: return "CAN?";
    }
}

// ============================================================================
// STORED-BUS-SELECTION PROBE (#379) — pinsConflict()'s third source
// ============================================================================

bool storedBusHoldsPin(uint8_t pin, const char* excludeOwner, const char** descOut) {
    if (pin == 0xFF) return false;

    // A selection refused this boot is not pending: it will not start until a
    // reboot, so it defends nothing (#564). Skipped per bus below.
    if (systemConfig.buses.active_i2c != 0xFF && systemConfig.buses.active_i2c < NUM_I2C_BUSES &&
        systemConfig.buses.active_i2c != i2c_refused_id) {
        const uint8_t b = systemConfig.buses.active_i2c;
        if (pin == getDefaultI2CSDA(b) || pin == getDefaultI2CSCL(b)) {
            const char* d = getI2CBusName(b);
            if (!excludeOwner || strcmp(d, excludeOwner) != 0) {
                if (descOut) *descOut = d;
                return true;
            }
        }
    }

    if (systemConfig.buses.active_spi != 0xFF && systemConfig.buses.active_spi < NUM_SPI_BUSES &&
        systemConfig.buses.active_spi != spi_refused_id) {
        const uint8_t b = systemConfig.buses.active_spi;
        if (pin == getDefaultSPIMOSI(b) || pin == getDefaultSPIMISO(b) || pin == getDefaultSPISCK(b)) {
            const char* d = getSPIBusName(b);
            if (!excludeOwner || strcmp(d, excludeOwner) != 0) {
                if (descOut) *descOut = d;
                return true;
            }
        }
    }

#if NUM_CAN_BUSES > 0
    // CAN has two independent stored selections (input/output can legitimately
    // be different buses) — check both, each only when its direction is
    // actually enabled, mirroring initConfiguredBuses()'s own gating.
    if (systemConfig.buses.can_output_enabled &&
        systemConfig.buses.output_can_bus != 0xFF &&
        systemConfig.buses.output_can_bus < NUM_CAN_BUSES &&
        !(can_refused_mask & (1u << systemConfig.buses.output_can_bus))) {
        const uint8_t b = systemConfig.buses.output_can_bus;
        if (pin == getDefaultCANTX(b) || pin == getDefaultCANRX(b)) {
            const char* d = getCANBusName(b);
            if (!excludeOwner || strcmp(d, excludeOwner) != 0) {
                if (descOut) *descOut = d;
                return true;
            }
        }
    }
    if (systemConfig.buses.can_input_mode != CAN_INPUT_OFF &&
        systemConfig.buses.input_can_bus != 0xFF &&
        systemConfig.buses.input_can_bus < NUM_CAN_BUSES &&
        !(can_refused_mask & (1u << systemConfig.buses.input_can_bus))) {
        const uint8_t b = systemConfig.buses.input_can_bus;
        if (pin == getDefaultCANTX(b) || pin == getDefaultCANRX(b)) {
            const char* d = getCANBusName(b);
            if (!excludeOwner || strcmp(d, excludeOwner) != 0) {
                if (descOut) *descOut = d;
                return true;
            }
        }
    }
#endif

    return false;
}

void busSelectionAdmitted(uint8_t kind, uint8_t bus_id) {
    if (kind == 0 && i2c_refused_id == bus_id) i2c_refused_id = 0xFF;
    else if (kind == 1 && spi_refused_id == bus_id) spi_refused_id = 0xFF;
    else if (kind == 2 && bus_id < 8) can_refused_mask &= (uint8_t)~(1u << bus_id);
}

//=============================================================================
// BUS Command helpers
//=============================================================================

// Shared by all three BUS status displays (#377 §6): wiring.ini is gitignored
// and per-unit, so the build-time declaration is otherwise invisible in the
// repo — without this line, debugging someone else's board means guessing all
// over again. One body avoids three copies of the same two strings.
// `wiredName` is a bus-name literal (e.g. "Wire1") or "none declared" — always
// a plain const char*, never F(), so every call site here shares print(const
// char*)'s existing clone rather than inlining another Flash-string path.
// noinline: this is small enough that the compiler would otherwise duplicate
// its body at all three call sites (I2C/SPI/CAN status) instead of sharing it.
// "Selected" is not "started". All three status displays used to print the
// STORED SELECTION as though it were the live bus (SPI never looked at
// active_spi at all, CAN never at can_ready), so a bus that failed to start
// still read as Active — the status-vs-peripheral lie this area exists to
// remove. It used to be nearly unreachable because a failed selection silently
// fell back to bus 0, so SOMETHING was always up; now that #377 has deleted
// that fallback, a named bus can legitimately stay down and the lie is routine.
//
// Say the four things a person needs: that nothing is reading, which bus was
// wanted AND WHO ASKED FOR IT, what is in the way, and what to type. "It could
// not start" only reports that a problem exists; the registry already knows the
// rest.
//
// Naming the source matters more than it looks. A bus can be wanted because the
// user typed `BUS I2C 1` or because wiring.ini declares I2C_WIRED_BUS=1 — two
// different fixes (a CLI command vs. editing wiring.ini and reflashing), so
// calling a declared bus "selected" points at the wrong tool.
//
// The usual cause is not a wire at all. Configuration outlives hardware: a
// serial port enabled for a module that has since been unplugged still claims
// its pins every boot, and blocks the bus that IS wired there now. That is why
// the fix line names the saved setting to turn off, not a wire to move.
__attribute__((noinline))
static uint8_t serialPortHoldingPin(uint8_t pin) {
    // Read the port number out of the REGISTRY DESCRIPTION, which
    // initSerialPort() sets from getSerialPortName() — i.e. "Serial1".."Serial8"
    // (serial_manager.cpp:218). Matching the description, not the platform pin
    // map, is what keeps Wire1 (17/16) and Serial4 (16/17) distinguishable: both
    // own those pins on paper, only one owns them now.
    //
    // Parsed rather than compared against getSerialPortName(): that function is
    // `inline` and returns string literals, so instantiating it in this
    // translation unit duplicates all nine into .data on AVR — +56 B of the one
    // resource mega2560 has none of. strncmp_P keeps the prefix in flash too.
    const char* d = getPinDescription(pin);
    if (!d || strncmp_P(d, PSTR("Serial"), 6) != 0) return 0;
    if (d[6] < '1' || d[6] > '8' || d[7] != '\0') return 0;
    uint8_t n = (uint8_t)(d[6] - '0');
    return (n <= NUM_SERIAL_PORTS) ? n : 0;
}

// One buffer, refilled and printed per line. println(const char*) is the CHEAP
// overload — the linker shares it — so several calls to it cost almost nothing,
// where each additional println(F(...)) site inlines a whole MessageStream
// Flash-string path (~175 B of RAM1 code, and teensy41's ITCM page is finite).
//
// The bus KIND arrives as an enum, not as const char* wording. Passing
// "I2C sensors" / "I2C_WIRED_BUS" / "BUS I2C" as arguments is the obvious
// interface and costs ~95 B of AVR .data: a plain string literal is RAM-resident
// on AVR, and only the PSTR format strings stay in flash. Deriving all three
// words from a 3-character kind copied out of PROGMEM keeps mega2560's RAM flat.
enum : uint8_t { BK_I2C = 0, BK_SPI = 1, BK_CAN = 2 };

__attribute__((noinline))
static void printBusNotStarted(uint8_t kind, const char* busName, bool fromWiring,
                               const uint8_t* pins, uint8_t count, bool refused) {
    char buf[88];
    char k[4];
    strncpy_P(k, kind == BK_I2C ? PSTR("I2C") : kind == BK_SPI ? PSTR("SPI") : PSTR("CAN"),
              sizeof(k));
    k[sizeof(k) - 1] = '\0';

    // Find what is holding a pin, if anything, BEFORE choosing the wording: a
    // bus that is down is not automatically a fault.
    // Inputs never enter the registry, so ask the input probe too: a bus refused
    // because a configured input holds its pin used to read as "idle" (#564).
    // Only for a bus whose claim was REFUSED: a declared bus nothing needs is
    // allowed to leave its copper to inputs (#394), and is idle, not blocked.
    uint8_t blockedPin = 0xFF;
    const char* owner = nullptr;
    bool byInput = false;
    for (uint8_t i = 0; i < count; i++) {
        if (pins[i] == 0xFF) continue;
        if (refused && pinHeldByConfiguredInput(pins[i])) {
            blockedPin = pins[i];
            byInput = true;
            break;
        }
        if (getPinUsage(pins[i]) == PIN_UNUSED) continue;
        const char* d = getPinDescription(pins[i]);
        if (!d) continue;
        blockedPin = pins[i];
        owner = d;
        break;
    }

    if (!owner && !byInput) {
        // Nothing is in the way. Bring-up is NEED-DRIVEN — acquireRequiredBuses()
        // only starts a bus some stored input or the display actually asks for —
        // so a declared bus on a board with no I2C device configured is idle, not
        // broken. Reporting it as a failure would send the user hunting a fault
        // that does not exist, which is its own kind of lying status line.
        if (fromWiring) {
            snprintf_P(buf, sizeof(buf),
                       PSTR("Active: NONE - %s declared, idle until a device needs it"), busName);
        } else {
            snprintf_P(buf, sizeof(buf),
                       PSTR("Active: NONE - %s selected, idle until a device needs it"), busName);
        }
        msg.control.println(buf);
        return;
    }

    snprintf_P(buf, sizeof(buf), PSTR("Status:  NOT RUNNING - nothing can use %s"), k);
    msg.control.println(buf);

    if (fromWiring) {
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Wanted:  %s, declared by %s_WIRED_BUS in wiring.ini"), busName, k);
    } else {
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Wanted:  %s, selected with BUS %s"), busName, k);
    }
    msg.control.println(buf);

    // The usual cause is a saved setting, not a wire: a serial port enabled for a
    // module that has since been unplugged still claims its pins every boot. Name
    // the setting to turn off, since that is what the user actually changes.
    uint8_t port = byInput ? 0 : serialPortHoldingPin(blockedPin);
    if (byInput) {
        snprintf_P(buf, sizeof(buf), PSTR("  Blocked: pin %u is held by a configured input"),
                   (unsigned)blockedPin);
        msg.control.println(buf);
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Fix:     CLEAR that input, or choose another %s bus"), k);
    } else if (port) {
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Blocked: pin %u is held by %s (enabled in saved config)"),
                   (unsigned)blockedPin, owner);
        msg.control.println(buf);
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Fix:     BUS SERIAL %u DISABLE if that is no longer wired,"),
                   (unsigned)port);
        msg.control.println(buf);
        snprintf_P(buf, sizeof(buf), PSTR("           or move to another bus: BUS %s <n>"), k);
    } else {
        snprintf_P(buf, sizeof(buf), PSTR("  Blocked: pin %u is held by %s"),
                   (unsigned)blockedPin, owner);
        msg.control.println(buf);
        snprintf_P(buf, sizeof(buf),
                   PSTR("  Fix:     free that pin, or choose another bus: BUS %s <n>"), k);
    }
    msg.control.println(buf);
}

// The "Available buses:" line for all three bus types, built in one buffer.
// Twelve msg.control.print(F(...)) sites collapse to one println(const char*),
// which is the overload the linker actually shares — that is what buys back the
// ITCM page room the richer not-started message spends.
__attribute__((noinline))
static void printAvailableBuses(uint8_t count, const char* (*nameFn)(uint8_t)) {
    char b[64];
    int n = snprintf_P(b, sizeof(b), PSTR("Available buses: "));
    for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(b); i++) {
        if (i) n += snprintf_P(b + n, sizeof(b) - n, PSTR(", "));
        n += snprintf_P(b + n, sizeof(b) - n, PSTR("%u=%s"), (unsigned)i, nameFn(i));
    }
    msg.control.println(b);
}

__attribute__((noinline))
static void printWiredBusLine(const char* wiredName) {
    msg.control.print(F("Wired (build-time): "));
    msg.control.println(wiredName);
}

// Helper function to display I2C bus configuration
FLASHMEM void displayI2CStatus() {
    // Report the bus that is actually up, not just the stored selection: a client
    // may have acquired the platform default on demand (#351), and a status line
    // reading "NONE" while the peripheral is clocking is exactly the confusion
    // this command exists to resolve.
    // Resolve exactly as acquireI2C() does — selection, else WIRING declaration.
    // Reading only the stored selection was the bug: a user who declared
    // I2C_WIRED_BUS=1 and lost a pin fight got "use 'BUS I2C 0' to enable",
    // which names the wrong bus and never mentions what is actually holding it.
    uint8_t bus_id = active_i2c ? active_i2c_id
                                : resolveWiredBusId(systemConfig.buses.active_i2c,
                                                    BUS_WIRED_I2C_ID);
    msg.control.println();
    msg.control.println(F("=== I2C Bus Configuration ==="));
    if (!active_i2c && bus_id != 0xFF) {
        // Wanted but not up. Its own branch: printing "Active: Wire2 (SDA=25,
        // SCL=24) @ 400kHz" and contradicting it underneath is what made this
        // unreadable.
        const uint8_t pins[2] = { getDefaultI2CSDA(bus_id), getDefaultI2CSCL(bus_id) };
        printBusNotStarted(BK_I2C, getI2CBusName(bus_id),
                           systemConfig.buses.active_i2c == 0xFF, pins, 2,
                           i2c_refused_id == bus_id);
    } else {
    msg.control.print(F("Active: "));
    if (bus_id == 0xFF) {
        msg.control.println(F("NONE - none selected or declared. Use: BUS I2C <n>"));
    } else {
        char b[80];
        snprintf_P(b, sizeof(b), PSTR("%s (SDA=%u, SCL=%u) @ %ukHz"),
                   getI2CBusName(bus_id), (unsigned)getDefaultI2CSDA(bus_id),
                   (unsigned)getDefaultI2CSCL(bus_id),
                   (unsigned)systemConfig.buses.i2c_clock);
        msg.control.println(b);
        if (systemConfig.buses.active_i2c == 0xFF) {
            msg.control.println(F("  (from the wiring declaration, not a BUS I2C selection)"));
        }
    }
    }
#if BUS_WIRED_I2C_ID != 0xFF
    printWiredBusLine(getI2CBusName(BUS_WIRED_I2C_ID));
#else
    printWiredBusLine("none declared");
#endif
    printAvailableBuses(NUM_I2C_BUSES, getI2CBusName);
}

// Helper function to display SPI bus configuration
FLASHMEM void displaySPIStatus() {
    // Declaration-aware, as in displayI2CStatus() — see the note there.
    uint8_t bus_id = active_spi ? active_spi_id
                                : resolveWiredBusId(systemConfig.buses.active_spi,
                                                    BUS_WIRED_SPI_ID);
    msg.control.println();
    msg.control.println(F("=== SPI Bus Configuration ==="));
    if (!active_spi && bus_id != 0xFF) {
        const uint8_t pins[3] = { getDefaultSPIMOSI(bus_id), getDefaultSPIMISO(bus_id),
                                  getDefaultSPISCK(bus_id) };
        printBusNotStarted(BK_SPI, getSPIBusName(bus_id),
                           systemConfig.buses.active_spi == 0xFF, pins, 3,
                           spi_refused_id == bus_id);
    } else {
    msg.control.print(F("Active: "));
    if (bus_id == 0xFF) {
        msg.control.println(F("NONE - none selected or declared. Use: BUS SPI <n>"));
    } else {
        char b[80];
        // %u.%u rather than %f: AVR's printf is built without float support, so
        // a %f here would emit "?" on mega2560.
        // %u not %lu: the quotient fits an unsigned int for any real SPI clock,
        // and %lu drags AVR's long-printf support in for nothing.
        snprintf_P(b, sizeof(b), PSTR("%s (MOSI=%u, MISO=%u, SCK=%u) @ %u.%uMHz"),
                   getSPIBusName(bus_id), (unsigned)getDefaultSPIMOSI(bus_id),
                   (unsigned)getDefaultSPIMISO(bus_id), (unsigned)getDefaultSPISCK(bus_id),
                   (unsigned)(systemConfig.buses.spi_clock / 1000000UL),
                   (unsigned)((systemConfig.buses.spi_clock / 100000UL) % 10UL));
        msg.control.println(b);
    }
    }
#if BUS_WIRED_SPI_ID != 0xFF
    printWiredBusLine(getSPIBusName(BUS_WIRED_SPI_ID));
#else
    printWiredBusLine("none declared");
#endif
    printAvailableBuses(NUM_SPI_BUSES, getSPIBusName);
}

// Helper function to display CAN bus configuration
FLASHMEM void displayCANStatus() {
    msg.control.println();
    msg.control.println(F("=== CAN Bus Configuration ==="));
#if NUM_CAN_BUSES > 0
    // Display input bus
    msg.control.print(F("Input:  "));
    if (systemConfig.buses.input_can_bus != 0xFF && systemConfig.buses.can_input_mode != CAN_INPUT_OFF) {
        msg.control.print(getCANBusName(systemConfig.buses.input_can_bus));
        if (systemConfig.buses.can_input_mode == CAN_INPUT_LISTEN) {
            msg.control.print(F(" (LISTEN) @ "));
        } else {
            msg.control.print(F(" (NORMAL) @ "));
        }
        msg.control.print(systemConfig.buses.can_input_baudrate / 1000);
        msg.control.print(F("kbps"));
    } else {
        msg.control.print(F("DISABLED"));
    }
    msg.control.println();

    // Display output bus
    msg.control.print(F("Output: "));
    if (systemConfig.buses.output_can_bus != 0xFF && systemConfig.buses.can_output_enabled) {
        msg.control.print(getCANBusName(systemConfig.buses.output_can_bus));
        msg.control.print(F(" (ENABLED) @ "));
        msg.control.print(systemConfig.buses.can_output_baudrate / 1000);
        msg.control.print(F("kbps"));
        msg.control.println();
        if (!can_ready) {
            const uint8_t pins[2] = { getDefaultCANTX(systemConfig.buses.output_can_bus),
                                      getDefaultCANRX(systemConfig.buses.output_can_bus) };
            // A CAN declaration seeds output_can_bus at defaults time rather than
            // being consulted at bring-up, so by here it is always a stored value.
            printBusNotStarted(BK_CAN, getCANBusName(systemConfig.buses.output_can_bus),
                               false, pins, 2,
                               (can_refused_mask >> systemConfig.buses.output_can_bus) & 1);
        }
    } else {
        msg.control.print(F("DISABLED"));
        msg.control.println();
    }
#if BUS_WIRED_CAN_ID != 0xFF
    printWiredBusLine(getCANBusName(BUS_WIRED_CAN_ID));
#else
    printWiredBusLine("none declared");
#endif
    printAvailableBuses(NUM_CAN_BUSES, getCANBusName);
#else
    msg.control.println(F("No CAN buses available on this platform"));
#endif
}

