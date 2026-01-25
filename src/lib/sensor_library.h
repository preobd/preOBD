/*
 * sensor_library.h - Hardware Sensor Library (Orchestrator)
 *
 * This file orchestrates all sensor definitions by including modular sensor files
 * organized by sensor type. The SENSOR_LIBRARY[] array is assembled from X-macros
 * defined in each sensor category file.
 *
 * Directory structure:
 * - sensor_library/sensor_types.h      - SensorInfo struct, forward declarations
 * - sensor_library/sensor_categories.h - Category enum and helpers
 * - sensor_library/sensor_helpers.h    - Lookup functions
 * - sensor_library/sensors/            - Sensor definitions by type
 *
 * HOW TO ADD A NEW SENSOR:
 * 1. Add calibration data to sensor_calibration_data/ (if needed)
 * 2. Find the appropriate category file in sensor_library/sensors/
 * 3. Add PROGMEM strings for name and label
 * 4. Add X_SENSOR entry to the category's macro
 * 5. Compute hash: python3 -c "h=5381; s='YOUR_NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
 *
 * MEMORY: All data here is stored in PROGMEM (flash), not RAM.
 */

#ifndef SENSOR_LIBRARY_H
#define SENSOR_LIBRARY_H

#include <Arduino.h>
#include "../config.h"
#include "../inputs/input.h"
#include "sensor_calibration_data.h"
#include "hash.h"

// ===== CORE TYPE DEFINITIONS =====
#include "sensor_library/sensor_types.h"
#include "sensor_library/sensor_categories.h"

// ===== SENSOR DEFINITIONS BY CATEGORY =====
#include "sensor_library/sensors/none.h"
#include "sensor_library/sensors/thermocouples.h"
#include "sensor_library/sensors/thermistors.h"
#include "sensor_library/sensors/pressure.h"
#include "sensor_library/sensors/voltage.h"
#include "sensor_library/sensors/frequency.h"
#include "sensor_library/sensors/environmental.h"
#include "sensor_library/sensors/digital.h"

// ===== SENSOR LIBRARY ASSEMBLY (PROGMEM) =====
// Assemble SENSOR_LIBRARY[] from X-macros defined in each category file
#define X_SENSOR(name, label, desc, readFn, initFn, measType, calType, defCal, minInt, minVal, maxVal, hash, pinType) \
    { name, label, desc, readFn, initFn, measType, calType, defCal, minInt, minVal, maxVal, hash, pinType },

static const PROGMEM SensorInfo SENSOR_LIBRARY[] = {
    NONE_SENSORS
    THERMOCOUPLE_SENSORS
    THERMISTOR_SENSORS
    PRESSURE_SENSORS
    VOLTAGE_SENSORS
    FREQUENCY_SENSORS
    ENVIRONMENTAL_SENSORS
    DIGITAL_SENSORS
};

#undef X_SENSOR

// Automatically calculate the number of sensors
constexpr uint8_t NUM_SENSORS = sizeof(SENSOR_LIBRARY) / sizeof(SENSOR_LIBRARY[0]);

// ===== HELPER FUNCTIONS =====
// These depend on SENSOR_LIBRARY being defined, so must come after
#include "sensor_library/sensor_helpers.h"

#endif // SENSOR_LIBRARY_H
