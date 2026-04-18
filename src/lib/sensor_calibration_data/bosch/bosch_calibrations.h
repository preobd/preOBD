/*
 * bosch_calibrations.h - Bosch NTC Sensor Calibration Data
 *
 * Calibration data for Bosch NTC temperature sensors used as universal EFI
 * coolant/air temp sensors across many manufacturers.
 *
 * Data source: Bosch published NTC characteristic curve (2057Ω @ 25°C type).
 * This is the industry-standard sensor used by Volvo, Audi, VW, BMW, Mercedes
 * from the early 1980s onward, and recommended by Haltech for their ECU inputs.
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
// Electrical: 2-wire NTC, 2057Ω @ 25°C characteristic
// Valid range: -40 to 130°C (18 points)
// Source: Bosch published NTC characteristic table (2057Ω @ 25°C type)
// NOTE: Requires BIAS_HIGH_Z (2.49kΩ position). Using the 100Ω VDO position
//       will give badly compressed cold readings. Haltech uses 1kΩ pull-up
//       internally — on preOBD hardware use the 2.49kΩ hardware position instead.
// Bias: BIAS_HIGH_Z (2.49kΩ) — sensor range ~100–28000Ω across -40 to 130°C
static const float bosch_ntc_m12_resistance[] PROGMEM = {
    28680.0, 16180.0,  9420.0,  5670.0,  3520.0,  2240.0,
     2057.0,  1460.0,   973.0,   667.0,   467.0,   336.0,
      247.0,   185.0,   141.0,   109.0,    85.0,    67.0
};
static const float bosch_ntc_m12_temperature[] PROGMEM = {
    -40.0, -30.0, -20.0, -10.0,   0.0,  10.0,
     15.0,  20.0,  30.0,  40.0,  50.0,  60.0,
     70.0,  80.0,  90.0, 100.0, 110.0, 120.0
};

static const PROGMEM ThermistorLookupCalibration bosch_ntc_m12_cal = {
    .bias_resistor = BIAS_HIGH_Z,
    .resistance_table = bosch_ntc_m12_resistance,
    .temperature_table = bosch_ntc_m12_temperature,
    .table_size = 18
};

#endif // BOSCH_CALIBRATIONS_H
