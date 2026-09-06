/*
 * bus_pins.h - The single "may this bus own these pins?" predicate
 *
 * Every bus (I2C, SPI, CAN) rests on the same invariant: a bus that is up owns
 * its pins, and nothing else may drive them (#351). Enforcing that needs TWO
 * sources of truth, not one:
 *
 *   1. the pin registry — what other *system* functions hold (serial ports,
 *      buttons, buzzer, CS lines, relay Action targets);
 *   2. the configured inputs — a sensor sitting on SDA/SCL. Inputs deliberately
 *      do NOT appear in the pin registry: mega2560's MAX_PIN_REGISTRY is 24 and
 *      would need ~32 to hold them, which its 181 free bytes cannot pay for.
 *      They are consulted through a probe instead, at zero storage cost.
 *
 * Source 2 used to be checked in exactly two CLI commands (`BUS I2C <n>` and
 * `DISPLAY TYPE LCD`) and in no boot path and for no other bus — which is how a
 * bus could come up over a configured input with no diagnostic in either
 * direction (#358). This header is that check's one home; both the boot path
 * and the CLI go through it.
 *
 * The probe is registered by the inputs layer (setBusPinInputProbe) so the bus
 * layer never includes an inputs header. Unregistered — early boot, host unit
 * tests — it simply reports "no input holds this pin", which is true before
 * loadInputConfig() has run.
 */

#ifndef BUS_PINS_H
#define BUS_PINS_H

#include <Arduino.h>

// Returns true if a CONFIGURED INPUT occupies `pin`.
typedef bool (*BusPinInputProbe)(uint8_t pin);

/** Register the configured-input probe (see header comment). */
void setBusPinInputProbe(BusPinInputProbe probe);

/**
 * Would `owner` conflict with anything already holding these pins?
 *
 * Pins equal to 0xFF ("not present on this platform") are skipped, as is a pin
 * this same owner already holds as PIN_RESERVED — re-checking a bus that is
 * already up must not report a conflict with itself.
 *
 * @return true if a conflict was found; a user-facing message has been printed.
 */
bool busPinsConflict(const uint8_t* pins, uint8_t count, const char* owner);

/**
 * Conflict check + all-or-nothing reservation, for a bus about to start.
 *
 * Claiming BEFORE the peripheral is started is deliberate: there is nothing to
 * un-begin afterwards (TwoWire has no portable end()), so a refusal has to be
 * decidable while the bus is still off.
 *
 * @return true if the pins are now reserved to `owner`.
 */
bool claimBusPins(const uint8_t* pins, uint8_t count, const char* tag, const char* owner);

#endif // BUS_PINS_H
