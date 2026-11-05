/*
 * board_pins.h - What this board's pins are: which exist, which pin each
 * analog name (A<n>) is, and the board's name. The single source for all
 * three (#570). Each is read from the Arduino core / PlatformIO board
 * definition wherever those know it, so preOBD keeps no copy of its own:
 *   - pins that exist:   NUM_DIGITAL_PINS (+ the ESP32 valid-GPIO mask)
 *   - analog names:      the core's A0..An constants, NUM_ANALOG_INPUTS of them
 *   - board name:        the ARDUINO_<BOARD> define the board JSON sets
 * Only what no core header can know comes from boards/<board>/board.h:
 * BOARD_ANALOG_USABLE (analog inputs the firmware can actually read, when
 * fewer than the core's count) and BOARD_RESERVED_PINS (pins that exist but
 * are wired to the module, claimed in the pin registry at boot).
 *
 * "Exists" and "is free" are separate questions: this answers the first; the
 * pin registry (pin_owner.h) answers the second.
 *
 * The A-names are NOT a contiguous `A0 + n` series on every board: Teensy 4.1
 * has A14..A17 at pins 38..41, and Teensy 3.6 is scattered (A10=64, A12=31).
 * Resolving `A<n>` as `A0 + n` targets the wrong pin there (#302), so both
 * directions go through one table of the core's constants.
 */

#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include <stdint.h>

// Number of analog inputs the board exposes for configuration (A0 .. A<count-1>).
uint8_t analogPinCount();

// Forward map: analog index -> physical pin. Returns 0xFF if out of range.
uint8_t analogPinForIndex(uint8_t index);

// Reverse map: physical pin -> analog index (the "n" in "A<n>"), or -1 if the
// pin is not an analog input on this board.
int8_t analogIndexForPin(uint8_t pin);

// True if `pin` is a physical pin on this board: a GPIO the core knows, or one
// of its A-names (Teensy 3.6 A10/A11 are ADC-only pins past the digital range).
// It may still be taken: ask the pin registry.
bool boardHasPin(uint8_t pin);

// This board's name as SYSTEM DUMP JSON reports it in firmware.platform
// ("TEENSY40", "ESP32S3"): the hardware, not the build (a hybrid-CAN build of a
// Teensy 4.1 is still TEENSY41).
const char* boardName();

#endif // BOARD_PINS_H
