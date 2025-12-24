/*
 * test_scenarios.h - Pre-Defined Test Scenarios
 *
 * Contains 5 pre-defined test scenarios for comprehensive output testing:
 * 1. Normal Operation - Typical operating values with realistic variations
 * 2. Alarm Test - Values that trigger alarm conditions
 * 3. Sensor Fault - Simulated sensor failures (NaN values)
 * 4. Engine Startup - Cold start sequence with all sensors ramping
 * 5. Dynamic Driving - Rapid changing values for stress testing
 *
 * All scenarios stored in PROGMEM to minimize RAM usage.
 */

#ifndef TEST_SCENARIOS_H
#define TEST_SCENARIOS_H

#include "../config.h"

#ifdef ENABLE_TEST_MODE

#include "test_mode.h"

// ===== SCENARIO 1: NORMAL OPERATION =====
// Purpose: Verify all outputs work with typical operating values
// Duration: 120 seconds
// Tests: LCD display, CAN frames, data logging, stable values

const PROGMEM InputTestConfig scenario1_configs[] = {
    // Input 0: CHT - 180°C steady
    { .inputIndex = 0, .valueType = TEST_STATIC, .value1 = 180.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 1: EGT - 350°C with slight sinusoidal oscillation (340-360°C, 5s period)
    { .inputIndex = 1, .valueType = TEST_SINE_WAVE, .value1 = 340.0f, .value2 = 360.0f,
      .period_ms = 5000, .forceAlarm = false, .forceNaN = false },

    // Input 2: Coolant - 85°C steady
    { .inputIndex = 2, .valueType = TEST_STATIC, .value1 = 85.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 3: Oil Temp - Warming up from 40°C to 90°C over 60 seconds
    { .inputIndex = 3, .valueType = TEST_RAMP_UP, .value1 = 40.0f, .value2 = 90.0f,
      .period_ms = 60000, .forceAlarm = false, .forceNaN = false },

    // Input 5: Ambient - 22°C steady
    { .inputIndex = 5, .valueType = TEST_STATIC, .value1 = 22.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 6: Barometric - 1.013 bar with slight oscillation (weather changes)
    { .inputIndex = 6, .valueType = TEST_SINE_WAVE, .value1 = 1.010f, .value2 = 1.016f,
      .period_ms = 30000, .forceAlarm = false, .forceNaN = false },

    // Input 7: Humidity - 45% with slow drift
    { .inputIndex = 7, .valueType = TEST_SINE_WAVE, .value1 = 40.0f, .value2 = 50.0f,
      .period_ms = 45000, .forceAlarm = false, .forceNaN = false }
};

const char scenario1_name[] PROGMEM = "Normal Operation";
const PROGMEM TestScenario scenario1_normalOperation = {
    .name = scenario1_name,
    .duration_ms = 120000,  // 2 minutes
    .numInputOverrides = 7,
    .inputConfigs = scenario1_configs
};

// ===== SCENARIO 2: ALARM TEST - OVERHEATING =====
// Purpose: Test alarm buzzer, display warnings, threshold checking
// Duration: 30 seconds
// Tests: Alarm triggering, buzzer activation, LCD warning display

const PROGMEM InputTestConfig scenario2_configs[] = {
    // Input 0: CHT - Rapidly rising to alarm level (180°C → 270°C over 15s)
    { .inputIndex = 0, .valueType = TEST_RAMP_UP, .value1 = 180.0f, .value2 = 270.0f,
      .period_ms = 15000, .forceAlarm = false, .forceNaN = false },

    // Input 1: EGT - Exceeding max threshold (400°C → 650°C over 20s)
    { .inputIndex = 1, .valueType = TEST_RAMP_UP, .value1 = 400.0f, .value2 = 650.0f,
      .period_ms = 20000, .forceAlarm = false, .forceNaN = false },

    // Input 2: Coolant - Also overheating (85°C → 115°C over 12s)
    { .inputIndex = 2, .valueType = TEST_RAMP_UP, .value1 = 85.0f, .value2 = 115.0f,
      .period_ms = 12000, .forceAlarm = false, .forceNaN = false }
};

const char scenario2_name[] PROGMEM = "Alarm Test - Overheating";
const PROGMEM TestScenario scenario2_alarmTest = {
    .name = scenario2_name,
    .duration_ms = 30000,  // 30 seconds
    .numInputOverrides = 3,
    .inputConfigs = scenario2_configs
};

// ===== SCENARIO 3: SENSOR FAULT SIMULATION =====
// Purpose: Test NaN handling, error displays, fault detection
// Duration: 45 seconds
// Tests: Error handling, LCD "ERR" display, CAN fault codes

const PROGMEM InputTestConfig scenario3_configs[] = {
    // Input 0: CHT - Starts normal, then fails (switches to NaN)
    // Note: This will return static value then switch - for true intermittent, use SQUARE_WAVE
    { .inputIndex = 0, .valueType = TEST_STATIC, .value1 = 180.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 1: EGT - Intermittent fault (square wave between 350°C and NaN, 5s period)
    // To simulate intermittent, we'll use square wave with one value being close to max
    // Actual NaN requires forceNaN=true which overrides valueType
    { .inputIndex = 1, .valueType = TEST_NAN, .value1 = 0.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = true },

    // Input 2: Coolant - Normal for contrast
    { .inputIndex = 2, .valueType = TEST_STATIC, .value1 = 85.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 3: Oil Temp - Normal for contrast
    { .inputIndex = 3, .valueType = TEST_STATIC, .value1 = 88.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false }
};

const char scenario3_name[] PROGMEM = "Sensor Fault Simulation";
const PROGMEM TestScenario scenario3_sensorFault = {
    .name = scenario3_name,
    .duration_ms = 45000,  // 45 seconds
    .numInputOverrides = 4,
    .inputConfigs = scenario3_configs
};

// ===== SCENARIO 4: ENGINE STARTUP SEQUENCE =====
// Purpose: Simulate realistic cold start with all sensors changing
// Duration: 120 seconds
// Tests: Ramp behavior, multiple simultaneous changes, realistic profiles

const PROGMEM InputTestConfig scenario4_configs[] = {
    // Input 0: CHT - Cold start to operating temp (20°C → 180°C over 90s)
    { .inputIndex = 0, .valueType = TEST_RAMP_UP, .value1 = 20.0f, .value2 = 180.0f,
      .period_ms = 90000, .forceAlarm = false, .forceNaN = false },

    // Input 1: EGT - Rises faster than CHT (25°C → 350°C over 60s)
    { .inputIndex = 1, .valueType = TEST_RAMP_UP, .value1 = 25.0f, .value2 = 350.0f,
      .period_ms = 60000, .forceAlarm = false, .forceNaN = false },

    // Input 2: Coolant - Gradual warmup (18°C → 85°C over full duration)
    { .inputIndex = 2, .valueType = TEST_RAMP_UP, .value1 = 18.0f, .value2 = 85.0f,
      .period_ms = 120000, .forceAlarm = false, .forceNaN = false },

    // Input 3: Oil Temp - Slower warmup (15°C → 88°C over full duration)
    { .inputIndex = 3, .valueType = TEST_RAMP_UP, .value1 = 15.0f, .value2 = 88.0f,
      .period_ms = 120000, .forceAlarm = false, .forceNaN = false },

    // Input 5: Ambient - Steady (it's cold outside!)
    { .inputIndex = 5, .valueType = TEST_STATIC, .value1 = 8.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false }
};

const char scenario4_name[] PROGMEM = "Engine Startup Sequence";
const PROGMEM TestScenario scenario4_engineStartup = {
    .name = scenario4_name,
    .duration_ms = 120000,  // 2 minutes
    .numInputOverrides = 5,
    .inputConfigs = scenario4_configs
};

// ===== SCENARIO 5: DYNAMIC DRIVING CONDITIONS =====
// Purpose: Test rapid value changes, LCD refresh rate, data logging
// Duration: 180 seconds
// Tests: Fast updates, oscillations, random variations, stress testing

const PROGMEM InputTestConfig scenario5_configs[] = {
    // Input 0: CHT - Oscillating with load changes (170-190°C, 30s period)
    { .inputIndex = 0, .valueType = TEST_SINE_WAVE, .value1 = 170.0f, .value2 = 190.0f,
      .period_ms = 30000, .forceAlarm = false, .forceNaN = false },

    // Input 1: EGT - High variation (300-450°C, 15s period)
    { .inputIndex = 1, .valueType = TEST_SINE_WAVE, .value1 = 300.0f, .value2 = 450.0f,
      .period_ms = 15000, .forceAlarm = false, .forceNaN = false },

    // Input 2: Coolant - Moderate oscillation (82-92°C, 25s period)
    { .inputIndex = 2, .valueType = TEST_SINE_WAVE, .value1 = 82.0f, .value2 = 92.0f,
      .period_ms = 25000, .forceAlarm = false, .forceNaN = false },

    // Input 3: Oil Temp - Slow variation (85-95°C, 40s period)
    { .inputIndex = 3, .valueType = TEST_SINE_WAVE, .value1 = 85.0f, .value2 = 95.0f,
      .period_ms = 40000, .forceAlarm = false, .forceNaN = false },

    // Input 5: Ambient - Steady
    { .inputIndex = 5, .valueType = TEST_STATIC, .value1 = 28.0f, .value2 = 0.0f,
      .period_ms = 0, .forceAlarm = false, .forceNaN = false },

    // Input 6: Barometric - Slight variation (altitude changes)
    { .inputIndex = 6, .valueType = TEST_SINE_WAVE, .value1 = 0.95f, .value2 = 1.02f,
      .period_ms = 60000, .forceAlarm = false, .forceNaN = false }
};

const char scenario5_name[] PROGMEM = "Dynamic Driving Conditions";
const PROGMEM TestScenario scenario5_drivingConditions = {
    .name = scenario5_name,
    .duration_ms = 180000,  // 3 minutes
    .numInputOverrides = 6,
    .inputConfigs = scenario5_configs
};

// ===== SCENARIO REGISTRY =====
// Array of all available test scenarios (stored in PROGMEM)
const TestScenario* const TEST_SCENARIOS[] PROGMEM = {
    &scenario1_normalOperation,
    &scenario2_alarmTest,
    &scenario3_sensorFault,
    &scenario4_engineStartup,
    &scenario5_drivingConditions
};

#define NUM_TEST_SCENARIOS (sizeof(TEST_SCENARIOS) / sizeof(TestScenario*))

// Helper function to get a scenario by index
inline const TestScenario* getTestScenario(uint8_t index) {
    if (index >= NUM_TEST_SCENARIOS) {
        return nullptr;
    }
    return (const TestScenario*)pgm_read_ptr(&TEST_SCENARIOS[index]);
}

#endif // ENABLE_TEST_MODE

#endif // TEST_SCENARIOS_H
