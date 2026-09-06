/*
 * bus_pins.cpp - The single "may this bus own these pins?" predicate
 *
 * See bus_pins.h for the contract.
 */

#include "bus_pins.h"
#include "pin_registry.h"
#include "message_api.h"
#include <string.h>

// Nullptr until the inputs layer registers one. That is the honest answer
// during early boot: loadInputConfig() has not run, so no input holds anything.
static BusPinInputProbe input_probe = nullptr;

void setBusPinInputProbe(BusPinInputProbe probe) {
    input_probe = probe;
}

// One shared format string: msg.debug.* format strings are RAM-resident on AVR,
// so per-bus variants would cost real bytes on the 8 KB mega.
static const char BUS_PINS_BUSY[] = "%s not started: pins already claimed";

static bool pinHeldByInput(uint8_t pin) {
    return input_probe && input_probe(pin);
}

bool busPinsConflict(const uint8_t* pins, uint8_t count, const char* owner) {
    if (!pins || !owner) return false;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t pin = pins[i];

        // 0xFF is "this bus has no such pin on this platform". It is also the
        // free-slot marker in inputs[], so the probe would report a phantom
        // conflict — screen it out before asking anything else.
        if (pin == 0xFF) continue;

        const bool heldByInput = pinHeldByInput(pin);

        // Self-ownership must be keyed to the OWNER, not merely to PIN_RESERVED:
        // on Teensy 4.x, I2C1 (17/16) overlaps Serial4 (16/17), and serial ports
        // are registered first. A bare type check would see Serial4's RESERVED
        // entry, conclude "that's us" and wave the conflict through.
        const char* held = getPinDescription(pin);
        if (!heldByInput && held && getPinUsage(pin) == PIN_RESERVED &&
            strcmp(held, owner) == 0) continue;

        if (!heldByInput && isPinAvailable(pin)) continue;

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
        msg.control.println(F("  Free that pin first, or choose another bus"));
        return true;
    }
    return false;
}

bool claimBusPins(const uint8_t* pins, uint8_t count,
                  const char* tag, const char* owner) {
    // Inputs first: they are invisible to the registry, so reservePins() would
    // happily hand a bus the copper an input is already reading (#358).
    if (busPinsConflict(pins, count, owner)) {
        msg.debug.error(tag, BUS_PINS_BUSY, owner);
        return false;
    }

    // registerPin() is first-writer-wins and the overlaps are real on stock
    // hardware (Teensy 4.x I2C1 17/16 vs Serial4 16/17), so the claim is all or
    // nothing: a half-claim leaves a bus believing it owns another function's pin.
    if (reservePins(pins, count, owner)) return true;

    msg.debug.error(tag, BUS_PINS_BUSY, owner);
    msg.control.print(F("ERROR: "));
    msg.control.print(owner);
    msg.control.println(F(" not started - a pin it needs is already in use"));
    msg.control.println(F("  SYSTEM PINS shows the owner; reconfigure one of them."));
    return false;
}
