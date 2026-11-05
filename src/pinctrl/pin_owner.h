/*
 * pin_owner.h - The single "may this function own these pins?" predicate,
 * firmware-wide.
 *
 * Every claimant of a physical pin — a bus (I2C, SPI, CAN), a serial port, the
 * RGB LED, a system pin (button/buzzer/CS), a configured input, a control-
 * engine Action — rests on the same invariant: a pin that is claimed is owned,
 * and nothing else may drive it (#351). Enforcing that needs THREE sources of
 * truth, not one:
 *
 *   1. the pin registry — what other *system* functions hold (serial ports,
 *      buttons, buzzer, CS lines, relay Action targets, bus pins);
 *   2. the configured inputs — a sensor sitting on SDA/SCL. Inputs deliberately
 *      do NOT appear in the pin registry: mega2560's MAX_PIN_REGISTRY is 24 and
 *      would need ~32 to hold them, which its 181 free bytes cannot pay for.
 *      They are consulted through a probe instead, at zero storage cost.
 *   3. a stored bus SELECTION (`BUS I2C <n>` etc.) that has not started yet —
 *      bring-up is deferred to reboot (bus_manager.cpp), so between the CLI
 *      command and the reboot the selected pins are in nobody's name unless
 *      something asks the stored config directly (#379). Also a probe, for
 *      the same reason as source 2.
 *
 * Source 2 used to be checked in exactly two CLI commands (`BUS I2C <n>` and
 * `DISPLAY TYPE LCD`) and in no boot path and for no other bus — which is how a
 * bus could come up over a configured input with no diagnostic in either
 * direction (#358). It was then generalized to every bus (#358 follow-up), but
 * still lived only in the bus layer with four callers — serial (#384) and SD
 * (#381) each consulted only part of the truth. This header is the ONE home for
 * the predicate, for every claimant, not just buses.
 *
 * The probe is registered by the inputs layer (setPinOwnerInputProbe) so this
 * module never includes an inputs header. Unregistered — early boot, host unit
 * tests — it simply reports "no input holds this pin", which is true before
 * loadInputConfig() has run.
 *
 * PRECEDENCE. Who wins a conflict used to be an accident of setup() call order
 * (registerPin_unchecked() is first-writer-wins, and main.cpp happened to call
 * system pins -> serial -> buses -> inputs -> actions). Moving a line in
 * setup() could silently change who owns a pin. PinRank below makes that order
 * an explicit, testable ladder instead.
 *
 * NO RUNTIME EVICTION. A claim ALWAYS refuses on conflict, regardless of rank —
 * there is no portable way to un-begin a peripheral (TwoWire has no end()), so
 * arbitration by eviction is not available here. Rank's job is diagnostic, not
 * arbitration: it tells the refusal message which side of the conflict is
 * legitimate ("that pin belongs to a higher-precedence function") versus
 * degraded ("the stored configuration is stale relative to the hardware" — the
 * claimant SHOULD have won by the ladder, so the thing holding the pin is the
 * anomaly and needs to be reconfigured or removed).
 */

#ifndef PIN_OWNER_H
#define PIN_OWNER_H

#include <Arduino.h>
#include "pin_registry.h"

// Precedence ladder for pin ownership. LOWER value wins. This is the explicit
// form of the setup() call order described above.
enum PinRank : uint8_t {
    PIN_RANK_BUILTIN = 1,  // wiring.ini / board.h physical facts (soldered copper)
    PIN_RANK_SERIAL  = 2,
    PIN_RANK_BUS     = 3,  // runtime BUS <x> <n> selections
    PIN_RANK_INPUT   = 4,
    PIN_RANK_ACTION  = 5,
};

// Returns true if a CONFIGURED INPUT occupies `pin`.
typedef bool (*PinOwnerInputProbe)(uint8_t pin);

/** Register the configured-input probe (see header comment). */
void setPinOwnerInputProbe(PinOwnerInputProbe probe);

/** Does a configured input hold `pin`? For status displays that name what blocks a bus (#564). */
bool pinHeldByConfiguredInput(uint8_t pin);

