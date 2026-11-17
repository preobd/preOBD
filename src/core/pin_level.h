/*
 * pin_level.h - make a held pin an output without a glitch (§12.5)
 *
 * The one place a newly claimed pin is turned into an output. The level is
 * set before the driver turns on, so the pin never passes through the other
 * level: an active-low relay is not pulsed, a chip select doesn't select.
 *
 * Header-only so the native tests that #include a firmware .cpp need nothing
 * more on their build line.
 */

#ifndef PIN_LEVEL_H
#define PIN_LEVEL_H

#include <Arduino.h>

// arduino-esp32 3.x refuses digitalWrite() before pinMode(); this relies on
// the 2.x core the board uses today.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#error "arduino-esp32 3.x refuses digitalWrite() before pinMode(): pinWriteLatch() needs a gpio_set_level() path (§12.5)"
#endif

// Write the output latch of a pin that is still an input. On Teensy 3.x/4.x
// digitalWrite() on an input sets a pull instead, so it must be
// digitalWriteFast() there.
inline void pinWriteLatch(uint8_t pin, uint8_t level) {
#if defined(TEENSYDUINO)
    // Teensy 3's runtime digitalWriteFast() has no range check: a virtual pin
    // (I2C, CAN: 0xC0+) would index past the pin table and write anywhere.
    if (pin >= CORE_NUM_DIGITAL) return;
    digitalWriteFast(pin, level);
#else
    digitalWrite(pin, level);
#endif
}

// INPUT first (no electrical change on a pin nobody drove; enables the STM32
// port clock, without which the latch write is lost; drops a stale pull-up),
// then the latch, then OUTPUT, then the level again for the cores whose latch
// write didn't take. Call only for a pin the caller holds.
inline void pinOutputAt(uint8_t pin, uint8_t level) {
    pinMode(pin, INPUT);
    pinWriteLatch(pin, level);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
}

#endif // PIN_LEVEL_H
