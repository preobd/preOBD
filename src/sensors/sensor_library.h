/*
 * sensor_library.h - Hardware Sensor Library (Orchestrator)
 *
 * Declares the SENSOR_LIBRARY[] registry. The array is assembled from
 * X-macros defined in each sensor category file, but that assembly — and
 * every category header, calibration header and PROGMEM string it pulls
 * in — now happens in sensor_library.cpp, not here.
 *
 * Why: this header used to do the assembly itself, so every one of the 18
 * .cpp files that include it got its own PROGMEM copy of SENSOR_LIBRARY[]
 * (and of every calibration array and PSTR_* string reachable from it) —
 * the PROGMEM section attribute stops the linker folding them back
 * together. Moving the assembly into one .cpp means those headers are now
 * compiled exactly once, so there is exactly one copy of everything in the
 * linked image (#400). Same shape as bus_pin_roles.{h,cpp} (#304).
 *
 * Directory structure:
 * - sensor_library/sensor_types.h      - SensorInfo struct, forward declarations
 * - sensor_library/sensor_categories.h - Category enum and helpers
 * - sensor_library/sensor_helpers.h    - Lookup functions
 * - sensor_library/sensors/            - Sensor definitions by type (assembled
 *                                         only from sensor_library.cpp now)
 *
 * HOW TO ADD A NEW SENSOR:
 * 1. Add calibration data to sensor_calibration_data/ (if needed)
 * 2. Find the appropriate category file in sensor_library/sensors/
 * 3. Add PROGMEM strings for name and label
 * 4. Add X_SENSOR entry to the category's macro
 * 5. Compute hash: python3 -c "h=5381; s='YOUR_NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
 * 6. Bump NUM_SENSORS below to match the new entry count
 *
 * MEMORY: All data here is stored in PROGMEM (flash), not RAM.
 */

#ifndef SENSOR_LIBRARY_H
#define SENSOR_LIBRARY_H

#include <Arduino.h>
#include "../inputs/input.h"
#include "../core/hash.h"

// ===== CORE TYPE DEFINITIONS =====
#include "sensor_library/sensor_types.h"
#include "sensor_library/sensor_categories.h"

// ===== SENSOR LIBRARY (PROGMEM - Flash Memory, assembled in sensor_library.cpp) =====
//
// NUM_SENSORS is a manifest constant, not sizeof(SENSOR_LIBRARY)/sizeof(...) —
// an extern array of unknown bound can't be sized from here. It tracks
// INCLUDE_CAN the same way sensor_library.cpp's assembly does, since the CAN
// category is conditionally compiled in. sensor_library.cpp carries a
// static_assert tying it to the array's real extent so the two cannot
// silently drift.
#if INCLUDE_CAN
#define NUM_SENSORS 56
#else
#define NUM_SENSORS 55
#endif

extern const PROGMEM SensorInfo SENSOR_LIBRARY[];

// ===== HELPER FUNCTIONS =====
// These depend on SENSOR_LIBRARY being defined, so must come after
#include "sensor_library/sensor_helpers.h"

#endif // SENSOR_LIBRARY_H
