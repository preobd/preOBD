/*
 * pin_owner.cpp - The single "may this function own these pins?" predicate
 *
 * See pin_owner.h for the contract.
 */

#include "pin_owner.h"
#include "../core/message_api.h"
#include <string.h>

// Nullptr until the inputs layer registers one. That is the honest answer
// during early boot: loadInputConfig() has not run, so no input holds anything.
static PinOwnerInputProbe input_probe = nullptr;

void setPinOwnerInputProbe(PinOwnerInputProbe probe) {
    input_probe = probe;
}

// True only while setup() is reconciling the stored configuration against the
// hardware. The rank-based "stale configuration" wording is correct ONLY in that
// window: before boot finishes, a claimant that outranks its holder means the
// stored config is internally inconsistent. After boot, the same comparison says
// nothing of the sort — the user is creating a fresh conflict at the CLI, and
// the holder is very likely a perfectly good input or Action. Set false by
// pinOwnerBootComplete() at the end of setup().
static bool bootReconciling = true;

void pinOwnerBootComplete() { bootReconciling = false; }

// Nullptr until the bus layer registers one (#379). Early boot / host unit
// tests without a registration correctly see "no stored selection holds this
// pin" — the third source simply does not vote.
static PinOwnerBusProbe bus_probe = nullptr;

void setPinOwnerBusProbe(PinOwnerBusProbe probe) {
    bus_probe = probe;
}

// One shared format string: msg.debug.* format strings are RAM-resident on AVR,
// so per-caller variants would cost real bytes on the 8 KB mega.
static const char PIN_OWNER_BUSY[] = "%s not started: pins already claimed";

static bool pinHeldByInput(uint8_t pin) {
    return input_probe && input_probe(pin);
}

// ---------------------------------------------------------------------------
// Boot diagnostics buffer (#396) — see pin_owner.h for the rationale.
// ---------------------------------------------------------------------------

#ifndef PIN_OWNER_BOOT_DIAG_CAPACITY
    #ifdef __AVR__
        #define PIN_OWNER_BOOT_DIAG_CAPACITY 4   // mega2560: ~191 B free at #396 time
    #else
        #define PIN_OWNER_BOOT_DIAG_CAPACITY 8
    #endif
#endif

enum PinOwnerDiagKind : uint8_t {
    PDK_CONFLICT_INPUT_STALE,       // held by a configured input; boot-stale wording
    PDK_CONFLICT_INPUT_PRECEDENCE,  // held by a configured input; precedence wording
    PDK_CONFLICT_HELD_STALE,        // registry holder; boot-stale wording
    PDK_CONFLICT_HELD_PRECEDENCE,   // registry holder; precedence wording
    PDK_PENDING_BUS,                // held by a stored-but-not-yet-started bus selection
    PDK_CLAIM_FAILED,               // claimPins(): conflict check passed, reservation still failed
};

struct PinOwnerDiagRecord {
    uint8_t pin;
    const char* a;   // owner (CONFLICT_*/CLAIM_FAILED) or busHolder, maybe null (PENDING_BUS)
    const char* b;   // held description, or null (CONFLICT_HELD_*)
    uint8_t kind;
};

static PinOwnerDiagRecord bootDiagBuf[PIN_OWNER_BOOT_DIAG_CAPACITY];
static uint8_t bootDiagCount = 0;
static uint8_t bootDiagOverflow = 0;
static bool bootDiagBuffering = true;

bool pinOwnerBootDiagBuffering() { return bootDiagBuffering; }

static void bufferDiag(uint8_t pin, const char* a, const char* b, uint8_t kind) {
    if (bootDiagCount < PIN_OWNER_BOOT_DIAG_CAPACITY) {
        bootDiagBuf[bootDiagCount++] = { pin, a, b, kind };
    } else {
        bootDiagOverflow++;
    }
}

