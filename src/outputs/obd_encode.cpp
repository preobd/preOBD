/*
 * obd_encode.cpp - the pure half of obd_encode.h: payload encoding and the
 * answerable check, no globals (test_obd_encode includes it directly).
 * obd_answer.cpp adds the parts that read the live input table.
 */

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "obd_encode.h"
#include "../inputs/input.h"
#include "../sensors/units_registry.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM uint8_t obdEncodeValue(const ObdOutputPid& spec, float baseValue, bool gaugeInput,
                       float baroBar, uint8_t* payload) {
    if (!isfinite(baseValue)) return 0;
    if (spec.length == 0 || spec.length > OBD_OUT_MAX_LENGTH) return 0;
    if (spec.valueOffset + spec.valueLength > spec.length) return 0;

    float v = baseValue;
    if ((spec.flags & OBD_OUT_ABSOLUTE) && gaugeInput) v += baroBar;   // gauge -> absolute (bar)
    if (spec.units) {
        // The PID's J1979 unit (kPa, degC ...), from the input's base unit
        char name[16];
        strncpy_P(name, spec.units, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        uint8_t u = findUnitsIndexByName(name);
        if (u != UNITS_NOT_FOUND) {
            v = v * pgm_read_float(&UNITS_REGISTRY[u].conversionFactor)
                  + pgm_read_float(&UNITS_REGISTRY[u].conversionOffset);
        }
    }

    // raw = (value - offset) / scale, rounded, clamped to the field in float
    // before any integer cast (casting an out-of-range float is undefined)
    float raw = (v - spec.offset) / spec.scale;
    const float maxRaw = spec.valueLength >= 4 ? 4294967295.0f
                                               : (float)((1UL << (8 * spec.valueLength)) - 1);
    uint32_t r;
    if (!(raw > 0.0f)) r = 0;                       // also NaN
    else if (raw + 0.5f >= maxRaw) r = spec.valueLength >= 4 ? 0xFFFFFFFFUL
                                                            : (uint32_t)maxRaw;
    else r = (uint32_t)(raw + 0.5f);

    memset(payload, 0, spec.length);
    if (spec.support) payload[0] = spec.support;
    for (uint8_t i = 0; i < spec.valueLength; i++) {
        payload[spec.valueOffset + i] = (uint8_t)(r >> (8 * (spec.valueLength - 1 - i)));
    }
    return spec.length;
}

FLASHMEM bool obdInputAnswerable(const Input& in) {
    if (in.obd2pid == 0) return false;
    ObdOutputPid spec;
    if (obdOutputSpec(in.obd2pid, spec)) {
        return obdSpecAccepts(spec, (MeasurementType)in.measurementType);
    }
    return in.obd2length >= 1 && in.obd2length <= 5;   // user's own PID: legacy encoding
}
