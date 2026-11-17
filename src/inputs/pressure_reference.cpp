/*
 * pressure_reference.cpp - The barometric reference (#639); see the header.
 * The lookup moved from outputs/formats/obd_answer.cpp in phase 1.
 */

#include <Arduino.h>
#include <math.h>
#include "pressure_reference.h"
#include "input.h"
#include "input_manager.h"
#include "../sensors/generated/registry_enums.h"
#include "../sensors/sensor_library.h"
#include "../sensors/application_presets.h"
#include "../sensors/can_sensor_library/obd_output_pids.h"   // absolutePressureApplication

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static float baroLastGood = STD_BARO_BAR;

FLASHMEM float baroReferenceBar() {
    const Input* best = nullptr;
    bool configured = false;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        const Input& in = inputs[i];
        if (in.pin == 0xFF || !in.flags.isEnabled || in.applicationIndex != APP_BAROMETRIC_PRESSURE) continue;
        configured = true;
        if (isfinite(in.value) && in.value > 0.5f && in.value < 1.2f &&   // bar; outside it is a fault, not weather
            (best == nullptr || in.pin < best->pin)) {
            best = &in;
        }
    }
    if (best) baroLastGood = best->value;
    else if (!configured) baroLastGood = STD_BARO_BAR;   // removed: a later one starts from standard
    return baroLastGood;
}

FLASHMEM PressureReference applicationReference(uint8_t app) {
    if (app == APP_NONE || getApplicationExpectedMeasurementType(app) != MEASURE_PRESSURE) return REF_NONE;
    return absolutePressureApplication(app) ? REF_ABSOLUTE : REF_GAUGE;
}

FLASHMEM float pressureReferenceOffset(PressureReference from, PressureReference to, float baroBar) {
    if (from == REF_NONE || to == REF_NONE || from == to) return 0.0f;
    return to == REF_ABSOLUTE ? baroBar : -baroBar;
}

FLASHMEM void applyPressureReference(Input& in) {
    if (in.measurementType != MEASURE_PRESSURE || !isfinite(in.value)) return;
    // The barometric input is the reference itself: converting it would read itself
    if (in.applicationIndex == APP_BAROMETRIC_PRESSURE) return;
    const SensorInfo* row = getSensorByIndex(in.sensorIndex);
    if (row == nullptr || (void*)in.readFunction != pgm_read_ptr(&row->readFunction)) return;
    const PressureReference from = getSensorReference(in.sensorIndex);
    const PressureReference to = applicationReference(in.applicationIndex);
    if (from == REF_NONE || to == REF_NONE || from == to) return;   // skip the baro scan
    in.value += pressureReferenceOffset(from, to, baroReferenceBar());
}

FLASHMEM void readInputNow(Input& in) {
    in.readFunction(&in);
    in.flags.hasReading = 1;
    applyPressureReference(in);
}
