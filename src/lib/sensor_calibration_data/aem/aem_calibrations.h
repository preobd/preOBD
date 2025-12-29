/*
 * aem_calibrations.h - AEM Performance Sensor Calibration Data
 *
 * Contains calibrations for AEM Electronics performance sensors.
 * Includes AEM 30-series stainless steel pressure sensors.
 */

#ifndef AEM_CALIBRATIONS_H
#define AEM_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== AEM 30-SERIES PRESSURE SENSORS =====

// AEM 30-2130-150: 150 PSIg stainless steel pressure sensor (0.5V-4.5V)
// Source: AEM datasheet - Transfer function: PSI = (37.5*V)-18.75
// Accuracy: ±0.5% Full Scale over -40C to 105C
// Common for oil pressure and fuel pressure monitoring
// WARNING: Designed for 5V systems. For 3.3V systems, use voltage divider
static const PROGMEM LinearCalibration aem_30_2130_150_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .output_min = 0.0,
    .output_max = 10.34     // 150 PSI = 10.34 bar (base unit)
};

#endif // AEM_CALIBRATIONS_H
