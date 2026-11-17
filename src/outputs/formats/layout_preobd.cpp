/*
 * layout_preobd.cpp - The PREOBD layout: preOBD's own CAN frames for gauges
 * (#612 phase 3; docs/can/README.md, published as docs/can/preOBD.dbc)
 *
 * Nine frames at 0x500-0x508, 8 bytes each, big-endian, four 16-bit unsigned
 * fields per frame (the two level bytes in frame 8 excepted). Unsigned plus an
 * offset, so a field is never negative and all-ones is free for "no data":
 *
 *   temperatures   0.1 degC, offset -50       -50.0 .. 6503.4
 *   pressures      0.1 kPa                     0.0 .. 6553.4 (MAP, baro: absolute;
 *                  boost: gauge, offset -100) -100.0 .. 6453.4
 *   voltage        0.001 V                     0 .. 65.534
 *   speed          0.01 km/h                   0 .. 655.34
 *   rpm            1                           0 .. 65534
 *   TPS, fuel level, humidity   0.1 %          0 .. 6553.4 (100 is the full scale)
 *   elevation      1 m, offset -1000           -1000 .. 64534
 *   coolant, oil level   1 byte, 0 or 1 (a float switch; no unit)
 *
 * A field of all ones (0xFFFF / 0xFF) is "no data"; a real value clamps one
 * step below it. The pressures are carried in kPa, converted from the input's
 * base unit (bar) by UNITS_REGISTRY index 4 (KPA), checked in test_can_layout.
 * Each row also states the physical range it promises to carry
 * (rangeMin/rangeMax); the test checks it against the field and against every
 * application's alarm defaults.
 *
 * APPEND ONLY after release: a row moves nothing that is already there.
 */

#include "../../config.h"

#if INCLUDE_CAN_LAYOUTS

#include <Arduino.h>
#include "can_layout.h"
#include "can_layout_output.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/generated/registry_enums.h"

#define U_KPA 4    // UNITS_REGISTRY index of KPA (asserted in test_can_layout)
#define N     LAYOUT_UNITS_NONE

//          frame  app                    inst  byte len flags measurement       units scale   offset  min     max
#define TEMP(f, app, inst, b)  { f, app, inst, b, 2, 0, MEASURE_TEMPERATURE, N,     0.1f,   -50.0f, -50.0f, 2000.0f }
#define PRESS(f, app, inst, b) { f, app, inst, b, 2, 0, MEASURE_PRESSURE,    U_KPA, 0.1f,   0.0f,   0.0f,   6000.0f }

const PROGMEM CanLayoutSignal PREOBD_SIGNALS[] = {
    // frame 0: engine
    { 0, APP_ENGINE_RPM,           1, 0, 2, 0, MEASURE_RPM,   N,     1.0f,   0.0f,    0.0f,    20000.0f },
    { 0, APP_VEHICLE_SPEED,        1, 2, 2, 0, MEASURE_SPEED, N,     0.01f,  0.0f,    0.0f,    655.34f },
    { 0, APP_MAP_PRESSURE,         1, 4, 2, 0, MEASURE_PRESSURE, U_KPA, 0.1f, 0.0f,   0.0f,    6000.0f },
    { 0, APP_BOOST_PRESSURE,       1, 6, 2, 0, MEASURE_PRESSURE, U_KPA, 0.1f, -100.0f, -100.0f, 6000.0f },
    // frame 1: engine temperatures
    TEMP(1, APP_COOLANT_TEMP,      1, 0),
    TEMP(1, APP_OIL_TEMP,          1, 2),
    TEMP(1, APP_INTAKE_AIR_TEMP,   1, 4),
    TEMP(1, APP_INTERCOOLER_TEMP,  1, 6),
    // frame 2: pressures
    PRESS(2, APP_OIL_PRESSURE,       1, 0),
    PRESS(2, APP_FUEL_PRESSURE,      1, 2),
    PRESS(2, APP_BRAKE_PRESSURE,     1, 4),
    PRESS(2, APP_HYDRAULIC_PRESSURE, 1, 6),
    // frame 3: drivetrain temperatures
    TEMP(3, APP_TRANS_TEMP,        1, 0),
    TEMP(3, APP_DIFF_TEMP,         1, 2),
    TEMP(3, APP_TCASE_TEMP,        1, 4),
    TEMP(3, APP_GEAR_OIL_TEMP,     1, 6),
    // frame 4: exhaust gas temperature, four sensors
    TEMP(4, APP_EGT, 1, 0),
    TEMP(4, APP_EGT, 2, 2),
    TEMP(4, APP_EGT, 3, 4),
    TEMP(4, APP_EGT, 4, 6),
    // frame 5: cylinder head temperature, four sensors
    TEMP(5, APP_CHT, 1, 0),
    TEMP(5, APP_CHT, 2, 2),
    TEMP(5, APP_CHT, 3, 4),
    TEMP(5, APP_CHT, 4, 6),
    // frame 6: electrical and driver controls
    { 6, APP_PRIMARY_BATTERY,      1, 0, 2, 0, MEASURE_VOLTAGE, N, 0.001f, 0.0f, 0.0f,   30.0f },
    { 6, APP_AUXILIARY_BATTERY,    1, 2, 2, 0, MEASURE_VOLTAGE, N, 0.001f, 0.0f, 0.0f,   30.0f },
    { 6, APP_THROTTLE_POSITION,    1, 4, 2, 0, MEASURE_PERCENT, N, 0.1f,   0.0f, 0.0f,   100.0f },
    { 6, APP_FUEL_LEVEL,           1, 6, 2, 0, MEASURE_PERCENT, N, 0.1f,   0.0f, 0.0f,   100.0f },
    // frame 7: environment
    TEMP(7, APP_AMBIENT_TEMP,      1, 0),
    { 7, APP_BAROMETRIC_PRESSURE,  1, 2, 2, 0, MEASURE_PRESSURE,  U_KPA, 0.1f, 0.0f,     0.0f,     200.0f },
    { 7, APP_HUMIDITY,             1, 4, 2, 0, MEASURE_HUMIDITY,  N,     0.1f, 0.0f,     0.0f,     100.0f },
    { 7, APP_ELEVATION,            1, 6, 2, 0, MEASURE_ELEVATION, N,     1.0f, -1000.0f, -1000.0f, 9000.0f },
    // frame 8: float-switch levels, one byte each (0 or 1); bytes 2-7 are spare (0)
    { 8, APP_COOLANT_LEVEL,        1, 0, 1, 0, MEASURE_DIGITAL,   N,     1.0f, 0.0f,     0.0f,     1.0f },
    { 8, APP_OIL_LEVEL,            1, 1, 1, 0, MEASURE_DIGITAL,   N,     1.0f, 0.0f,     0.0f,     1.0f },
};

const CanLayout PREOBD_LAYOUT = {
    PREOBD_SIGNALS,
    (uint8_t)(sizeof(PREOBD_SIGNALS) / sizeof(PREOBD_SIGNALS[0])),
    PREOBD_FRAMES,
    PREOBD_BASE_ID,
};

static_assert(sizeof(CanLayoutSignal) == 24, "a signal row is 24 bytes");
static_assert(sizeof(PREOBD_SIGNALS) / sizeof(PREOBD_SIGNALS[0]) <= CAN_LAYOUT_MAX_SIGNALS,
              "CAN_LAYOUT_MAX_SIGNALS bounds the per-output map");

#endif  // INCLUDE_CAN_LAYOUTS
