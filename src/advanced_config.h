/*
 * advanced_config.h - Advanced Configuration for Custom Sensors
 *
 * This file provides examples and helper macros for adding custom sensors
 * that aren't part of the standard sensor library.
 *
 * USAGE:
 * 1. Uncomment the examples below or copy them to create your custom sensor
 * 2. Modify the calibration values to match your sensor
 * 3. In config.h, enable your sensor and set its type
 * 4. The calibration will automatically be used instead of the preset
 */

#ifndef ADVANCED_CONFIG_H
#define ADVANCED_CONFIG_H

#include "config.h"  // For VDO_BIAS_RESISTOR and other global defines

// =============================================================================
// HELPER MACROS FOR CUSTOM SENSORS
// =============================================================================

// Define a custom thermistor using Steinhart-Hart equation
// Usage: DEFINE_CUSTOM_THERMISTOR(SENSOR_NAME, bias_r, a, b, c)
#define DEFINE_CUSTOM_THERMISTOR(name, bias, a, b, c) \
    static ThermistorSteinhartCalibration name##_custom_cal = { \
        .bias_resistor = bias, \
        .steinhart_a = a, \
        .steinhart_b = b, \
        .steinhart_c = c \
    };

// Define a custom pressure sensor using linear calibration
// Usage: DEFINE_CUSTOM_PRESSURE_LINEAR(SENSOR_NAME, v_min, v_max, p_min, p_max)
#define DEFINE_CUSTOM_PRESSURE_LINEAR(name, vmin, vmax, pmin, pmax) \
    static PressureLinearCalibration name##_custom_cal = { \
        .voltage_min = vmin, \
        .voltage_max = vmax, \
        .pressure_min = pmin, \
        .pressure_max = pmax \
    };

// Define a custom pressure sensor using polynomial calibration (VDO style)
// Usage: DEFINE_CUSTOM_PRESSURE_POLY(SENSOR_NAME, bias_r, poly_a, poly_b, poly_c)
#define DEFINE_CUSTOM_PRESSURE_POLY(name, bias, a, b, c) \
    static PressurePolynomialCalibration name##_custom_cal = { \
        .bias_resistor = bias, \
        .poly_a = a, \
        .poly_b = b, \
        .poly_c = c \
    };

// Define a custom RPM calibration
// Usage: DEFINE_CUSTOM_RPM(SENSOR_NAME, poles, timeout, min, max)
#define DEFINE_CUSTOM_RPM(name, num_poles, timeout, min_rpm, max_rpm) \
    static RPMCalibration name##_custom_cal = { \
        .poles = num_poles, \
        .pulses_per_rev = (float)num_poles / 2.0, \
        .timeout_ms = timeout, \
        .min_rpm = min_rpm, \
        .max_rpm = max_rpm \
    };

// =============================================================================
// EXAMPLE 1: Custom Thermistor Calibration
// =============================================================================
// Uncomment and modify to use your own thermistor calibration

/*
// Example: Custom coolant sensor with 470Ω bias resistor
#define COOLANT_CUSTOM_CALIBRATION
#ifdef COOLANT_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_THERMISTOR(coolant,
        470.0,          // bias_resistor
        1.299e-3,       // steinhart_a
        2.401e-4,       // steinhart_b
        1.301e-7        // steinhart_c
    )
#endif
*/

// =============================================================================
// EXAMPLE 2: Adding a Second Coolant Temperature Sensor
// =============================================================================
// This shows how to add a completely new sensor (not just custom calibration)

/*
// Step 1: Define the sensor in config.h (uncomment these lines there):
// #define ENABLE_COOLANT_TEMP_2
// #define COOLANT_TEMP_2_SENSOR_TYPE  VDO_120C_STEINHART  // Or use custom
// #define COOLANT_TEMP_2_INPUT        A5
// #define COOLANT_TEMP_2_MIN          -1
// #define COOLANT_TEMP_2_MAX          100

// Step 2: (Optional) Define custom calibration here:
#ifdef ENABLE_COOLANT_TEMP_2
    // Option A: Use a preset (VDO_120C_STEINHART) - no code needed!

    // Option B: Use custom calibration - uncomment below:
    // #define COOLANT_TEMP_2_CUSTOM_CALIBRATION
    // #ifdef COOLANT_TEMP_2_CUSTOM_CALIBRATION
    //     DEFINE_CUSTOM_THERMISTOR(coolant_temp_2,
    //         2200.0,         // bias_resistor
    //         1.764e-03,      // steinhart_a
    //         2.499e-04,      // steinhart_b
    //         6.773e-08       // steinhart_c
    //     )
    // #endif
#endif

// Step 3: Add sensor definition to sensors.cpp (see docs/guides/configuration/ADDING_SENSORS.md)
*/

// =============================================================================
// EXAMPLE 3: Custom Pressure Sensor
// =============================================================================

