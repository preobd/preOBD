/*
 * pressure_reference.cpp - The barometric reference (#639); see the header.
 * Moved from outputs/formats/obd_answer.cpp unchanged.
 */

#include <Arduino.h>
#include <math.h>
#include "pressure_reference.h"
#include "input.h"
#include "input_manager.h"
#include "../sensors/generated/registry_enums.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM float baroReferenceBar() {
    const Input* best = nullptr;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        const Input& in = inputs[i];
        if (in.pin != 0xFF && in.flags.isEnabled && in.applicationIndex == APP_BAROMETRIC_PRESSURE &&
            isfinite(in.value) && in.value > 0.5f && in.value < 1.2f &&   // bar; outside it is a fault, not weather
            (best == nullptr || in.pin < best->pin)) {
            best = &in;
        }
    }
    return best ? best->value : STD_BARO_BAR;
}
