/*
 * sensor_categories.cpp - Sensor Category Definitions (data)
 *
 * See sensor_categories.h. The PSTR_CAT_* string literals and the
 * SENSOR_CATEGORIES[] array itself live here, and only here, so there is
 * exactly one copy in the linked image regardless of how many translation
 * units include the header (#400 missed this file; fixed here).
 */

#include "sensor_categories.h"

// ===== SENSOR CATEGORY STRINGS (PROGMEM) =====
// Category names match the sensor definition files in sensors/ directory
static const char PSTR_CAT_THERMOCOUPLE[] PROGMEM = "THERMOCOUPLE";
static const char PSTR_CAT_THERMOCOUPLE_LABEL[] PROGMEM = "K-Type Thermocouples";
static const char PSTR_CAT_THERMISTOR[] PROGMEM = "THERMISTOR";
static const char PSTR_CAT_THERMISTOR_LABEL[] PROGMEM = "Thermistor Temperature Sensors";
static const char PSTR_CAT_PRESSURE[] PROGMEM = "PRESSURE";
static const char PSTR_CAT_PRESSURE_LABEL[] PROGMEM = "Pressure Sensors";
static const char PSTR_CAT_VOLTAGE[] PROGMEM = "VOLTAGE";
static const char PSTR_CAT_VOLTAGE_LABEL[] PROGMEM = "Voltage Sensors";
static const char PSTR_CAT_FREQUENCY[] PROGMEM = "FREQUENCY";
static const char PSTR_CAT_FREQUENCY_LABEL[] PROGMEM = "RPM and Speed Sensors";
static const char PSTR_CAT_ENVIRONMENTAL[] PROGMEM = "ENVIRONMENTAL";
static const char PSTR_CAT_ENVIRONMENTAL_LABEL[] PROGMEM = "Environmental Sensors";
static const char PSTR_CAT_DIGITAL[] PROGMEM = "DIGITAL";
static const char PSTR_CAT_DIGITAL_LABEL[] PROGMEM = "Digital Input Sensors";
static const char PSTR_CAT_LEVEL[] PROGMEM = "LEVEL";
static const char PSTR_CAT_LEVEL_LABEL[] PROGMEM = "Fluid Level Sensors";

// ===== SENSOR CATEGORY REGISTRY (PROGMEM) =====
// Hash values computed with: python3 -c "h=5381; s='NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
const PROGMEM SensorCategoryInfo SENSOR_CATEGORIES[] = {
    { PSTR_CAT_THERMOCOUPLE,    PSTR_CAT_THERMOCOUPLE_LABEL,    0xA69C },  // THERMOCOUPLE
    { PSTR_CAT_THERMISTOR,      PSTR_CAT_THERMISTOR_LABEL,      0x4556 },  // THERMISTOR
    { PSTR_CAT_PRESSURE,        PSTR_CAT_PRESSURE_LABEL,        0x233E },  // PRESSURE
    { PSTR_CAT_VOLTAGE,         PSTR_CAT_VOLTAGE_LABEL,         0x03F7 },  // VOLTAGE
    { PSTR_CAT_FREQUENCY,       PSTR_CAT_FREQUENCY_LABEL,       0x9B8F },  // FREQUENCY
    { PSTR_CAT_ENVIRONMENTAL,   PSTR_CAT_ENVIRONMENTAL_LABEL,   0x0C07 },  // ENVIRONMENTAL
    { PSTR_CAT_DIGITAL,         PSTR_CAT_DIGITAL_LABEL,         0x9803 },  // DIGITAL
    { PSTR_CAT_LEVEL,           PSTR_CAT_LEVEL_LABEL,           0x939D },  // LEVEL (djb2("LEVEL"))
};

static_assert(sizeof(SENSOR_CATEGORIES) / sizeof(SENSOR_CATEGORIES[0]) == CAT_COUNT,
              "CAT_COUNT (sensor_categories.h) has drifted from SENSOR_CATEGORIES's real extent");
