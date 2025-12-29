/*
 * system_calibrations.h - System Default Calibration Data
 *
 * Contains default system calibrations for internal sensors and
 * commonly used configurations (RPM sensing, voltage dividers).
 */

#ifndef SYSTEM_CALIBRATIONS_H
#define SYSTEM_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== RPM CALIBRATION =====

// Default W-Phase RPM calibration (12-pole alternator, 3:1 pulley ratio)
// Suitable for most automotive alternators
static const PROGMEM RPMCalibration default_rpm_cal = {
    .poles = 12,             // Most common automotive alternator
    .pulley_ratio = 3.0,     // Typical automotive ratio (range 2.5-3.5:1)
    .calibration_mult = 1.0, // No fine-tuning by default
    .timeout_ms = 2000,      // 2 seconds without pulse = engine stopped
    .min_rpm = 100,          // Minimum valid RPM (reject noise)
    .max_rpm = 10000         // Maximum valid RPM (reject spikes)
};

// Alternative: 12-pole alternator with 2:1 pulley ratio
// Common in older vehicles and light trucks
static const PROGMEM RPMCalibration rpm_12p_2to1_cal = {
    .poles = 12,             // 12-pole alternator
    .pulley_ratio = 2.0,     // 2:1 alternator to engine ratio
    .calibration_mult = 1.0, // No fine-tuning by default
    .timeout_ms = 2000,      // 2 seconds without pulse = engine stopped
    .min_rpm = 100,          // Minimum valid RPM (reject noise)
    .max_rpm = 10000         // Maximum valid RPM (reject spikes)
};

#endif // SYSTEM_CALIBRATIONS_H
