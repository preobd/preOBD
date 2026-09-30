/*
 * obd_encode.h - Encode an input's value as an OBD-II Service 01 answer (#516)
 *
 * The encoding is chosen by PID (obd_output_pids.h): a J1979 PID gets exactly
 * J1979's layout, so a standard tool reads it with no setup. Before #516 it
 * was chosen by measurement type, which sent e.g. RPM as rpm/4 and MAP as
 * bar*10 under PIDs whose J1979 formulas are rpm*4 and kPa.
 */

#ifndef OBD_ENCODE_H
#define OBD_ENCODE_H

#include <stdint.h>
#include "../sensors/can_sensor_library/obd_output_pids.h"

struct Input;

// 0x41, PID, then up to OBD_OUT_MAX_LENGTH payload bytes
#define OBD_ANSWER_MAX (2 + OBD_OUT_MAX_LENGTH)

#define OBD_STD_BARO_BAR 1.01325f   // barometric pressure when no baro input is fitted

/**
 * Payload for `spec` from a value in the input's BASE unit (degC, bar, V ...).
 * `gaugeInput`: the value is gauge pressure (a boost input) and an absolute
 * PID (OBD_OUT_ABSOLUTE) needs `baroBar` added. Rounds to the nearest step and
 * clamps to the field's range. Returns spec.length, or 0 for a non-finite
 * value. Pure: no globals.
 */
uint8_t obdEncodeValue(const ObdOutputPid& spec, float baseValue, bool gaugeInput,
                       float baroBar, uint8_t* payload);

/**
 * Can `in` answer on its obd2pid? A table-defined PID needs the input's exact
 * measurement type; any other PID needs an obd2length of 1-5.
 */
bool obdInputAnswerable(const Input& in);

/**
 * The whole answer for an input on its obd2pid: out[0] = 0x41, out[1] = PID,
 * then the payload. Returns 2 + payload length, or 0 when it can't answer
 * (no PID, NaN, wrong type). `out` holds OBD_ANSWER_MAX bytes. An answer
 * longer than 7 bytes needs a multi-frame (ISO-TP) reply.
 */
uint8_t obdBuildAnswer(const Input& in, uint8_t* out);

/** Barometric pressure (bar) from a BAROMETRIC_PRESSURE input, else standard. */
float obdBaroBar();

#endif // OBD_ENCODE_H
