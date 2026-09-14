/*
 * bus_pin_roles.h - "what is this pin, at the silicon level?" (#304)
 *
 * SYSTEM DIAG PINS sweeps unclaimed analog pins and reports what it measures.
 * On a Teensy, A4/A5 are the hardware I2C SDA/SCL, so with no bus running they
 * read "pulled high" (the bus pull-ups) with no explanation. This supplies the
 * missing context.
 *
 * The pin -> role mapping is derived entirely from the getDefault*() helpers in
 * bus_defaults.h, so there is no second copy of the platform pin map to drift
 * against theirs. Only the LABEL TEXT lives here.
 *
 * Why a .cpp and not an inline in bus_defaults.h: the labels must be
 * flash-resident, which needs PROGMEM arrays. Teensy 4's PSTR() declares a
 * static array inside a statement expression, so several in one function hit
 * "section type conflict"; and a header-inlined table would be duplicated into
 * every TU that includes bus_defaults.h (which is most of them). One TU with
 * one table solves both — the same shape as analog_pins.{h,cpp}.
 */

#ifndef BUS_PIN_ROLES_H
#define BUS_PIN_ROLES_H

#include <Arduino.h>

/**
 * This pin's hardware bus role, if any.
 *
 * Says WHAT the pin is wired to at the silicon level, never whether a bus is
 * currently running on it — callers already know that separately (a pin
 * registry entry, or its absence). In particular an unclaimed bus pin may mean
 * the bus was never selected, is declared but unneeded (#394), or was selected
 * and failed to start; this cannot distinguish them and must not imply it can.
 *
 * @param pin Physical pin number.
 * @return Flash-resident label such as "I2C0 SDA", or nullptr if the pin is
 *         not one of this platform's default bus pins.
 */
const __FlashStringHelper* busPinRole(uint8_t pin);

#endif // BUS_PIN_ROLES_H
