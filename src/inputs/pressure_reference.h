/*
 * pressure_reference.h - Absolute vs gauge pressure (#639)
 *
 * A pressure is measured against a zero: vacuum (ABSOLUTE: MAP, barometric,
 * an MPX4250AP) or the air around the sensor (GAUGE: boost, oil pressure, a
 * VDO sender, 0 with the engine off). The two differ by the barometric
 * pressure. Each sensor row carries its reference (sensor_types.h); the
 * barometric reference used to convert between them lives here, in the
 * inputs layer, because every reader of a pressure value needs the same one.
 */

#ifndef PRESSURE_REFERENCE_H
#define PRESSURE_REFERENCE_H

#include <stdint.h>

struct Input;

enum PressureReference : uint8_t {
    REF_NONE = 0,    // not a pressure (or the reference comes from elsewhere: a CAN import's PID)
    REF_GAUGE,       // 0 at atmosphere
    REF_ABSOLUTE     // 0 at vacuum
};

#define STD_BARO_BAR 1.01325f   // barometric pressure when no baro input is fitted

/**
 * The barometric reference, in bar: the lowest-pin enabled BAROMETRIC_PRESSURE
 * input (a wired sensor, pins below CAN:0, beats an imported PID 0x33,
 * whatever slots they sit in; #636) whose value is finite and inside
 * 0.5-1.2 bar. While a baro input is configured but none reads in range, the
 * last good reading holds, so a barometer dropout moves no converted value
 * (#639 decision 5A); with no baro input at all, STD_BARO_BAR. Never NaN.
 */
float baroReferenceBar();

/**
 * An application's reference: REF_ABSOLUTE for MAP and barometric pressure,
 * REF_GAUGE for every other pressure application, REF_NONE for NONE and
 * anything that is not a pressure.
 */
PressureReference applicationReference(uint8_t app);

/**
 * What to add (bar) to a pressure in reference `from` to express it in `to`:
 * +baro gauge->absolute, -baro absolute->gauge, 0 when they match or either
 * is REF_NONE. Pure.
 */
float pressureReferenceOffset(PressureReference from, PressureReference to, float baroBar);

/**
 * THE rule (ARCHITECTURE.md §14.8): a pressure in the value table is in its
 * application's reference. Converts `in.value` from the sensor row's
 * reference to the application's, once, right after the sensor's own read.
 * Only a real sensor read is converted: an input whose read function is not
 * its sensor row's (TEST INJECT, a test-mode scenario) already holds an
 * application value (#639 decision 3A). A CAN import takes its PID's
 * reference (0x0B/0x33/0x70 absolute, 0x0A/0x23 gauge).
 */
void applyPressureReference(Input& in);

/** Read one input now: its read function, then applyPressureReference. */
void readInputNow(Input& in);

#endif // PRESSURE_REFERENCE_H
