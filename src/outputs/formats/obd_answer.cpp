/*
 * obd_answer.cpp - obd_encode.h's answer builder: reads the live input table
 * and the barometric reference (inputs/pressure_reference), and keeps the
 * legacy encoding for PIDs no table defines.
 */

#include <Arduino.h>
#include <math.h>
#include "obd_encode.h"
#include "../../inputs/input.h"
#include "../../inputs/input_manager.h"
#include "../../inputs/pressure_reference.h"
#include "../../sensors/sensor_library.h"
#include "../../sensors/generated/registry_enums.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM uint8_t obdBuildAnswer(const Input& in, uint8_t* out) {
    if (in.obd2pid == 0 || !isfinite(in.value)) return 0;
    out[0] = 0x41;
    out[1] = in.obd2pid;

    ObdOutputPid spec;
    if (obdOutputSpec(in.obd2pid, spec)) {
        if (!obdSpecAccepts(spec, (MeasurementType)in.measurementType)) return 0;
        // The value is in its application's reference (§14.8); the PID has
        // its own: absolute (OBD_OUT_ABSOLUTE) or gauge. One rule, any
        // pressure (#639 decision 7): BOOST on 0x70 adds baro, MAP on 0x0A
        // takes it off.
        float v = in.value;
        if (spec.measurementType == MEASURE_PRESSURE) {
            const PressureReference from = applicationReference(in.applicationIndex);
            const PressureReference to = (spec.flags & OBD_OUT_ABSOLUTE) ? REF_ABSOLUTE : REF_GAUGE;
            if (from != REF_NONE && from != to) v += pressureReferenceOffset(from, to, baroReferenceBar());
        }
        uint8_t n = obdEncodeValue(spec, v, false, 0.0f, &out[2]);
        return n ? (uint8_t)(2 + n) : 0;
    }

    // A PID no table defines (the user's own choice): preOBD's legacy
    // per-measurement encoding, with the input's own length
    uint8_t dataBytes = in.obd2length;
    if (dataBytes == 0 || dataBytes > 5) return 0;
    float obdValue = getObdConvertFunc((MeasurementType)in.measurementType)(in.value);
    if (!(obdValue > 0.0f)) obdValue = 0.0f;   // unsigned encodings: below the floor reads as the floor
    const float maxRaw = dataBytes >= 4 ? 4294967040.0f : (float)((1UL << (8 * dataBytes)) - 1);
    if (obdValue > maxRaw) obdValue = maxRaw;
    uint32_t value = (uint32_t)obdValue;
    for (uint8_t i = 0; i < dataBytes; i++) {
        uint8_t shift = (dataBytes - 1 - i) * 8;   // a 5th byte is the top, always 0
        out[2 + i] = shift >= 32 ? 0 : (uint8_t)(value >> shift);
    }
    return (uint8_t)(2 + dataBytes);
}
