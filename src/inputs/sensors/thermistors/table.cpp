/*
 * table.cpp - Thermistor Reading (Lookup Table Method)
 *
 * Implements thermistor temperature reading using PROGMEM lookup tables.
 * Most accurate method when using manufacturer-provided resistance/temperature data.
 * Used by VDO 120C (323 095) and VDO 150C (323 057) sensors.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"

/**
 * Read thermistor using lookup table interpolation
 *
 * Converts thermistor resistance to temperature by interpolating between
 * values in a PROGMEM lookup table. Provides highest accuracy when using
 * manufacturer calibration data.
 *
 * @param ptr  Pointer to Input structure to store temperature reading
 *
 * @note Requires preset calibration with lookup table in PROGMEM
 * @note Returns NAN if calibration data is missing or ADC reading is invalid
 */
void readThermistorLookup(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration from PROGMEM (REQUIRED for table method)
    if (ptr->calibrationType != CAL_THERMISTOR_TABLE || ptr->presetCalibration == nullptr) {
        ptr->value = NAN;  // Can't do lookup without table
        return;
    }

    const ThermistorLookupCalibration* cal = (const ThermistorLookupCalibration*)ptr->presetCalibration;

    // Read calibration values from PROGMEM
    float R_bias = pgm_read_float(&cal->bias_resistor);
    float R_thermistor = calculateResistance(reading, R_bias);

    if (isnan(R_thermistor) || R_thermistor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Read lookup table info from PROGMEM
    byte table_size = pgm_read_byte(&cal->table_size);
    const float* resistance_table = (const float*)pgm_read_ptr(&cal->resistance_table);
    const float* temperature_table = (const float*)pgm_read_ptr(&cal->temperature_table);

    ptr->value = interpolate(R_thermistor, table_size,
                            resistance_table, temperature_table);
}
