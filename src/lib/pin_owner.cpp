/*
 * pin_owner.cpp - The single "may this function own these pins?" predicate
 *
 * See pin_owner.h for the contract.
 */

#include "pin_owner.h"
#include "message_api.h"
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
                // lower. Refuse plainly instead.
                msg.control.print(F("ERROR: pin "));
                msg.control.print(pin);
                msg.control.print(F(" is claimed by "));
                if (busHolder) msg.control.print(busHolder);
                else msg.control.print(F("another bus selection"));
                msg.control.println(F("; free it first or choose another"));
                return true;
            }
            continue;
        }

        const uint8_t holderRank = heldByInput ? (uint8_t)PIN_RANK_INPUT : getPinRank(pin);

        msg.control.print(F("ERROR: pin "));
        msg.control.print(pin);
        msg.control.print(F(" ("));
        msg.control.print(owner);
        msg.control.print(F(") is in use"));
        if (heldByInput) {
            msg.control.println(F(" by a configured input"));
        } else {
            if (held) { msg.control.print(F(": ")); msg.control.print(held); }
            msg.control.println();
        }

        // No runtime eviction either way — this always refuses. Rank only
        // decides which explanation is true, and ONLY during boot (see below).
        if (bootReconciling && holderRank != 0 && rank < holderRank) {
            // Boot only: the claimant should have won by the ladder, so the
            // holder is the anomaly — the stored config disagrees with the
            // hardware and needs a human. Loud on purpose.
            msg.control.println(F("  Stale configuration: this holder should not have this pin. Reconfigure or remove it."));
        } else {
            // Everything after boot, and any boot conflict the ladder explains.
            // A rank comparison must NOT pick the wording here: once setup() is
            // done, a conflict is something the user is creating RIGHT NOW by
            // typing a command, not an established claim gone stale. Serial
            // (rank 2) over a configured input (4) or an Action target (5) is
            // exactly that case, and telling the user their configuration is
            // stale sends them off to delete a sensor that is doing nothing
            // wrong (found on teensy40 bench, #384 verification).
            msg.control.println(F("  That function has precedence; free the pin first, or choose another."));
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
    msg.control.print(F("ERROR: "));
    msg.control.print(owner);
    msg.control.println(F(" not started - a pin it needs is already in use"));
    msg.control.println(F("  SYSTEM PINS shows the owner; reconfigure one of them."));
    return false;
}
