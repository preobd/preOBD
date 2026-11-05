/*
 * hall_speed_debounce.h - HALL_SPEED edge-rejection threshold (#577)
 *
 * Pure helper, no Arduino dependencies, so the native test env can include it.
 *
 * The ISR rejects a rising edge that arrives sooner than this many µs after
 * the last accepted one. The threshold follows the calibration instead of
 * being a fixed constant: a fixed 500 µs capped the input at 2 kHz, which is
 * only 38.6 km/h with the preset calibration (100 ppr, 2000 mm, 3.73), and
 * above it the ISR silently kept every 2nd/3rd edge and reported a fraction
 * of the real speed.
 *
 * Shortest legitimate pulse interval, at max_speed_kph:
 *   speed_kph = (1e6 / interval_us) / ppr * (circ_mm / 1000) / ratio * 3.6 * mult
 *   => interval_min_us = 3600 * circ_mm * mult / (ppr * ratio * max_kph)
 *
 * The threshold is half of that. Rejecting only edges closer than
 * interval_min / 2 keeps that invariant: any pulse rate above max speed
 * still yields an accepted interval shorter than interval_min, so
 * readHallSpeed() reports it as out of range (NAN), never as a lower,
 * plausible-looking speed. (Below 2x max speed nothing is dropped; above
 * it, an accepted interval is < threshold + period < 2 * threshold.)
 *
 * HALL_SPEED_DEBOUNCE_FLOOR_US is the absolute noise floor. It only binds
 * when interval_min / 2 is below it (a calibration whose max speed is above
 * ~25 kHz), where the input couldn't be followed reliably anyway.
 */

#ifndef HALL_SPEED_DEBOUNCE_H
#define HALL_SPEED_DEBOUNCE_H

#include <stdint.h>

#define HALL_SPEED_DEBOUNCE_FLOOR_US 20UL
// Upper clamp: keeps the value meaningful for absurd calibrations (e.g.
// max_speed_kph of 1). Clamping down only rejects fewer edges, so the
// out-of-range invariant above still holds.
#define HALL_SPEED_DEBOUNCE_MAX_US   1000000UL

static inline uint32_t hallSpeedDebounceUs(uint8_t pulses_per_rev,
                                           uint16_t tire_circumference_mm,
                                           float final_drive_ratio,
                                           float calibration_mult,
                                           uint16_t max_speed_kph) {
    // Invalid / unset calibration: fall back to the noise floor only.
    if (pulses_per_rev == 0 || tire_circumference_mm == 0 || max_speed_kph == 0 ||
        !(final_drive_ratio > 0.0f) || !(calibration_mult > 0.0f)) {
        return HALL_SPEED_DEBOUNCE_FLOOR_US;
    }
    float min_interval_us = 3600.0f * (float)tire_circumference_mm * calibration_mult /
                            ((float)pulses_per_rev * final_drive_ratio * (float)max_speed_kph);
    float half = min_interval_us * 0.5f;
    if (!(half > (float)HALL_SPEED_DEBOUNCE_FLOOR_US)) return HALL_SPEED_DEBOUNCE_FLOOR_US;
    if (half >= (float)HALL_SPEED_DEBOUNCE_MAX_US) return HALL_SPEED_DEBOUNCE_MAX_US;
    return (uint32_t)half;  // truncates down: never above interval_min / 2
}

#endif // HALL_SPEED_DEBOUNCE_H
