/*
 * sensor_calibration_data.h - Sensor Calibration Database (Orchestrator)
 *
 * This file orchestrates all sensor calibration implementations by including
 * modular calibration files organized by manufacturer/source.
 *
 * Directory structure:
 * - sensor_calibration_data/vdo/        - VDO thermistors & pressure sensors
 * - sensor_calibration_data/aem/        - AEM performance sensors
 * - sensor_calibration_data/nxp/        - NXP/Freescale integrated sensors
 * - sensor_calibration_data/generic/    - Generic/aftermarket sensors
 * - sensor_calibration_data/system/     - System default calibrations
 */

#ifndef SENSOR_CALIBRATION_DATA_H
#define SENSOR_CALIBRATION_DATA_H

#include <Arduino.h>
#include "sensor_types.h"
#include "../config.h"

// ===== CALIBRATION DATA BY MANUFACTURER =====
// Include modular calibration files organized by manufacturer

#include "sensor_calibration_data/vdo/vdo_calibrations.h"
#include "sensor_calibration_data/aem/aem_calibrations.h"
#include "sensor_calibration_data/nxp/nxp_calibrations.h"
#include "sensor_calibration_data/generic/generic_calibrations.h"
#include "sensor_calibration_data/system/system_calibrations.h"

#endif // SENSOR_CALIBRATION_DATA_H
