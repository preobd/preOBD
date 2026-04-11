/*
 * table.cpp - Table-based Fluid Level Sensor Reading
 *
 * Implements fluid level reading for VDO and other resistive level senders
 * using PROGMEM lookup tables. Supports both ascending (3-180Ω, 0-90Ω) and
 * descending (240-34Ω, 75-3Ω) resistance curves.
 *
 * Output is percentage: 0.0 = Empty, 100.0 = Full.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../sensor_utils.h"

/**
 * Read fluid level sensor using lookup table interpolation
 *
 * Converts sensor resistance to a level percentage by interpolating between
 * values in a PROGMEM lookup table. Supports ascending and descending
 * resistance curves via the calibration's ascending flag.
 *
 * @param ptr  Pointer to Input structure to store level reading (0-100%)
 *
 * @note Requires preset calibration with lookup table in PROGMEM
 * @note Returns NAN if calibration data is missing or ADC reading is invalid
 * @note Ascending sensors use interpolateAscending(); descending use interpolate()
 */
void readLevelTable(Input *ptr) {
    bool isValid;
    int reading = readAnalogPin(ptr->pin, &isValid);

    if (!isValid) {
        ptr->value = NAN;
        return;
    }

    if (ptr->calibrationType != CAL_LEVEL_TABLE || ptr->presetCalibration == nullptr) {
        ptr->value = NAN;
        return;
    }

    const LevelTableCalibration* cal = (const LevelTableCalibration*)ptr->presetCalibration;

    float R_bias = pgm_read_float(&cal->bias_resistor);
    float R_sensor = calculateResistance(reading, R_bias);

    if (isnan(R_sensor) || R_sensor < 0) {
        ptr->value = NAN;
        return;
    }

    byte table_size = pgm_read_byte(&cal->table_size);
    const float* resistance_table = (const float*)pgm_read_ptr(&cal->resistance_table);
    const float* level_table = (const float*)pgm_read_ptr(&cal->level_table);
    bool ascending = (bool)pgm_read_byte(&cal->ascending);

    if (ascending) {
        ptr->value = interpolateAscending(R_sensor, table_size, resistance_table, level_table);
    } else {
        ptr->value = interpolate(R_sensor, table_size, resistance_table, level_table);
    }
}
