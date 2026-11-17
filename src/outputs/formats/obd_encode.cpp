/*
 * obd_encode.cpp - the pure half of obd_encode.h: payload encoding and the
 * answerable check, no globals (test_obd_encode includes it directly).
 * obd_answer.cpp adds the parts that read the live input table.
 */

#include <Arduino.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include "obd_encode.h"
#include "field_encode.h"
#include "../../inputs/input.h"
#include "../../sensors/units_registry.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM uint8_t obdResolveUnits(const char* unitsName) {
    if (!unitsName) return UNITS_NOT_FOUND;
    char name[16];
    strncpy_P(name, unitsName, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    return findUnitsIndexByName(name);
}

float obdConvertUnits(float baseValue, uint8_t unitsIdx) {
    if (unitsIdx == UNITS_NOT_FOUND) return baseValue;
    return baseValue * pgm_read_float(&UNITS_REGISTRY[unitsIdx].conversionFactor)
                     + pgm_read_float(&UNITS_REGISTRY[unitsIdx].conversionOffset);
}

FLASHMEM uint8_t obdEncodeValue(const ObdOutputPid& spec, float baseValue, bool gaugeInput,
                       float baroBar, uint8_t* payload) {
    if (!isfinite(baseValue)) return 0;
    if (spec.length == 0 || spec.length > OBD_OUT_MAX_LENGTH) return 0;
    if (spec.valueOffset + spec.valueLength > spec.length) return 0;

    float v = baseValue;
    if ((spec.flags & OBD_OUT_ABSOLUTE) && gaugeInput) v += baroBar;   // gauge -> absolute (bar)
    // The PID's J1979 unit (kPa, degC ...), from the input's base unit
    v = obdConvertUnits(v, obdResolveUnits(spec.units));
    // A huge finite value can overflow to inf in the conversion; it must still
    // clamp to the field's max (as it always has), not read as "no value"
    if (v > FLT_MAX) v = FLT_MAX;
    else if (v < -FLT_MAX) v = -FLT_MAX;

    memset(payload, 0, spec.length);
    if (spec.support) payload[0] = spec.support;
    // valueLength is 1, 2 or 4 in every table row; an odd one encodes nothing
    encodeScaledField(v, spec.scale, spec.offset, spec.valueLength, 0, false,
                      payload + spec.valueOffset);
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
