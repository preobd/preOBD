/*
 * bosch_calibrations.h - Bosch NTC Sensor Calibration Data
 *
 * Calibration data for Bosch NTC temperature sensors used as universal EFI
 * coolant/intake air temp sensors across many manufacturers.
 *
 * Data source: Bosch published β-equation parameters (R25 and β coefficient).
 * Bosch also publishes a full R-vs-T datasheet table per part number; a
 * bench-verified lookup table would be more accurate than this β fit,
 * especially below 0°C and above 100°C. See issue #141.
 *
 * Bosch NTC M12 is high-impedance — use BIAS_HIGH_Z (2.49kΩ position).
 */

#ifndef BOSCH_CALIBRATIONS_H
#define BOSCH_CALIBRATIONS_H

#include <Arduino.h>
#include "../../sensor_types.h"
#include "../../../config.h"

// ===== BOSCH NTC M12 TEMPERATURE SENSOR =====
// Bosch NTC M12 (0 280 130 026 and related part numbers)
// Universal EFI coolant/intake air temp sensor, M12×1.5 thread
// Cross-applications: Volvo B20/B21/B23, Audi/VW (early 80s), BMW E30/E28,
//   Mercedes W123/W124, and many Haltech/MegaSquirt EFI installs
// Electrical: 2-wire NTC, 2057Ω @ 25°C, β ≈ 3750K (published Bosch parameters)
// Calibration type: CAL_THERMISTOR_BETA (β-equation approximation)
// Accuracy: ~±2°C across 0–100°C; degrades to ±5°C at temperature extremes.
//   For precision applications, a bench-verified R-vs-T table would be better.
// NOTE: Requires BIAS_HIGH_Z (2.49kΩ position). Using the 100Ω VDO position
//       will give badly compressed cold readings. Haltech uses 1kΩ pull-up
//       internally — on preOBD hardware use the 2.49kΩ hardware position instead.
static const PROGMEM BetaCalibration bosch_ntc_m12_cal = {
    .bias_resistor = BIAS_HIGH_Z,
    .beta = 3750.0f,   // β₂₅/₈₅ coefficient (K)
    .r0 = 2057.0f,     // Reference resistance at T0 (Ω)
    .t0 = 25.0f        // Reference temperature (°C)
};

#endif // BOSCH_CALIBRATIONS_H
