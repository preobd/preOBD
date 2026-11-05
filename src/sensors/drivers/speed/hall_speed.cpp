/*
 * hall_speed.cpp - Hall Effect Vehicle Speed Sensing
 *
 * Implements vehicle speed measurement using hall effect sensors.
 * Uses interrupt-based pulse counting to calculate speed based on
 * tire circumference, pulses per revolution, and drive ratio.
 */

#include "../../../config.h"
#include "../../../core/platform.h"
#include "../../../inputs/input.h"
#include "../../calibration_types.h"
#include "../../../core/message_api.h"
#include "../../../core/log_tags.h"
#include "hall_speed_debounce.h"

// ===== GLOBAL VARIABLES FOR SPEED CALCULATION =====
// These must be global to be accessed by the ISR

volatile unsigned long speed_pulse_count = 0;
volatile unsigned long speed_last_time = 0;
volatile unsigned long speed_pulse_interval = 0;

// Edge-rejection threshold (µs) the ISR compares against (#577). Derived from
// the calibration outside the ISR by updateDebounce() -- see
// hall_speed_debounce.h -- so the ISR stays integer-only. Starts at the noise
// floor until initHallSpeed()/readHallSpeed() compute the real value.
volatile unsigned long speed_debounce_us = HALL_SPEED_DEBOUNCE_FLOOR_US;

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

    // Debounce: ignore an edge sooner than speed_debounce_us after the last
    // accepted one. The threshold is half the pulse interval at the
    // calibration's max_speed_kph (floor 20 µs), so every legitimate rate is
    // followed and a rate above max speed still reads as out of range (#577).
    // A fixed 500 µs here used to cap the input at 2 kHz (38.6 km/h with the
    // preset calibration) and silently drop every 2nd/3rd edge above it.
    if (interval >= speed_debounce_us) {
        speed_pulse_interval = interval;
        speed_pulse_count++;
        speed_last_time = now;
    }
}

// ===== CALIBRATION =====

typedef struct {
    uint8_t pulses_per_rev;
    uint16_t tire_circumference_mm;
    float final_drive_ratio;
    float calibration_mult;
    uint16_t timeout_ms;
    uint16_t max_speed_kph;
} HallSpeedParams;

/**
 * Resolve the calibration in effect for this input.
 *
 * Calibration sources (in priority order):
 * 1. Custom calibration (RAM) - all parameters
 * 2. Preset calibration (PROGMEM) - from sensor library
 * 3. Default fallback - generic configuration
 */
static void resolveSpeedParams(const Input* ptr, HallSpeedParams& p) {
    if (ptr->flags.useCustomCalibration) {
        // Use custom calibration from Input union
        p.pulses_per_rev = ptr->customCalibration.speed.pulses_per_rev;
        p.tire_circumference_mm = ptr->customCalibration.speed.tire_circumference_mm;
        p.final_drive_ratio = ptr->customCalibration.speed.final_drive_ratio;
        p.calibration_mult = ptr->customCalibration.speed.calibration_mult;
        p.timeout_ms = ptr->customCalibration.speed.timeout_ms;
        p.max_speed_kph = ptr->customCalibration.speed.max_speed_kph;
        return;
    }
    // Use preset calibration. It lives in PROGMEM, so copy it to RAM before
    // touching a field: a direct dereference reads garbage on AVR, which would
    // now also feed the ISR's debounce threshold.
    if (ptr->presetCalibration) {
        SpeedCalibration cal;
        memcpy_P(&cal, ptr->presetCalibration, sizeof(cal));
        p.pulses_per_rev = cal.pulses_per_rev;
        p.tire_circumference_mm = cal.tire_circumference_mm;
        p.final_drive_ratio = cal.final_drive_ratio;
        p.calibration_mult = cal.calibration_mult;
        p.timeout_ms = cal.timeout_ms;
        p.max_speed_kph = cal.max_speed_kph;
    } else {
        // Fallback defaults (conservative generic values)
        p.pulses_per_rev = 100;           // Common gear tooth count
        p.tire_circumference_mm = 2000;   // ~205/55R16 tire
        p.final_drive_ratio = 3.73;       // Common diff ratio
        p.calibration_mult = 1.0;
        p.timeout_ms = 2000;
        p.max_speed_kph = 300;
    }
}

