/*
 * sensor_library.cpp - Hardware Sensor Library (data + assembly)
 *
 * See sensor_library.h. This is the ONLY translation unit that includes the
 * sensor category headers (sensor_library/sensors/ *.h) and the calibration
 * data aggregator (sensor_calibration_data.h). Every PROGMEM string literal
 * and calibration array declared "static" inside those headers therefore
 * now compiles exactly once, and SENSOR_LIBRARY[] itself is defined exactly
 * once — instead of once per .cpp that (transitively) included
 * sensor_library.h, which is what produced 5-7 duplicate copies of the
 * table (and up to 12 of some individual calibration arrays) before #400.
 *
 * The category headers (sensor_library/sensors/ *.h) and per-vendor
 * calibration headers (sensor_calibration_data/<vendor>/ *.h) are unchanged — they
 * still declare their data "static const ... PROGMEM", which is exactly
 * right now that this is their only translation unit.
 */

#include "sensor_library.h"
#include "../config.h"
#include "sensor_calibration_data.h"

// ===== SENSOR DEFINITIONS BY CATEGORY =====
#include "sensor_library/sensors/none.h"
#include "sensor_library/sensors/thermocouples.h"
#include "sensor_library/sensors/thermistors.h"
#include "sensor_library/sensors/pressure.h"
#include "sensor_library/sensors/voltage.h"
#include "sensor_library/sensors/frequency.h"
#include "sensor_library/sensors/environmental.h"
#include "sensor_library/sensors/digital.h"
#include "sensor_library/sensors/level.h"
#if INCLUDE_CAN
#include "sensor_library/sensors/can.h"
#else
#define CAN_SENSORS
#endif

// ===== SENSOR LIBRARY ASSEMBLY (PROGMEM) =====
// Assemble SENSOR_LIBRARY[] from X-macros defined in each category file
#define X_SENSOR(name, label, desc, readFn, initFn, measType, calType, defCal, minInt, minVal, maxVal, hash, pinType) \
    { name, label, desc, readFn, initFn, measType, calType, defCal, minInt, minVal, maxVal, hash, pinType },

const PROGMEM SensorInfo SENSOR_LIBRARY[] = {
    NONE_SENSORS
    THERMOCOUPLE_SENSORS
    THERMISTOR_SENSORS
    PRESSURE_SENSORS
    VOLTAGE_SENSORS
    FREQUENCY_SENSORS
    ENVIRONMENTAL_SENSORS
    DIGITAL_SENSORS
    LEVEL_SENSORS
    CAN_SENSORS
};

#undef X_SENSOR

static_assert(sizeof(SENSOR_LIBRARY) / sizeof(SENSOR_LIBRARY[0]) == NUM_SENSORS,
              "NUM_SENSORS (sensor_library.h) has drifted from SENSOR_LIBRARY's real extent");