/*
// Custom boost pressure sensor (linear, 0.5-4.5V, 0-3 bar)
#define BOOST_CUSTOM_CALIBRATION
#ifdef BOOST_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_PRESSURE_LINEAR(boost,
        0.5,    // voltage_min
        4.5,    // voltage_max
        0.0,    // pressure_min (bar)
        3.0     // pressure_max (bar)
    )
#endif
*/

// =============================================================================
// EXAMPLE 4: Custom Oil Pressure Sensor (VDO Polynomial)
// =============================================================================

/*
// Custom VDO oil pressure sensor with different polynomial
#define OIL_PRESSURE_CUSTOM_CALIBRATION
#ifdef OIL_PRESSURE_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_PRESSURE_POLY(oil_pressure,
        VDO_BIAS_RESISTOR,  // bias_resistor (uses global default)
        -0.3682,            // poly_a
        36.465,             // poly_b
        10.648              // poly_c
    )
#endif
*/

// =============================================================================
// EXAMPLE 5: Custom RPM Sensor (18-pole alternator)
// =============================================================================

/*
#define RPM_CUSTOM_CALIBRATION
#ifdef RPM_CUSTOM_CALIBRATION
    DEFINE_CUSTOM_RPM(rpm,
        18,      // poles
        2000,    // timeout_ms
        300,     // min_rpm
        8000     // max_rpm
    )
#endif
*/

// =============================================================================
// EXAMPLE 6: Custom Calibration in Compile-Time Config Mode
// =============================================================================

/*
USING CUSTOM CALIBRATIONS WITH sensors_config.h
------------------------------------------------
When using compile-time configuration (USE_STATIC_CONFIG), you can override
the default calibration for any sensor.

STEP 1: Define your sensor in sensors_config.h
-----------------------------------------------
   // Input 5: Oil Temperature with Custom Thermistor
   #define INPUT_5_PIN         A7
   #define INPUT_5_APPLICATION OIL_TEMP
   #define INPUT_5_SENSOR      VDO_150C_STEINHART  // Uses default calibration

STEP 2: Add custom calibration in advanced_config.h (this file)
----------------------------------------------------------------
   #define INPUT_5_CUSTOM_CALIBRATION
   #ifdef INPUT_5_CUSTOM_CALIBRATION
       DEFINE_CUSTOM_THERMISTOR(input_5,
           VDO_BIAS_RESISTOR,  // bias_resistor (uses global default)
           1.764e-03,          // steinhart_a
           2.499e-04,          // steinhart_b
           6.773e-08           // steinhart_c
       )
   #endif

That's it! The custom calibration will automatically override the preset.

CALIBRATION TYPES SUPPORTED:
-----------------------------
- DEFINE_CUSTOM_THERMISTOR(name, bias_r, a, b, c)
  For thermistor sensors using Steinhart-Hart equation

- DEFINE_CUSTOM_PRESSURE_LINEAR(name, v_min, v_max, p_min, p_max)
  For linear voltage-to-pressure sensors

- DEFINE_CUSTOM_PRESSURE_POLY(name, bias_r, poly_a, poly_b, poly_c)
  For VDO-style polynomial pressure sensors

- DEFINE_CUSTOM_RPM(name, poles, timeout, min_rpm, max_rpm)
  For RPM sensors (alternator, hall effect, etc.)

NAMING CONVENTION:
------------------
The calibration struct name MUST be: input_N_custom_cal
where N matches your INPUT_N_PIN number.

Examples:
  INPUT_0_CUSTOM_CALIBRATION → input_0_custom_cal
  INPUT_5_CUSTOM_CALIBRATION → input_5_custom_cal

NOTE: Custom calibrations are currently only supported in compile-time config
mode (USE_STATIC_CONFIG). Runtime serial configuration uses preset calibrations
from sensor_library.h.
*/

// =============================================================================
// YOUR CUSTOM SENSORS BELOW
// =============================================================================
// Add your custom sensor definitions here

// Example: Uncomment and modify for your needs
// #define MY_SENSOR_CUSTOM_CALIBRATION
// #ifdef MY_SENSOR_CUSTOM_CALIBRATION
//     DEFINE_CUSTOM_THERMISTOR(my_sensor,
//         10000.0,    // bias_resistor
//         1.129e-3,   // steinhart_a
//         2.341e-4,   // steinhart_b
//         8.775e-8    // steinhart_c
//     )
// #endif

// Test custom calibration for Input 3 (Oil Temperature)
// #define INPUT_3_CUSTOM_CALIBRATION
// #ifdef INPUT_3_CUSTOM_CALIBRATION
//     DEFINE_CUSTOM_THERMISTOR(input_3,
//         1000.0,      // Custom bias resistor (different from default)
//         1.125e-03,   // steinhart_a
//         2.347e-04,   // steinhart_b
//         8.566e-08    // steinhart_c
//     )
// #endif

#endif // ADVANCED_CONFIG_H