/**
 * Publish the ISR's edge-rejection threshold for this calibration (#577).
 *
 * Called from initHallSpeed() and from every readHallSpeed(), which resolves
 * the calibration anyway. That covers every way the calibration can change
 * (preset vs custom, SET <pin> CAL SPEED, CLEAR <pin> CALIBRATION, JSON
 * import, EEPROM load) without hooking each path: the new threshold is live
 * within one read interval. The write only happens when the value changes.
 */
static void updateDebounce(const HallSpeedParams& p) {
    unsigned long us = hallSpeedDebounceUs(p.pulses_per_rev, p.tire_circumference_mm,
                                           p.final_drive_ratio, p.calibration_mult,
                                           p.max_speed_kph);
    // Only the main loop writes it, so this unguarded read can't tear.
    if (us == speed_debounce_us) return;
    // 32-bit writes are not atomic on AVR; ARM is fine without guards
    #ifdef __AVR__
    noInterrupts();
    #endif
    speed_debounce_us = us;
    #ifdef __AVR__
    interrupts();
    #endif
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
    HallSpeedParams p;
    resolveSpeedParams(ptr, p);
    updateDebounce(p);
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
 * Calibration is resolved by resolveSpeedParams().
 *
 * @param ptr  Pointer to Input structure to store speed reading
 *
 * Formula:
 * freq_hz = 1000000.0 / pulse_interval_us
 * revolutions_per_second = freq_hz / pulses_per_rev
 * wheel_speed_m_per_s = revolutions_per_second * (tire_circumference_mm / 1000.0) / final_drive_ratio
 * speed_kph = wheel_speed_m_per_s * 3.6
 */
void readHallSpeed(Input *ptr) {
    HallSpeedParams p;
    resolveSpeedParams(ptr, p);
    updateDebounce(p);
    const uint8_t pulses_per_rev = p.pulses_per_rev;
    const uint16_t tire_circumference_mm = p.tire_circumference_mm;
    const float final_drive_ratio = p.final_drive_ratio;
    const float calibration_mult = p.calibration_mult;
    const uint16_t timeout_ms = p.timeout_ms;
    const uint16_t max_speed_kph = p.max_speed_kph;

    // Atomic snapshot — 32-bit reads are not atomic on AVR; ARM is fine without guards
    #ifdef __AVR__
    noInterrupts();
    #endif
    unsigned long interval = speed_pulse_interval;
    unsigned long lastTime = speed_last_time;
    #ifdef __AVR__
    interrupts();
    #endif

    // Timeout check using micros() consistently with the ISR (#275). The ISR
    // stamps speed_last_time on the SAME micros() timebase, so compare in micros
    // and snapshot lastTime BEFORE reading the clock: micros() is then always
    // >= lastTime, so the unsigned diff can't underflow and is rollover-safe.
    // (The old code cached millis() first, then read the ISR-written micros()
    // value /1000 -- a just-fired ISR could make the subtrahend exceed `now`,
    // underflowing to ~UINT32_MAX and forcing a spurious vehicle-stopped read;
    // mixing the millis/micros timebases also glitched at the ~71-min micros
    // rollover.)
    if (micros() - lastTime > (unsigned long)timeout_ms * 1000UL) {
        ptr->value = 0.0;
        return;
    }

    // Calculate vehicle speed from pulse interval
    if (interval > 0) {
        // Convert pulse interval (µs) to frequency (Hz)
        float freq_hz = 1000000.0 / interval;

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
