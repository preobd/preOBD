/*
 * layout_haltech.cpp - The HALTECH layout: Haltech's ECU broadcast for gauges
 * and dashes that listen to a Haltech ECU (#612 phase 4; docs/can/README.md)
 *
 * Source: Haltech ECU Broadcast CAN Protocol, document version 2.0 (08/2025).
 * 1 Mbit/s, 11-bit IDs, big-endian, unsigned unless marked. Only the channels
 * preOBD has an application for are filled; every other field in a sent frame
 * is 0, and a frame none of whose signals maps to an input is not sent.
 *
 *   temperatures   0.1 K               (x / 10 - 273.15 = degC)
 *   pressures      0.1 kPa ABSOLUTE    (Haltech: "all pressures are absolute";
 *                                       the dash subtracts 101.3 for gauge)
 *   brake pressure 1 kPa ABSOLUTE      (0x36B: y = x - 101.3, not /10)
 *   speed          0.1 km/h
 *   battery        0.1 V
 *   TPS, fuel level, humidity   0.1 %  (humidity signed)
 *
 * No data is 0 (CAN_LAYOUT_ZERO_NO_DATA): Haltech's newer channels say "0 if
 * there's an error (No Signal, Calibrating or Device Timeout)", and a Haltech
 * ECU sends 0 for a sensor that isn't fitted. A real value uses the field's
 * full range.
 *
 * Pressures follow §14.8: a row flagged LAYOUT_ABSOLUTE gets the barometric
 * reference added to a gauge application (oil, fuel, brake); MAP and baro are
 * absolute already. With a baro input away from sea level, the dash's gauge
 * reading is off by (baro - 101.3 kPa), exactly as with a Haltech ECU's own
 * absolute sensors.
 *
 * The frame index is the position in HALTECH_FRAME_IDS (can_layout_ids.cpp).
 * Haltech has no channel for CHT, coolant level, oil level (a switch here),
 * elevation, intercooler outlet temperature, gear oil temperature (beside
 * the gearbox's), the auxiliary battery or the other pressures.
 */

#include "../../config.h"

#if INCLUDE_CAN_LAYOUTS

#include <Arduino.h>
#include "can_layout.h"
#include "can_layout_ids.h"
#include "../../sensors/calibration_types.h"
#include "../../sensors/generated/registry_enums.h"

#define U_KPA 4    // UNITS_REGISTRY index of KPA (asserted in test_can_layout)
#define N     LAYOUT_UNITS_NONE
#define ABS   LAYOUT_ABSOLUTE

//              frame  app    inst  byte len flags measurement         units  scale  offset    min      max
#define KTEMP(f, app, inst, b)  { f, app, inst, b, 2, 0,   MEASURE_TEMPERATURE, N,     0.1f, -273.15f, -50.0f, 2000.0f }
#define APRESS(f, app, b)       { f, app, 1,    b, 2, ABS, MEASURE_PRESSURE,    U_KPA, 0.1f, 0.0f,     0.0f,   6000.0f }

const PROGMEM CanLayoutSignal HALTECH_SIGNALS[] = {
    // 0x360 (frame 0)
    { 0, APP_ENGINE_RPM,        1, 0, 2, 0, MEASURE_RPM,     N, 1.0f, 0.0f, 0.0f, 20000.0f },
    APRESS(0, APP_MAP_PRESSURE,  2),
    { 0, APP_THROTTLE_POSITION, 1, 4, 2, 0, MEASURE_PERCENT, N, 0.1f, 0.0f, 0.0f, 100.0f },
    // 0x361 (frame 1)
    APRESS(1, APP_FUEL_PRESSURE, 0),
    APRESS(1, APP_OIL_PRESSURE,  2),
    // 0x36B (frame 2): brake pressure front, 1 kPa absolute
    { 2, APP_BRAKE_PRESSURE,    1, 0, 2, ABS, MEASURE_PRESSURE, U_KPA, 1.0f, 0.0f, 0.0f, 20000.0f },
    // 0x370 (frame 3)
    { 3, APP_VEHICLE_SPEED,     1, 0, 2, 0, MEASURE_SPEED,   N, 0.1f, 0.0f, 0.0f, 655.0f },
    // 0x372 (frame 4)
    { 4, APP_PRIMARY_BATTERY,   1, 0, 2, 0, MEASURE_VOLTAGE, N, 0.1f, 0.0f, 0.0f, 30.0f },
    APRESS(4, APP_BAROMETRIC_PRESSURE, 6),
    // 0x373, 0x374 (frames 5, 6): EGT 1-8
    KTEMP(5, APP_EGT, 1, 0),
    KTEMP(5, APP_EGT, 2, 2),
    KTEMP(5, APP_EGT, 3, 4),
    KTEMP(5, APP_EGT, 4, 6),
    KTEMP(6, APP_EGT, 5, 0),
    KTEMP(6, APP_EGT, 6, 2),
    KTEMP(6, APP_EGT, 7, 4),
    KTEMP(6, APP_EGT, 8, 6),
    // 0x376 (frame 7)
    KTEMP(7, APP_AMBIENT_TEMP, 1, 0),
    { 7, APP_HUMIDITY,          1, 2, 2, LAYOUT_SIGNED, MEASURE_HUMIDITY, N, 0.1f, 0.0f, 0.0f, 100.0f },
    // 0x3E0 (frame 8): bytes 4-5 are fuel temperature, which preOBD has no application for
    KTEMP(8, APP_COOLANT_TEMP,     1, 0),
    KTEMP(8, APP_INTAKE_AIR_TEMP,  1, 2),
    KTEMP(8, APP_OIL_TEMP,         1, 6),
    // 0x3E1 (frame 9): "Gearbox Oil Temperature", "Diff Oil Temperature"
    KTEMP(9, APP_TRANS_TEMP,       1, 0),
    KTEMP(9, APP_DIFF_TEMP,        1, 2),
    // 0x6F8 (frame 10): "Fuel Level Percentage 0"; bytes 0-4 are state enums, 0 = Disabled
    { 10, APP_FUEL_LEVEL,       1, 6, 2, 0, MEASURE_PERCENT, N, 0.1f, 0.0f, 0.0f, 100.0f },
};

const CanLayout HALTECH_LAYOUT = {
    HALTECH_SIGNALS,
    (uint8_t)(sizeof(HALTECH_SIGNALS) / sizeof(HALTECH_SIGNALS[0])),
    HALTECH_FRAMES,
    0,
    HALTECH_FRAME_IDS,
    CAN_LAYOUT_ZERO_NO_DATA,
};

static_assert(sizeof(HALTECH_SIGNALS) / sizeof(HALTECH_SIGNALS[0]) <= CAN_LAYOUT_MAX_SIGNALS,
              "CAN_LAYOUT_MAX_SIGNALS bounds the per-output map");
static_assert(HALTECH_FRAMES <= LAYOUT_MAX_FRAMES, "a pass holds every frame");

#endif  // INCLUDE_CAN_LAYOUTS
