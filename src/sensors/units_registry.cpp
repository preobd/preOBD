/*
 * units_registry.cpp - Display Units Registry (data)
 *
 * See units_registry.h. The PROGMEM string literals and the UNITS_REGISTRY[]
 * array itself live here, and only here, so there is exactly one copy in the
 * linked image regardless of how many translation units include the header
 * (#400 — was up to 12 copies on teensy41).
 *
 * CONVERSION FACTORS (from conversion functions in sensor_read.cpp):
 * - Fahrenheit: C * 9/5 + 32    => factor=1.8, offset=32.0
 * - PSI:        bar * 14.5038   => factor=14.5038, offset=0
 * - KPA:        bar * 100.0     => factor=100.0, offset=0
 * - INHG:       bar * 29.53     => factor=29.53, offset=0
 * - FEET:       m * 3.28084     => factor=3.28084, offset=0
 *
 * BASE UNITS (conversionFactor = 1.0, conversionOffset = 0.0):
 * - Temperature: Celsius
 * - Pressure: Bar
 * - Voltage: Volts
 * - Speed: RPM
 * - Percent: Percent (0.0 - 100.0)
 * - Distance: Meters
 */

#include "units_registry.h"

// String literals in PROGMEM
static const char PSTR_CELSIUS[] PROGMEM = "CELSIUS";
static const char PSTR_C[] PROGMEM = "C";

static const char PSTR_FAHRENHEIT[] PROGMEM = "FAHRENHEIT";
static const char PSTR_F[] PROGMEM = "F";

static const char PSTR_BAR[] PROGMEM = "BAR";
static const char PSTR_BAR_SYMBOL[] PROGMEM = "bar";

static const char PSTR_PSI[] PROGMEM = "PSI";
static const char PSTR_PSI_SYMBOL[] PROGMEM = "psi";

static const char PSTR_KPA[] PROGMEM = "KPA";
static const char PSTR_KPA_SYMBOL[] PROGMEM = "kPa";

static const char PSTR_INHG[] PROGMEM = "INHG";
static const char PSTR_INHG_SYMBOL[] PROGMEM = "inHg";

static const char PSTR_VOLTS[] PROGMEM = "VOLTS";
static const char PSTR_V[] PROGMEM = "V";

static const char PSTR_RPM[] PROGMEM = "RPM";
static const char PSTR_RPM_SYMBOL[] PROGMEM = "rpm";

static const char PSTR_PERCENT[] PROGMEM = "PERCENT";
static const char PSTR_PERCENT_SYMBOL[] PROGMEM = "%";

static const char PSTR_METERS[] PROGMEM = "METERS";
static const char PSTR_M[] PROGMEM = "M";
static const char PSTR_M_SYMBOL[] PROGMEM = "m";

static const char PSTR_FEET[] PROGMEM = "FEET";
static const char PSTR_FT[] PROGMEM = "FT";
static const char PSTR_FT_SYMBOL[] PROGMEM = "ft";

static const char PSTR_KPH[] PROGMEM = "KPH";
static const char PSTR_KPH_SYMBOL[] PROGMEM = "km/h";

static const char PSTR_MPH[] PROGMEM = "MPH";
static const char PSTR_MPH_SYMBOL[] PROGMEM = "mph";

// #509: units of imported OBD-II PIDs. Where the symbol equals the alias
// ("km", "s") the alias string doubles as the symbol.
static const char PSTR_KILOMETERS[] PROGMEM = "KILOMETERS";
static const char PSTR_KM[] PROGMEM = "KM";
static const char PSTR_KM_SYMBOL[] PROGMEM = "km";
static const char PSTR_MILES[] PROGMEM = "MILES";
static const char PSTR_MI[] PROGMEM = "MI";
static const char PSTR_MI_SYMBOL[] PROGMEM = "mi";
static const char PSTR_SECONDS[] PROGMEM = "SECONDS";
static const char PSTR_S[] PROGMEM = "S";
static const char PSTR_S_SYMBOL[] PROGMEM = "s";
static const char PSTR_MINUTES[] PROGMEM = "MINUTES";
static const char PSTR_MIN[] PROGMEM = "MIN";
static const char PSTR_MIN_SYMBOL[] PROGMEM = "min";
static const char PSTR_HOURS[] PROGMEM = "HOURS";
static const char PSTR_H[] PROGMEM = "H";
static const char PSTR_H_SYMBOL[] PROGMEM = "h";
static const char PSTR_GRAMS_PER_SEC[] PROGMEM = "GRAMS_PER_SEC";
static const char PSTR_GPS[] PROGMEM = "GPS";
static const char PSTR_GPS_SYMBOL[] PROGMEM = "g/s";
static const char PSTR_LB_PER_MIN[] PROGMEM = "LB_PER_MIN";
static const char PSTR_LBMIN[] PROGMEM = "LBMIN";
static const char PSTR_LBMIN_SYMBOL[] PROGMEM = "lb/min";
static const char PSTR_LITERS_PER_HOUR[] PROGMEM = "LITERS_PER_HOUR";
static const char PSTR_LPH[] PROGMEM = "LPH";
static const char PSTR_LPH_SYMBOL[] PROGMEM = "L/h";
static const char PSTR_GAL_PER_HOUR[] PROGMEM = "GAL_PER_HOUR";
static const char PSTR_GPH[] PROGMEM = "GPH";
static const char PSTR_GPH_SYMBOL[] PROGMEM = "gal/h";

