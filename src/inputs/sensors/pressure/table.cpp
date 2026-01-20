/*
 * table.cpp - VDO Table-based Pressure Sensor Reading
 *
 * Implements pressure reading for VDO sensors using PROGMEM lookup tables.
 * Most accurate method when using manufacturer-provided resistance/pressure data.
 * Used by VDO 2-bar (360 043) and VDO 5-bar (360 003) sensors.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"

/**
 * Read pressure sensor using lookup table interpolation
 *
 * Converts sensor resistance to pressure by interpolating between
 * values in a PROGMEM lookup table. Provides highest accuracy when using
 * manufacturer calibration data.
 *
 * @param ptr  Pointer to Input structure to store pressure reading
 *
 * @note Requires preset calibration with lookup table in PROGMEM
 * @note Returns NAN if calibration data is missing or ADC reading is invalid
 * @note Uses interpolateAscending() since resistance increases with pressure
 */
void readPressureTable(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    // Get calibration from PROGMEM (REQUIRED for table method)
    if (ptr->calibrationType != CAL_PRESSURE_TABLE || ptr->presetCalibration == nullptr) {
        ptr->value = NAN;  // Can't do lookup without table
        return;
    }

    const PressureTableCalibration* cal = (const PressureTableCalibration*)ptr->presetCalibration;

    // Read calibration values from PROGMEM
    float R_bias = pgm_read_float(&cal->bias_resistor);
    float R_sensor = calculateResistance(reading, R_bias);

    if (isnan(R_sensor) || R_sensor <= 0) {
        ptr->value = NAN;
        return;
    }

    // Read lookup table info from PROGMEM
    byte table_size = pgm_read_byte(&cal->table_size);
    const float* resistance_table = (const float*)pgm_read_ptr(&cal->resistance_table);
    const float* pressure_table = (const float*)pgm_read_ptr(&cal->pressure_table);

    // Use ascending interpolation (resistance increases with pressure)
    ptr->value = interpolateAscending(R_sensor, table_size,
                                      resistance_table, pressure_table);
}
