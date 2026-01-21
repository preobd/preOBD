/*
 * nxp_calibrations.h - NXP/Freescale Sensor Calibration Data
 *
 * Contains calibrations for NXP (formerly Freescale) sensors.
 * Includes MPX series integrated pressure sensors with signal conditioning.
 */

#ifndef NXP_CALIBRATIONS_H
#define NXP_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== NXP/FREESCALE MPX SERIES PRESSURE SENSORS =====

// Freescale (NXP) MPX4250AP (20-250 kPa, 0.2V-4.7V)
// Source: MPX4250AP datasheet
// Integrated pressure sensor with signal conditioning
// Output: 0.2V @ 20kPa, 4.7V @ 250kPa
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration mpx4250ap_linear_cal = {
    .voltage_min = 0.2,
    .voltage_max = 4.7,
    .output_min = 0.2,    // 20 kPa = 0.2 bar
    .output_max = 2.5     // 250 kPa = 2.5 bar
};

// Freescale (NXP) MPX5700AP (15-700 kPa, 0.2V-4.7V)
// Source: MPX5700AP datasheet
// Integrated pressure sensor with signal conditioning
// Output: 0.2V @ 15kPa, 4.7V @ 700kPa
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration mpx5700ap_linear_cal = {
    .voltage_min = 0.2,
    .voltage_max = 4.7,
    .output_min = 0.15,   // 15 kPa = 0.15 bar
    .output_max = 7.0     // 700 kPa = 7.0 bar
};

#endif // NXP_CALIBRATIONS_H
