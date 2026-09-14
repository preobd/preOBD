/*
 * pin_label.cpp - Pin-encoding decoder: physical pin number -> display label.
 *
 * Moved verbatim from inputs/input_manager.cpp (#402 step 1). Zero behaviour
 * change intended.
 */

#include "pin_label.h"
#include "analog_pins.h"
#include <Arduino.h>
#include <stdio.h>

#ifndef FLASHMEM
#define FLASHMEM
#endif

FLASHMEM void formatPinLabel(uint8_t pin, char* buf, size_t bufLen) {
    // snprintf_P + PSTR: the format strings stay in flash on AVR (plain
    // string literals there are RAM-resident and count against the 8 KB
    // budget); both are passthroughs on ARM.
    if (pin >= 0xF0) {
        snprintf_P(buf, bufLen, PSTR("I2C:%u"), (unsigned)(pin - 0xF0));
    } else if (pin >= 0xC0 && pin < 0xE0) {
        snprintf_P(buf, bufLen, PSTR("CAN:%u"), (unsigned)(pin - 0xC0));
    } else {
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            snprintf_P(buf, bufLen, PSTR("A%d"), analogNum);
        } else {
            snprintf_P(buf, bufLen, PSTR("%u"), (unsigned)pin);
        }
    }
}
