/*
 * w_phase.cpp - W-Phase Alternator RPM Sensing
 *
 * Implements engine RPM measurement using the W-phase output from an alternator.
 * Uses interrupt-based pulse counting to calculate RPM based on alternator
 * characteristics (poles, pulley ratio).
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../../../lib/sensor_library.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"

// ===== GLOBAL VARIABLES FOR RPM CALCULATION =====
// These must be global to be accessed by the ISR

volatile unsigned long rpm_pulse_count = 0;
volatile unsigned long rpm_last_time = 0;
volatile unsigned long rpm_pulse_interval = 0;
unsigned long rpm_calc_time = 0;

// ===== INTERRUPT SERVICE ROUTINE =====

/**
 * RPM pulse interrupt service routine
 *
 * Called on each rising edge of the W-phase signal.
 * Measures time between pulses and applies debouncing.
 */
void rpmPulseISR() {
    unsigned long now = micros();
    unsigned long interval = now - rpm_last_time;

    // Debounce: ignore pulses faster than 100 µs (600,000 RPM equivalent)
    if (interval > 100) {
        rpm_pulse_interval = interval;
        rpm_pulse_count++;
        rpm_last_time = now;
    }
}

// ===== INITIALIZATION =====

/**
 * Initialize W-Phase RPM sensing
 *
 * Sets up interrupt-based pulse counting on the specified pin.
 *
 * @param ptr  Pointer to Input structure containing pin configuration
 */
void initWPhaseRPM(Input* ptr) {
    pinMode(ptr->pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(ptr->pin), rpmPulseISR, RISING);
    msg.debug.info(TAG_SENSOR, "RPM sensing on pin %d for %s", ptr->pin, ptr->abbrName);
}

// ===== READING =====

/**
 * Read W-Phase RPM
 *
 * Calculates engine RPM from alternator W-phase pulse timing.
 * Accounts for alternator poles and pulley ratio.
 *
 * @param ptr  Pointer to Input structure to store RPM reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - poles, pulley ratio, calibration multiplier
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - 12-pole alternator, 3:1 pulley ratio
 *
 * Formula: Engine_RPM = (60,000,000 / (interval × pulses_per_rev × pulley_ratio)) × calibration_mult
 */
void readWPhaseRPM(Input *ptr) {
    // Get calibration parameters
    byte poles;
    float pulley_ratio;
    float calibration_mult;
    uint16_t timeout_ms;
    uint16_t min_rpm;
    uint16_t max_rpm;

    if (ptr->flags.useCustomCalibration) {
        // Use custom calibration from Input union
        poles = ptr->customCalibration.rpm.poles;
        pulley_ratio = ptr->customCalibration.rpm.pulley_ratio;
        calibration_mult = ptr->customCalibration.rpm.calibration_mult;
        timeout_ms = ptr->customCalibration.rpm.timeout_ms;
        min_rpm = ptr->customCalibration.rpm.min_rpm;
        max_rpm = ptr->customCalibration.rpm.max_rpm;
    } else {
        // Use preset calibration from sensor library
        const SensorInfo* sensorInfo = getSensorByIndex(ptr->sensorIndex);
        if (sensorInfo && sensorInfo->defaultCalibration) {
            const RPMCalibration* cal = (const RPMCalibration*)sensorInfo->defaultCalibration;
            poles = cal->poles;
            pulley_ratio = cal->pulley_ratio;
            calibration_mult = cal->calibration_mult;
            timeout_ms = cal->timeout_ms;
            min_rpm = cal->min_rpm;
            max_rpm = cal->max_rpm;
        } else {
            // Fallback defaults (12-pole, 3:1 ratio)
            poles = 12;             // Most common automotive alternator
            pulley_ratio = 3.0;     // Typical automotive ratio
            calibration_mult = 1.0; // No adjustment
            timeout_ms = 2000;
            min_rpm = 100;
            max_rpm = 10000;
        }
    }

    // Calculate pulses per alternator revolution from poles
    // A 12-pole alternator produces 6 pulses per revolution (poles/2)
    float pulses_per_rev = (float)poles / 2.0;

    // Calculate base calibration factor (pulses per engine revolution)
    // Accounts for both alternator pulses and pulley ratio
    float calibration_factor = pulses_per_rev * pulley_ratio;

    // Calculate time since last pulse
    unsigned long now = millis();
    unsigned long time_since_pulse = now - (rpm_last_time / 1000);

    // Check for timeout (engine stopped)
    if (time_since_pulse > timeout_ms) {
        ptr->value = 0;
        return;
    }

    // Calculate ENGINE RPM from pulse interval
    // Formula: Engine_RPM = (60,000,000 / (interval × pulses_per_rev × pulley_ratio)) × calibration_mult
    if (rpm_pulse_interval > 0) {
        float engine_rpm = (60000000.0 / (rpm_pulse_interval * calibration_factor)) * calibration_mult;

        // Validate range
        if (engine_rpm >= min_rpm && engine_rpm <= max_rpm) {
            // Apply simple smoothing filter
            if (!isnan(ptr->value) && ptr->value > 0) {
                ptr->value = (ptr->value * 0.8) + (engine_rpm * 0.2);
            } else {
                ptr->value = engine_rpm;
            }
        } else {
            ptr->value = NAN;  // Out of range
        }
    }
}
