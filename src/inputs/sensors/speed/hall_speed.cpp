/*
 * hall_speed.cpp - Hall Effect Vehicle Speed Sensing
 *
 * Implements vehicle speed measurement using hall effect sensors.
 * Uses interrupt-based pulse counting to calculate speed based on
 * tire circumference, pulses per revolution, and drive ratio.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "../../../lib/sensor_library.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"

// ===== GLOBAL VARIABLES FOR SPEED CALCULATION =====
// These must be global to be accessed by the ISR

volatile unsigned long speed_pulse_count = 0;
volatile unsigned long speed_last_time = 0;
volatile unsigned long speed_pulse_interval = 0;

// ===== INTERRUPT SERVICE ROUTINE =====

/**
 * Speed pulse interrupt service routine
 *
 * Called on each rising edge of the hall effect sensor signal.
 * Measures time between pulses and applies debouncing.
 */
void speedPulseISR() {
    unsigned long now = micros();
    unsigned long interval = now - speed_last_time;

    // Debounce: ignore pulses faster than 500 µs (prevents noise at high speed)
    // At 300 km/h with 100 pulses/rev on 2000mm circumference:
    // freq = 300000/(3600*2.0) * 100 = 4166 Hz, period = 240 µs
    // So 500 µs debounce allows up to ~300 km/h safely
    if (interval > 500) {
        speed_pulse_interval = interval;
        speed_pulse_count++;
        speed_last_time = now;
    }
}

// ===== INITIALIZATION =====

/**
 * Initialize Hall Effect Speed sensing
 *
 * Sets up interrupt-based pulse counting on the specified pin.
 *
 * @param ptr  Pointer to Input structure containing pin configuration
 */
void initHallSpeed(Input* ptr) {
    pinMode(ptr->pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(ptr->pin), speedPulseISR, RISING);
    msg.debug.info(TAG_SENSOR, "Speed sensing on pin %d for %s", ptr->pin, ptr->abbrName);
}

// ===== READING =====

/**
 * Read Hall Effect Speed Sensor
 *
 * Calculates vehicle speed from pulse timing.
 * Accounts for tire circumference, pulses per revolution, and drive ratio.
 *
 * @param ptr  Pointer to Input structure to store speed reading
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - all parameters
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - generic configuration
 *
 * Formula:
 * freq_hz = 1000000.0 / pulse_interval_us
 * revolutions_per_second = freq_hz / pulses_per_rev
 * wheel_speed_m_per_s = revolutions_per_second * (tire_circumference_mm / 1000.0) / final_drive_ratio
 * speed_kph = wheel_speed_m_per_s * 3.6
 */
void readHallSpeed(Input *ptr) {
    // Get calibration parameters
    uint8_t pulses_per_rev;
    uint16_t tire_circumference_mm;
    float final_drive_ratio;
    float calibration_mult;
    uint16_t timeout_ms;
    uint16_t max_speed_kph;

    if (ptr->flags.useCustomCalibration) {
        // Use custom calibration from Input union
        pulses_per_rev = ptr->customCalibration.speed.pulses_per_rev;
        tire_circumference_mm = ptr->customCalibration.speed.tire_circumference_mm;
        final_drive_ratio = ptr->customCalibration.speed.final_drive_ratio;
        calibration_mult = ptr->customCalibration.speed.calibration_mult;
        timeout_ms = ptr->customCalibration.speed.timeout_ms;
        max_speed_kph = ptr->customCalibration.speed.max_speed_kph;
    } else {
        // Use preset calibration from sensor library
        const SensorInfo* sensorInfo = getSensorByIndex(ptr->sensorIndex);
        if (sensorInfo && sensorInfo->defaultCalibration) {
            const SpeedCalibration* cal = (const SpeedCalibration*)sensorInfo->defaultCalibration;
            pulses_per_rev = cal->pulses_per_rev;
            tire_circumference_mm = cal->tire_circumference_mm;
            final_drive_ratio = cal->final_drive_ratio;
            calibration_mult = cal->calibration_mult;
            timeout_ms = cal->timeout_ms;
            max_speed_kph = cal->max_speed_kph;
        } else {
            // Fallback defaults (conservative generic values)
            pulses_per_rev = 100;           // Common gear tooth count
            tire_circumference_mm = 2000;   // ~205/55R16 tire
            final_drive_ratio = 3.73;       // Common diff ratio
            calibration_mult = 1.0;
            timeout_ms = 2000;
            max_speed_kph = 300;
        }
    }

    // Calculate time since last pulse
    unsigned long now = millis();
    unsigned long time_since_pulse = now - (speed_last_time / 1000);

    // Check for timeout (vehicle stopped)
    if (time_since_pulse > timeout_ms) {
        ptr->value = 0.0;
        return;
    }

    // Calculate vehicle speed from pulse interval
    if (speed_pulse_interval > 0) {
        // Convert pulse interval (µs) to frequency (Hz)
        float freq_hz = 1000000.0 / speed_pulse_interval;

        // Calculate wheel revolutions per second
        float revolutions_per_second = freq_hz / pulses_per_rev;

        // Calculate wheel speed in m/s
        // Divide by final_drive_ratio because sensor is on transmission/diff output
        float wheel_speed_m_per_s = revolutions_per_second *
                                    (tire_circumference_mm / 1000.0) /
                                    final_drive_ratio;

        // Convert to km/h
        float speed_kph = wheel_speed_m_per_s * 3.6 * calibration_mult;

        // Validate range
        if (speed_kph >= 0 && speed_kph <= max_speed_kph) {
            // Apply simple smoothing filter (exponential moving average)
            if (!isnan(ptr->value) && ptr->value > 0) {
                ptr->value = (ptr->value * 0.7) + (speed_kph * 0.3);
            } else {
                ptr->value = speed_kph;
            }
        } else {
            ptr->value = NAN;  // Out of range
        }
    }
}