// Note on self-exclusion: a candidate re-validating its OWN unchanged pin must
// not see itself in the probe and misreport a self-conflict. That can't be
// solved HERE by excluding a pin value — two DIFFERENT inputs may legitimately
// (if transiently) share one pin (a "squatter" mid-reconfiguration), and a
// pin-keyed exclusion cannot tell "it's mine" from "someone else is also
// there". The probe implementation is the one place with enough information
// (which INPUT is `self`, not just which pin) to exclude correctly, so
// self-exclusion is the input layer's own problem, entirely behind
// PinOwnerInputProbe — see inputHoldsPin()'s exclusion in storage/input_store.cpp.
// This module stays free of that identity; it only ever asks "is this pin
// held", by pin.

// Returns true if a STORED BUS SELECTION (systemConfig.buses.*) not yet
// started occupies `pin` — see pinsConflict()'s third source below (#379).
// `excludeOwner`, when non-null, is the description of the bus this very
// call is trying to start; a match means "that's not a conflict, that's me"
// (mirrors the input probe's need for self-exclusion, but by description
// instead of by pin — a bus's own claim always uses its own default pins, so
// there is no "unchanged pin" ambiguity to thread through a parameter).
// On a true holder, `*descOut` is set to that bus's owner description.
typedef bool (*PinOwnerBusProbe)(uint8_t pin, const char* excludeOwner, const char** descOut);

/**
 * Called once at the end of setup(), after every boot-time claim has been made.
 *
 * Switches pinsConflict() out of boot-reconciliation mode. The rank-based
 * "stale configuration" wording is only truthful while the firmware is
 * reconciling a stored config against the hardware; afterwards every conflict
 * is one the user is creating at that moment, and the holder is usually a
 * perfectly valid input or Action.
 */
void pinOwnerBootComplete();

/** Register the stored-bus-selection probe (see header comment, #379). */
void setPinOwnerBusProbe(PinOwnerBusProbe probe);

// ---------------------------------------------------------------------------
// Boot diagnostics buffer (#396).
//
// registerSystemPins() and initConfiguredSerialPorts() both claim pins BEFORE
// router.begin() has registered any transport (main.cpp: pin claims at
// :289/:293, router.begin() at ~:334) — every pinsConflict()/claimPins()
// refusal printed in that window goes to msg.control with nowhere to
// deliver it and is silently discarded. That is exactly the "loser is
// always reported, never a silent drop" rule (ARCHITECTURE.md §12.4)
// failing during the one window that most needs it: a stale wiring.ini vs.
// EEPROM mismatch presents as a feature that just doesn't work.
//
// Mirrors systemConfig.cpp's bootDiag buffer for the same reason: no
// strings are copied, only stable pointers (owner/holder descriptions are
// always string literals or getI2CBusName()-style flash constants that
// outlive the call — pin_registry.h documents this for the registry itself)
// plus a few scalar/enum bytes. Capacity is fixed and board-tunable via
// PIN_OWNER_BOOT_DIAG_CAPACITY (define in board.h to override the default);
// overflow past capacity is counted, not dropped silently — the flush says
// "+N more".
bool pinOwnerBootDiagBuffering();

/**
 * Emit every buffered boot-time pin-conflict diagnostic, then stop
 * buffering — every claim after this point prints immediately, same as
 * before this feature existed. Call once, right after router.begin()
 * (main.cpp, beside flushSystemConfigBootDiagnostics()). Idempotent: a
 * second call finds nothing buffered and emits nothing.
 */
void flushPinOwnerBootDiagnostics();

/**
 * Would `owner`, claiming at `rank`, conflict with anything already holding
 * these pins?
 *
 * Pins equal to 0xFF ("not present on this platform") are skipped, as is a pin
 * this same owner already holds — re-checking a claimant that is already up
 * must not report a conflict with itself.
 *
 * @return true if a conflict was found; a user-facing message has been printed
 *         (which side is legitimate vs. stale is picked using `rank`).
 */
bool pinsConflict(const uint8_t* pins, uint8_t count, uint8_t rank, const char* owner);

/**
 * Conflict check + all-or-nothing reservation, for a claimant about to start.
 *
 * Claiming BEFORE the peripheral is started is deliberate: there is nothing to
 * un-begin afterwards (TwoWire has no portable end()), so a refusal has to be
 * decidable while the peripheral is still off.
 *
 * @param type  PinUsageType the pins are registered as on success
 * @param rank  PinRank this claim is made at (see enum above)
 * @return true if the pins are now reserved to `owner`.
 */
bool claimPins(const uint8_t* pins, uint8_t count, PinUsageType type, uint8_t rank,
               const char* tag, const char* owner);

#endif // PIN_OWNER_H
