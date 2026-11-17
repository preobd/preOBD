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
 * 0.5-1.2 bar; otherwise STD_BARO_BAR. Never NaN.
 */
float baroReferenceBar();

#endif // PRESSURE_REFERENCE_H
