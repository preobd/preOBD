/*
 * sensor_calibration_data.h - Sensor Calibration Database (Orchestrator)
 *
 * This file orchestrates all sensor calibration implementations by including
 * modular calibration files organized by manufacturer/source.
 *
 * Directory structure:
 * - sensor_calibration_data/vdo/           - VDO thermistors & pressure sensors
 * - sensor_calibration_data/aem/           - AEM performance sensors
 * - sensor_calibration_data/nxp/           - NXP/Freescale integrated sensors
 * - sensor_calibration_data/generic/       - Generic/aftermarket sensors
 * - sensor_calibration_data/system/        - System default calibrations
 * - sensor_calibration_data/jeep/          - Jeep/AMC sensors (CJ/SJ/XJ 4.0L)
 * - sensor_calibration_data/smiths/        - Smiths (British classics: MG, Triumph, Jaguar)
 * - sensor_calibration_data/stewart_warner/ - Stewart-Warner (US hot rod/performance)
 * - sensor_calibration_data/acdelco/       - AC Delco / GM OEM gauge senders
 * - sensor_calibration_data/bosch/         - Bosch NTC sensors (universal EFI)
 */

#ifndef SENSOR_CALIBRATION_DATA_H
#define SENSOR_CALIBRATION_DATA_H

#include <Arduino.h>
#include "sensor_types.h"
#include "../config.h"

// ===== STANDARD BIAS RESISTOR POSITIONS =====
// Fixed hardware positions on the preOBD PCB. Use these instead of raw values
// so the intent is clear and a single change updates all calibration structs.
#define BIAS_LOW_Z    100.0f   // Low-impedance senders (VDO, Smiths, SW, pre-EFI Ford/GM): 10–500Ω
#define BIAS_HIGH_Z  2490.0f   // High-impedance NTC senders (Jeep XJ, GM EFI, Bosch NTC): 135–9400Ω

// ===== CALIBRATION DATA BY MANUFACTURER =====
// Include modular calibration files organized by manufacturer

#include "sensor_calibration_data/vdo/vdo_calibrations.h"
#include "sensor_calibration_data/aem/aem_calibrations.h"
#include "sensor_calibration_data/nxp/nxp_calibrations.h"
#include "sensor_calibration_data/generic/generic_calibrations.h"
#include "sensor_calibration_data/system/system_calibrations.h"
#include "sensor_calibration_data/jeep/jeep_calibrations.h"
#include "sensor_calibration_data/smiths/smiths_calibrations.h"
#include "sensor_calibration_data/stewart_warner/sw_calibrations.h"
#include "sensor_calibration_data/acdelco/acdelco_calibrations.h"
#include "sensor_calibration_data/bosch/bosch_calibrations.h"

#endif // SENSOR_CALIBRATION_DATA_H
