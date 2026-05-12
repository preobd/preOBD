/*
 * bosch_calibrations.h - Bosch NTC Sensor Calibration Data
 *
 * Calibration data for Bosch NTC temperature sensors used as universal EFI
 * coolant/intake air temp sensors across many manufacturers.
 *
 * Calibration uses Steinhart-Hart coefficients fitted to the official Bosch
 * 0 280 130 026 R-vs-T table (18 points, -40 to 130°C). Residuals verified
 * against all table points: <0.2°C across 0–100°C, <0.6°C at extremes.
 *
 * Bosch NTC M12 is high-impedance — use SENSOR_BIAS_HIGH_Z (2.49kΩ position).
 */

#ifndef BOSCH_CALIBRATIONS_H
#define BOSCH_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../sensor_calibration_data.h"

// ===== BOSCH NTC M12 TEMPERATURE SENSOR =====
// Bosch NTC M12 (0 280 130 026 and compatible aftermarket equivalents)
// Universal EFI coolant/intake air temp sensor, M12×1.5 thread
// Cross-applications: Volvo B20/B21/B23, Audi/VW (early 80s), BMW E30/E28,
//   Mercedes W123/W124, and many Haltech/MegaSquirt EFI installs
// Electrical: 2-wire NTC, 2500Ω @ 20°C (nominal), Bosch Jetronic 2-pin connector
// Calibration type: CAL_THERMISTOR_STEINHART (Steinhart-Hart 3-coefficient)
// S-H coefficients fitted to official Bosch 0 280 130 026 R-vs-T table using
//   anchor points: 0°C/5896Ω, 40°C/1175Ω, 100°C/187Ω. Verified against
//   all 18 table points; max residual <0.2°C (0–100°C), <0.6°C at extremes.
// NOTE: Requires SENSOR_BIAS_HIGH_Z (2.49kΩ position). Using the 100Ω VDO position
//       will give badly compressed cold readings. Haltech uses 1kΩ pull-up
//       internally — on preOBD hardware use the 2.49kΩ hardware position instead.
static const PROGMEM ThermistorSteinhartCalibration bosch_ntc_m12_cal = {
    .bias_resistor  = SENSOR_BIAS_HIGH_Z,
    .steinhart_a    = 1.285173e-03f,
    .steinhart_b    = 2.625300e-04f,
    .steinhart_c    = 1.468600e-07f
};

#endif // BOSCH_CALIBRATIONS_H