static void printDiag(uint8_t pin, const char* a, const char* b, uint8_t kind) {
    switch (kind) {
        case PDK_CONFLICT_INPUT_STALE:
        case PDK_CONFLICT_INPUT_PRECEDENCE:
            msg.control.print(F("ERROR: pin "));
            msg.control.print(pin);
            msg.control.print(F(" ("));
            msg.control.print(a);
            msg.control.println(F(") is in use by a configured input"));
            msg.control.println(kind == PDK_CONFLICT_INPUT_STALE
                ? F("  Stale configuration: this holder should not have this pin. Reconfigure or remove it.")
                : F("  That function has precedence; free the pin first, or choose another."));
            break;
        case PDK_CONFLICT_HELD_STALE:
        case PDK_CONFLICT_HELD_PRECEDENCE:
            msg.control.print(F("ERROR: pin "));
            msg.control.print(pin);
            msg.control.print(F(" ("));
            msg.control.print(a);
            msg.control.print(F(") is in use"));
            if (b) { msg.control.print(F(": ")); msg.control.print(b); }
            msg.control.println();
            msg.control.println(kind == PDK_CONFLICT_HELD_STALE
                ? F("  Stale configuration: this holder should not have this pin. Reconfigure or remove it.")
                : F("  That function has precedence; free the pin first, or choose another."));
            break;
        case PDK_PENDING_BUS:
            msg.control.print(F("ERROR: pin "));
            msg.control.print(pin);
            msg.control.print(F(" is claimed by "));
            if (b) msg.control.print(b);
            else msg.control.print(F("another bus selection"));
            msg.control.println(F("; free it first or choose another"));
            break;
        case PDK_CLAIM_FAILED:
            msg.control.print(F("ERROR: "));
            msg.control.print(a);
            msg.control.println(F(" not started - a pin it needs is already in use"));
            msg.control.println(F("  SYSTEM PINS shows the owner; reconfigure one of them."));
            break;
    }
}

// Buffer while router has no transport yet, print immediately once it does
// (or once boot has already flushed) — see pin_owner.h.
static void reportDiag(uint8_t pin, const char* a, const char* b, uint8_t kind) {
    if (bootDiagBuffering) bufferDiag(pin, a, b, kind);
    else printDiag(pin, a, b, kind);
}

void flushPinOwnerBootDiagnostics() {
    for (uint8_t i = 0; i < bootDiagCount; i++) {
        printDiag(bootDiagBuf[i].pin, bootDiagBuf[i].a, bootDiagBuf[i].b, bootDiagBuf[i].kind);
    }
    if (bootDiagOverflow) {
        msg.control.print(F("  ("));
        msg.control.print(bootDiagOverflow);
        msg.control.println(F(" more boot-time pin conflict(s) not shown)"));
    }
    bootDiagCount = 0;
    bootDiagOverflow = 0;
    bootDiagBuffering = false;   // idempotent: nothing buffered next call, nothing printed
}

