/*
 * measurement_compat.cpp - see measurement_compat.h
 */

#include "measurement_compat.h"
#include "../sensors/sensor_library.h"
#include "../sensors/application_presets.h"
#include "../sensors/units_registry.h"

bool sensorMatchesApplication(uint8_t sensorIndex, uint8_t applicationIndex) {
    // "Nothing is paired yet" — SENSOR_NONE / APP_NONE. There is genuinely no
    // constraint to violate, so this is a real match, not an unknown.
    if (sensorIndex == 0 || applicationIndex == 0) return true;

    // "I cannot tell" — an index outside its table. This must REFUSE, not
    // wave through: the answer is unknown, and an unknown pairing is exactly
    // what this predicate exists to stop. Answering "compatible" here would
    // also mask a wrong answer from underneath, because
    // getApplicationExpectedMeasurementType() returns MEASURE_TEMPERATURE for
    // an out-of-range index (application_presets.h) — a plausible-looking
    // value, not an error. Every caller today resolves both indices before
    // calling, so this branch is unreachable in current code; it is the
    // default the NEXT caller inherits.
    if (sensorIndex >= NUM_SENSORS || applicationIndex >= NUM_APPLICATION_PRESETS) return false;

    return getSensorMeasurementType(sensorIndex) ==
           getApplicationExpectedMeasurementType(applicationIndex);
}

MeasurementType inputMeasurementType(uint8_t sensorIndex, uint8_t applicationIndex) {
    if (sensorIndex != 0 && sensorIndex < NUM_SENSORS) return getSensorMeasurementType(sensorIndex);
    return getApplicationExpectedMeasurementType(applicationIndex);
}

// The registry has one PERCENT unit, tagged MEASURE_HUMIDITY, and it is also
// the (only, and default) unit of PERCENT inputs (TPS, fuel level) and DIGITAL
// ones (float switch) — neither type has a unit of its own.
static bool isPercentFamily(MeasurementType t) {
    return t == MEASURE_HUMIDITY || t == MEASURE_PERCENT || t == MEASURE_DIGITAL;
}

bool unitsMatchMeasurement(uint8_t unitsIndex, MeasurementType type) {
    if (unitsIndex >= NUM_UNITS) return false;
    const MeasurementType unitType = (MeasurementType)pgm_read_byte(&UNITS_REGISTRY[unitsIndex].measurementType);
    return unitType == type || (isPercentFamily(unitType) && isPercentFamily(type));
}
