/*
 * analog_pins.h - Canonical analog A-name <-> physical pin mapping.
 *
 * The Arduino core's A-names are NOT a contiguous `A0 + n` series on every
 * platform. On Teensy they are scattered: 4.1 has A0..A13 at pins 14..27 but
 * A14..A17 at pins 38..41; 3.5/3.6 are wildly non-contiguous (A10=64, A12=31).
 * Resolving `A<n>` as `A0 + n` (or labelling a pin as `A<pin - A0>`) therefore
 * silently targets/relabels the wrong pin on those boards (#302).
 *
 * This is the single source of truth for both directions, so the `SET A<n>`
 * resolver and every pin-label printer agree:
 *   - forward: analog index n (the "n" in "A<n>") -> physical pin number
 *   - reverse: physical pin -> analog index, or -1 if the pin isn't analog
 *
 * On AVR the A-names ARE contiguous, so the mapping stays formula-based with no
 * lookup table — the SRAM-starved mega gains nothing to store.
 */

#ifndef ANALOG_PINS_H
#define ANALOG_PINS_H

#include <stdint.h>

// Number of analog inputs the board exposes for configuration (A0 .. A<count-1>).
uint8_t analogPinCount();

// Forward map: analog index -> physical pin. Returns 0xFF if out of range.
uint8_t analogPinForIndex(uint8_t index);

// Reverse map: physical pin -> analog index (the "n" in "A<n>"), or -1 if the
// pin is not an analog input on this board.
int8_t analogIndexForPin(uint8_t pin);

#endif // ANALOG_PINS_H