bool pinsConflict(const uint8_t* pins, uint8_t count, uint8_t rank, const char* owner) {
    if (!pins || !owner) return false;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t pin = pins[i];

        // 0xFF is "this claimant has no such pin on this platform". It is also
        // the free-slot marker in inputs[], so the probe would report a phantom
        // conflict — screen it out before asking anything else.
        if (pin == 0xFF) continue;

        const bool heldByInput = pinHeldByInput(pin);

        // Self-ownership needs BOTH a matching owner string AND PIN_RESERVED —
        // neither alone is enough. Type-only would, on Teensy 4.x, see
        // Serial4's I2C1-overlapping (17/16 vs 16/17) entry and, for anything
        // sharing that type, wrongly conclude "that's us". Owner-only is worse:
        // Action targets deliberately share ONE description ("Action output",
        // control_manager.cpp) across every action, so matching on the string
        // alone would let a second action claim a pin the first already has —
        // two Rules driving the same relay with no diagnostic. PIN_RESERVED is
        // what makes "this claimant is re-checking something already up" a safe
        // skip: only bus/serial claims register that way, and each such call
        // passes a description unique to its own bus/port.
        const char* held = getPinDescription(pin);
        if (!heldByInput && held && getPinUsage(pin) == PIN_RESERVED &&
            strcmp(held, owner) == 0) continue;

        if (!heldByInput && isPinAvailable(pin)) {
            // The registry has nothing on this pin — consult source 3: a
            // stored BUS SELECTION that has not started yet (#379). Checked
            // only when the registry is silent, so an ALREADY-STARTED bus
            // (which registered its pins at claim time) is judged by the
            // ordinary registry path above, with the usual rank wording —
            // this branch exists only for the gap between "selected" and
            // "started".
            const char* busHolder = nullptr;
            if (bus_probe && bus_probe(pin, owner, &busHolder)) {
                // Deliberately NOT the stale/precedence rank wording above:
                // that logic answers "does an EXISTING claim disagree with
                // the ladder", which presumes the holder is already a real,
                // established fact. A pending selection is not established —
                // it is a second, fresher intention the user is creating
                // RIGHT NOW by typing this very command, so calling it STALE
                // would be backwards regardless of which rank is numerically
                // lower. Refuse plainly instead. Buffered/printed via
                // reportDiag() so a pending-bus refusal during boot survives
                // to be seen too (#396).
                reportDiag(pin, owner, busHolder, PDK_PENDING_BUS);
                return true;
            }
            continue;
        }

        const uint8_t holderRank = heldByInput ? (uint8_t)PIN_RANK_INPUT : getPinRank(pin);

        // No runtime eviction either way — this always refuses. Rank only
        // decides which explanation is true, and ONLY during boot (see below).
        // reportDiag() buffers this during boot (before the router has a
        // transport) and replays it once flushPinOwnerBootDiagnostics() runs,
        // rather than printing straight to msg.control where it would be
        // silently discarded (#396) — the message itself is unchanged.
        if (bootReconciling && holderRank != 0 && rank < holderRank) {
            // Boot only: the claimant should have won by the ladder, so the
            // holder is the anomaly — the stored config disagrees with the
            // hardware and needs a human. Loud on purpose.
            reportDiag(pin, owner, held,
                       heldByInput ? PDK_CONFLICT_INPUT_STALE : PDK_CONFLICT_HELD_STALE);
        } else {
            // Everything after boot, and any boot conflict the ladder explains.
            // A rank comparison must NOT pick the wording here: once setup() is
            // done, a conflict is something the user is creating RIGHT NOW by
            // typing a command, not an established claim gone stale. Serial
            // (rank 2) over a configured input (4) or an Action target (5) is
            // exactly that case, and telling the user their configuration is
            // stale sends them off to delete a sensor that is doing nothing
            // wrong (found on teensy40 bench, #384 verification).
            reportDiag(pin, owner, held,
                       heldByInput ? PDK_CONFLICT_INPUT_PRECEDENCE : PDK_CONFLICT_HELD_PRECEDENCE);
        }
        return true;
    }
    return false;
}

bool claimPins(const uint8_t* pins, uint8_t count, PinUsageType type, uint8_t rank,
              const char* tag, const char* owner) {
    // Inputs first: they are invisible to the registry, so reservePins_unchecked()
    // would happily hand a claimant the copper an input is already reading (#358).
    if (pinsConflict(pins, count, rank, owner)) {
        msg.debug.error(tag, PIN_OWNER_BUSY, owner);
        return false;
    }

    // registerPin_unchecked() is first-writer-wins and the overlaps are real on
    // stock hardware (Teensy 4.x I2C1 17/16 vs Serial4 16/17), so the claim is
    // all or nothing: a half-claim leaves a caller believing it owns another
    // function's pin.
    if (reservePins_unchecked(pins, count, type, owner, rank)) return true;

    msg.debug.error(tag, PIN_OWNER_BUSY, owner);
    reportDiag(0xFF, owner, nullptr, PDK_CLAIM_FAILED);
    return false;
}
