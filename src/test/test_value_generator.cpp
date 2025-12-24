/*
 * test_value_generator.cpp - Dynamic Test Value Generation
 *
 * Generates time-based test values for simulating sensor behavior:
 * - Static values
 * - Linear ramps (up/down)
 * - Sinusoidal waves
 * - Square waves
 * - Random walks
 * - NaN (sensor faults)
 */

#include "../config.h"

#ifdef ENABLE_TEST_MODE

#include "test_mode.h"
#include <Arduino.h>

// ===== VALUE GENERATION FUNCTIONS =====

float generateTestValue(const InputTestConfig* config, unsigned long elapsedMs) {
    switch (config->valueType) {
        case TEST_STATIC: {
            // Constant value
            return config->value1;
        }

        case TEST_RAMP_UP: {
            // Linear increase from value1 to value2 over period_ms
            if (config->period_ms == 0) {
                return config->value2;  // Instant
            }

            float progress = (float)elapsedMs / config->period_ms;
            if (progress > 1.0f) {
                progress = 1.0f;  // Clamp at end value
            }

            return config->value1 + (config->value2 - config->value1) * progress;
        }

        case TEST_RAMP_DOWN: {
            // Linear decrease from value2 to value1 over period_ms
            if (config->period_ms == 0) {
                return config->value1;  // Instant
            }

            float progress = (float)elapsedMs / config->period_ms;
            if (progress > 1.0f) {
                progress = 1.0f;  // Clamp at end value
            }

            return config->value2 - (config->value2 - config->value1) * progress;
        }

        case TEST_SINE_WAVE: {
            // Sinusoidal oscillation between value1 and value2
            if (config->period_ms == 0) {
                return (config->value1 + config->value2) / 2.0f;  // Midpoint
            }

            // Calculate phase (0.0 to 1.0) within current period
            unsigned long periodTime = (unsigned long)config->period_ms;
            float phase = (float)(elapsedMs % periodTime) / periodTime;

            // Calculate sine wave
            float amplitude = (config->value2 - config->value1) / 2.0f;
            float midpoint = (config->value1 + config->value2) / 2.0f;

            return midpoint + amplitude * sin(2.0f * PI * phase);
        }

        case TEST_SQUARE_WAVE: {
            // Square wave alternating between value1 and value2
            if (config->period_ms == 0) {
                return config->value1;  // Default to low value
            }

            // Calculate phase (0.0 to 1.0) within current period
            unsigned long periodTime = (unsigned long)config->period_ms;
            float phase = (float)(elapsedMs % periodTime) / periodTime;

            // First half of period = value1, second half = value2
            return (phase < 0.5f) ? config->value1 : config->value2;
        }

        case TEST_RANDOM: {
            // Random walk within bounds [value1, value2]
            // Use a pseudo-random walk that changes gradually
            static float lastValue = -999.0f;  // Sentinel value
            static unsigned long lastUpdateTime = 0;

            // Initialize on first call or if too much time has passed
            if (lastValue == -999.0f || (elapsedMs - lastUpdateTime) > 10000) {
                lastValue = (config->value1 + config->value2) / 2.0f;
                lastUpdateTime = elapsedMs;
            }

            // Update every ~200ms for smooth random walk
            if (elapsedMs - lastUpdateTime >= 200) {
                // Random change: ±1% of range per step
                float range = config->value2 - config->value1;
                float maxChange = range * 0.01f;

                // Generate random change
                float change = ((float)random(-100, 101) / 100.0f) * maxChange;
                lastValue += change;

                // Clamp to bounds
                if (lastValue < config->value1) lastValue = config->value1;
                if (lastValue > config->value2) lastValue = config->value2;

                lastUpdateTime = elapsedMs;
            }

            return lastValue;
        }

        case TEST_NAN: {
            // Always return NaN (sensor fault)
            return NAN;
        }

        default: {
            // Unknown type - return NaN
            return NAN;
        }
    }
}

#endif // ENABLE_TEST_MODE