/**
 * UNITS_REGISTRY - Complete unit definitions
 */
const PROGMEM UnitsInfo UNITS_REGISTRY[] = {
    // Index 0: CELSIUS (base unit for temperature)
    {
        .name = PSTR_CELSIUS,
        .alias = PSTR_C,
        .symbol = PSTR_C,
        .measurementType = MEASURE_TEMPERATURE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x82DD,  // djb2_hash("CELSIUS")
        .aliasHash = 0xB5E8  // djb2_hash("C")
    },

    // Index 1: FAHRENHEIT
    {
        .name = PSTR_FAHRENHEIT,
        .alias = PSTR_F,
        .symbol = PSTR_F,
        .measurementType = MEASURE_TEMPERATURE,
        .conversionFactor = 1.8,
        .conversionOffset = 32.0,
        .nameHash = 0xA9E3,  // djb2_hash("FAHRENHEIT")
        .aliasHash = 0xB5EB  // djb2_hash("F")
    },

    // Index 2: BAR (base unit for pressure)
    {
        .name = PSTR_BAR,
        .alias = PSTR_BAR,       // Same as name
        .symbol = PSTR_BAR_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xD45A,  // djb2_hash("BAR")
        .aliasHash = 0xD45A  // same as name
    },

    // Index 3: PSI
    {
        .name = PSTR_PSI,
        .alias = PSTR_PSI,
        .symbol = PSTR_PSI_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 14.5038,
        .conversionOffset = 0.0,
        .nameHash = 0x1231,  // djb2_hash("PSI")
        .aliasHash = 0x1231  // same as name
    },

    // Index 4: KPA
    {
        .name = PSTR_KPA,
        .alias = PSTR_KPA,
        .symbol = PSTR_KPA_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 100.0,
        .conversionOffset = 0.0,
        .nameHash = 0xFC81,  // djb2_hash("KPA")
        .aliasHash = 0xFC81  // same as name
    },

    // Index 5: INHG
    {
        .name = PSTR_INHG,
        .alias = PSTR_INHG,
        .symbol = PSTR_INHG_SYMBOL,
        .measurementType = MEASURE_PRESSURE,
        .conversionFactor = 29.53,
        .conversionOffset = 0.0,
        .nameHash = 0x6C8B,  // djb2_hash("INHG")
        .aliasHash = 0x6C8B  // same as name
    },

    // Index 6: VOLTS (base unit)
    {
        .name = PSTR_VOLTS,
        .alias = PSTR_V,
        .symbol = PSTR_V,
        .measurementType = MEASURE_VOLTAGE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xDBDD,  // djb2_hash("VOLTS")
        .aliasHash = 0xB5FB  // djb2_hash("V")
    },

    // Index 7: RPM (base unit)
    {
        .name = PSTR_RPM,
        .alias = PSTR_RPM,
        .symbol = PSTR_RPM_SYMBOL,
        .measurementType = MEASURE_RPM,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x1A54,  // djb2_hash("RPM")
        .aliasHash = 0x1A54  // same as name
    },

    // Index 8: PERCENT (base unit)
    {
        .name = PSTR_PERCENT,
        .alias = PSTR_PERCENT_SYMBOL,
        .symbol = PSTR_PERCENT_SYMBOL,
        .measurementType = MEASURE_HUMIDITY,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x53B6,  // djb2_hash("PERCENT")
        .aliasHash = 0xB5CA  // djb2_hash("%")
    },

    // Index 9: METERS (base unit for elevation)
    {
        .name = PSTR_METERS,
        .alias = PSTR_M,
        .symbol = PSTR_M_SYMBOL,
        .measurementType = MEASURE_ELEVATION,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x1835,  // djb2_hash("METERS")
        .aliasHash = 0xB5F2  // djb2_hash("M")
    },

    // Index 10: FEET
    {
        .name = PSTR_FEET,
        .alias = PSTR_FT,
        .symbol = PSTR_FT_SYMBOL,
        .measurementType = MEASURE_ELEVATION,
        .conversionFactor = 3.28084,
        .conversionOffset = 0.0,
        .nameHash = 0xA0C9,  // djb2_hash("FEET")
        .aliasHash = 0x739F  // djb2_hash("FT")
    },

    // Index 11: KPH (base unit for speed)
    {
        .name = PSTR_KPH,
        .alias = PSTR_KPH,
        .symbol = PSTR_KPH_SYMBOL,
        .measurementType = MEASURE_SPEED,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xFC88,  // djb2_hash("KPH")
        .aliasHash = 0xFC88  // same as name
    },

    // Index 12: MPH
    {
        .name = PSTR_MPH,
        .alias = PSTR_MPH,
        .symbol = PSTR_MPH_SYMBOL,
        .measurementType = MEASURE_SPEED,
        .conversionFactor = 0.621371,  // km/h to mph conversion
        .conversionOffset = 0.0,
        .nameHash = 0x050A,  // djb2_hash("MPH")
        .aliasHash = 0x050A  // same as name
    },    // Index 13: KILOMETERS (base unit for distance)
    {
        .name = PSTR_KILOMETERS,
        .alias = PSTR_KM,
        .symbol = PSTR_KM_SYMBOL,
        .measurementType = MEASURE_DISTANCE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0xA304,
        .aliasHash = 0x743D
    },
    // Index 14: MILES
    {
        .name = PSTR_MILES,
        .alias = PSTR_MI,
        .symbol = PSTR_MI_SYMBOL,
        .measurementType = MEASURE_DISTANCE,
        .conversionFactor = 0.621371,
        .conversionOffset = 0.0,
        .nameHash = 0xB31F,
        .aliasHash = 0x747B
    },
    // Index 15: SECONDS (base unit for time)
    {
        .name = PSTR_SECONDS,
        .alias = PSTR_S,
        .symbol = PSTR_S_SYMBOL,
        .measurementType = MEASURE_TIME,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x53F4,
        .aliasHash = 0xB5F8
    },
    // Index 16: MINUTES
    {
        .name = PSTR_MINUTES,
        .alias = PSTR_MIN,
        .symbol = PSTR_MIN_SYMBOL,
        .measurementType = MEASURE_TIME,
        .conversionFactor = (1.0 / 60.0),
        .conversionOffset = 0.0,
        .nameHash = 0xFB6A,
        .aliasHash = 0x0429
    },
    // Index 17: HOURS
    {
        .name = PSTR_HOURS,
        .alias = PSTR_H,
        .symbol = PSTR_H_SYMBOL,
        .measurementType = MEASURE_TIME,
        .conversionFactor = (1.0 / 3600.0),
        .conversionOffset = 0.0,
        .nameHash = 0xAAD6,
        .aliasHash = 0xB5ED
    },
    // Index 18: GRAMS_PER_SEC (base unit for mass air flow)
    {
        .name = PSTR_GRAMS_PER_SEC,
        .alias = PSTR_GPS,
        .symbol = PSTR_GPS_SYMBOL,
        .measurementType = MEASURE_MASS_FLOW,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x7B7F,
        .aliasHash = 0xEB8F
    },
    // Index 19: LB_PER_MIN (1 g/s = 0.132277 lb/min)
    {
        .name = PSTR_LB_PER_MIN,
        .alias = PSTR_LBMIN,
        .symbol = PSTR_LBMIN_SYMBOL,
        .measurementType = MEASURE_MASS_FLOW,
        .conversionFactor = 0.132277,
        .conversionOffset = 0.0,
        .nameHash = 0x491C,
        .aliasHash = 0xC8B7
    },
    // Index 20: LITERS_PER_HOUR (base unit for fuel rate)
    {
        .name = PSTR_LITERS_PER_HOUR,
        .alias = PSTR_LPH,
        .symbol = PSTR_LPH_SYMBOL,
        .measurementType = MEASURE_FUEL_RATE,
        .conversionFactor = 1.0,
        .conversionOffset = 0.0,
        .nameHash = 0x645B,
        .aliasHash = 0x00C9
    },
    // Index 21: GAL_PER_HOUR (US gallons)
    {
        .name = PSTR_GAL_PER_HOUR,
        .alias = PSTR_GPH,
        .symbol = PSTR_GPH_SYMBOL,
        .measurementType = MEASURE_FUEL_RATE,
        .conversionFactor = 0.264172,
        .conversionOffset = 0.0,
        .nameHash = 0x8D7C,
        .aliasHash = 0xEB84
    }
};

static_assert(sizeof(UNITS_REGISTRY) / sizeof(UNITS_REGISTRY[0]) == NUM_UNITS,
              "NUM_UNITS (units_registry.h) has drifted from UNITS_REGISTRY's real extent");

// Empty string in PROGMEM for invalid unit index
static const char PSTR_EMPTY_UNIT[] PROGMEM = "";

const char* getUnitStringByIndex(uint8_t unitsIndex) {
    if (unitsIndex >= NUM_UNITS) return PSTR_EMPTY_UNIT;

    const UnitsInfo* info = &UNITS_REGISTRY[unitsIndex];
    return (const char*)pgm_read_ptr(&info->symbol);
}
