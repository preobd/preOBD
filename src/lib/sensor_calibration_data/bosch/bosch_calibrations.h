/*
 * bosch_calibrations.h - Bosch NTC Sensor Calibration Data
 *
 * Calibration data for Bosch NTC temperature sensors used as universal EFI
 * coolant/intake air temp sensors across many manufacturers.
 *
 * Data source caveat: Bosch datasheets publish R-vs-T tables per part number,
 * NOT a single β coefficient. The β value used here (3750K) is a commonly
 * cited aftermarket figure (Haltech/MegaSquirt wiki, Volvo enthusiast forums)
 * derived from approximately the 25°C/85°C datasheet points. This has NOT
 * been verified against an actual Bosch datasheet in this codebase — a
 * bench-verified lookup table from the official R-vs-T data would be more
 * accurate, especially at temperature extremes. See issue #141.
 *
 * Bosch NTC M12 is high-impedance — use SENSOR_BIAS_HIGH_Z (2.49kΩ position).
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
// Electrical: 2-wire NTC, 2057Ω @ 25°C, β ≈ 3750K
// Calibration type: CAL_THERMISTOR_BETA (β-equation approximation)
// β source: commonly cited aftermarket figure (Haltech/MegaSquirt wiki),
//   approximately β₂₅/₈₅ derived from the Bosch datasheet R-vs-T table.
//   NOT verified against the actual Bosch datasheet in this codebase.
// Accuracy: estimated ~±2°C across 0–100°C; degrades to ±5°C at extremes.
//   For precision applications, a bench-verified R-vs-T table is preferable.
// NOTE: Requires SENSOR_BIAS_HIGH_Z (2.49kΩ position). Using the 100Ω VDO position
//       will give badly compressed cold readings. Haltech uses 1kΩ pull-up
//       internally — on preOBD hardware use the 2.49kΩ hardware position instead.
static const PROGMEM BetaCalibration bosch_ntc_m12_cal = {
    .bias_resistor = SENSOR_BIAS_HIGH_Z,
    .beta = 3750.0f,   // β₂₅/₈₅ coefficient (K)
    .r0 = 2057.0f,     // Reference resistance at T0 (Ω)
    .t0 = 25.0f        // Reference temperature (°C)
};

#endif // BOSCH_CALIBRATIONS_H
