/*
 * sensors.cpp - Define all sensors with their configurations
 */

#include "sensor_types.h"
#include "sensors.h"
#include "sensor_configs.h"
#include "config.h"

// Helper macro to reduce sensor definition boilerplate
// Fills in common fields that are derived from SensorConfig
#define SENSOR_DEFAULTS(config) \
    .value = 0, \
    .sensorType = config->internalType, \
    .displayName = config->name, \
    .display = true, \
    .isEnabled = true, \
    .readFunction = config->readFunction, \
    .displayConvert = config->displayConvert, \
    .obdConvert = config->obdConvert, \
    .calibrationData = config->calibrationData, \
    .calibrationType = config->calibrationType

// ===== SENSOR DEFINITIONS =====

#ifdef ENABLE_CHT
    const SensorConfig* cht_config = getSensorConfig(CHT_SENSOR_TYPE);
    
    Sensor CHT = {
        .input = CHT_INPUT,
        .obd2pid = 0xC8,
        .obd2length = 1,
        .abbrName = "CHT",
        #ifdef CHT_DISPLAY_UNITS
        .displayUnits = CHT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = CHT_MIN,
        .maxValue = CHT_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(cht_config)
    };
#endif

#ifdef ENABLE_EGT
    const SensorConfig* egt_config = getSensorConfig(EGT_SENSOR_TYPE);
    
    Sensor EGT = {
        .input = EGT_INPUT,
        .obd2pid = 0x78,
        .obd2length = 2,  // 2 bytes for high-temp range (was 9, but max single-frame is 6)
        .abbrName = "EGT",
        #ifdef EGT_DISPLAY_UNITS
        .displayUnits = EGT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = EGT_MIN,
        .maxValue = EGT_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(egt_config)
    };
#endif

#ifdef ENABLE_COOLANT_TEMP
    const SensorConfig* coolant_config = getSensorConfig(COOLANT_SENSOR_TYPE);
    
    #ifdef COOLANT_CUSTOM_CALIBRATION
        static ThermistorSteinhartCalibration coolant_custom_cal = {
            .bias_resistor = COOLANT_BIAS_RESISTOR,
            .steinhart_a = COOLANT_STEINHART_A,
            .steinhart_b = COOLANT_STEINHART_B,
            .steinhart_c = COOLANT_STEINHART_C
        };
    #endif
    
    Sensor coolantTemp = {
        .input = COOLANT_TEMP_INPUT,
        .obd2pid = 0x05,
        .obd2length = 1,
        .abbrName = "WTR",
        #ifdef COOLANT_DISPLAY_UNITS
        .displayUnits = COOLANT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = COOLANT_TEMP_MIN,
        .maxValue = COOLANT_TEMP_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(coolant_config)
        #ifdef COOLANT_CUSTOM_CALIBRATION
        , .calibrationData = &coolant_custom_cal
        , .calibrationType = CAL_THERMISTOR_STEINHART
        #endif
    };
#endif

#ifdef ENABLE_OIL_TEMP
    const SensorConfig* oil_config = getSensorConfig(OIL_TEMP_SENSOR_TYPE);
    
    #ifdef OIL_TEMP_CUSTOM_CALIBRATION
        static ThermistorSteinhartCalibration oil_custom_cal = {
            .bias_resistor = OIL_BIAS_RESISTOR,
            .steinhart_a = OIL_STEINHART_A,
            .steinhart_b = OIL_STEINHART_B,
            .steinhart_c = OIL_STEINHART_C
        };
    #endif
    
    Sensor oilTemp = {
        .input = OIL_TEMP_INPUT,
        .obd2pid = 0x5C,
        .obd2length = 1,
        .abbrName = "OIL",
        #ifdef OIL_DISPLAY_UNITS
        .displayUnits = OIL_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = OIL_TEMP_MIN,
        .maxValue = OIL_TEMP_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(oil_config)
        #ifdef OIL_TEMP_CUSTOM_CALIBRATION
        , .calibrationData = &oil_custom_cal
        , .calibrationType = CAL_THERMISTOR_STEINHART
        #endif
    };
#endif

#ifdef ENABLE_TCASE_TEMP
    const SensorConfig* tcase_config = getSensorConfig(TCASE_TEMP_SENSOR_TYPE);
    
    Sensor tcaseTemp = {
        .input = TCASE_TEMP_INPUT,
        .obd2pid = 0xC9,
        .obd2length = 1,
        .abbrName = "TRANS",
        #ifdef TCASE_DISPLAY_UNITS
        .displayUnits = TCASE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = TCASE_TEMP_MIN,
        .maxValue = TCASE_TEMP_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(tcase_config)
    };
#endif

#ifdef ENABLE_BOOST_PRESSURE
    const SensorConfig* boost_config = getSensorConfig(BOOST_PRESSURE_SENSOR_TYPE);
    
    Sensor boostPressure = {
        .input = BOOST_PRESSURE_INPUT,
        .obd2pid = 0x6F,
        .obd2length = 2,  // 2 bytes sufficient for pressure range (was 3)
        .abbrName = "BST",
        #ifdef BOOST_DISPLAY_UNITS
        .displayUnits = BOOST_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = BOOST_PRESSURE_MIN,
        .maxValue = BOOST_PRESSURE_MAX,
        .alarm = false,
        SENSOR_DEFAULTS(boost_config)
    };
#endif

#ifdef ENABLE_OIL_PRESSURE
    const SensorConfig* oil_press_config = getSensorConfig(OIL_PRESSURE_SENSOR_TYPE);
    
    Sensor oilPressure = {
        .input = OIL_PRESSURE_INPUT,
        .obd2pid = 0xCA,
        .obd2length = 1,
        .abbrName = "OPS",
        #ifdef OIL_PRESSURE_DISPLAY_UNITS
        .displayUnits = OIL_PRESSURE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = OIL_PRESSURE_MIN,
        .maxValue = OIL_PRESSURE_MAX,
        .alarm = true,
        SENSOR_DEFAULTS(oil_press_config)
    };
#endif

#ifdef ENABLE_PRIMARY_BATTERY
    const SensorConfig* bat1_config = getSensorConfig(PRIMARY_BATTERY_SENSOR_TYPE);
    
    Sensor primaryBattery = {
        .input = PRIMARY_BATTERY_INPUT,
        .obd2pid = 0xCB,
        .obd2length = 1,
        .abbrName = "BAT",
        .displayUnits = VOLTS,  // Always volts
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        SENSOR_DEFAULTS(bat1_config)
    };
#endif

#ifdef ENABLE_AUXILIARY_BATTERY
    const SensorConfig* bat2_config = getSensorConfig(SECONDARY_BATTERY_SENSOR_TYPE);
    
    Sensor auxiliaryBattery = {
        .input = SECONDARY_BATTERY_INPUT,
        .obd2pid = 0xCC,
        .obd2length = 1,
        .abbrName = "AUX",
        .displayUnits = VOLTS,  // Always volts
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        SENSOR_DEFAULTS(bat2_config)
    };
#endif

#ifdef ENABLE_AMBIENT_TEMP
    const SensorConfig* ambient_config = getSensorConfig(AMBIENT_TEMP_SENSOR_TYPE);
    
    Sensor ambientAirTemp = {
        .input = 0,
        .obd2pid = 0x46,
        .obd2length = 1,
        .abbrName = "AMB",
        #ifdef AMBIENT_DISPLAY_UNITS
        .displayUnits = AMBIENT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        SENSOR_DEFAULTS(ambient_config)
    };
#endif

#ifdef ENABLE_BAROMETRIC_PRESSURE
    const SensorConfig* baro_config = getSensorConfig(BARO_PRESSURE_SENSOR_TYPE);
    
    Sensor absBarPressure = {
        .input = 0,
        .obd2pid = 0x33,
        .obd2length = 1,
        .abbrName = "ABP",
        #ifdef BARO_DISPLAY_UNITS
        .displayUnits = BARO_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        SENSOR_DEFAULTS(baro_config)
    };
#endif

#ifdef ENABLE_HUMIDITY
    const SensorConfig* humidity_config = getSensorConfig(HUMIDITY_SENSOR_TYPE);
    
    Sensor humidity = {
        .input = 0,
        .obd2pid = 0xA0,
        .obd2length = 1,
        .abbrName = " RH",
        .displayUnits = PERCENT,  // Always percent
        .minValue = 0,
        .maxValue = 100,
        .alarm = false,
        SENSOR_DEFAULTS(humidity_config)
    };
#endif

#ifdef ENABLE_ALTITUDE
    const SensorConfig* altitude_config = getSensorConfig(ALTITUDE_SENSOR_TYPE);
    
    Sensor altitude = {
        .input = 0,
        .obd2pid = 0xA1,
        .obd2length = 2,
        .abbrName = "ALT",
        #ifdef ALTITUDE_DISPLAY_UNITS
        .displayUnits = ALTITUDE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_ALTITUDE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        SENSOR_DEFAULTS(altitude_config)
    };
#endif

// ===== SENSOR ARRAY =====
// Order of sensors in this array determines display order
Sensor *sensors[] = {
    #ifdef ENABLE_CHT
    &CHT,
    #endif
    #ifdef ENABLE_EGT
    &EGT,
    #endif
    #ifdef ENABLE_COOLANT_TEMP
    &coolantTemp,
    #endif
    #ifdef ENABLE_OIL_TEMP
    &oilTemp,
    #endif
    #ifdef ENABLE_TCASE_TEMP
    &tcaseTemp,
    #endif
    #ifdef ENABLE_BOOST_PRESSURE
    &boostPressure,
    #endif
    #ifdef ENABLE_OIL_PRESSURE
    &oilPressure,
    #endif
    #ifdef ENABLE_PRIMARY_BATTERY
    &primaryBattery,
    #endif
    #ifdef ENABLE_AUXILIARY_BATTERY
    &auxiliaryBattery,
    #endif
    #ifdef ENABLE_AMBIENT_TEMP
    &ambientAirTemp,
    #endif
    #ifdef ENABLE_BAROMETRIC_PRESSURE
    &absBarPressure,
    #endif
    #ifdef ENABLE_HUMIDITY
    &humidity,
    #endif
    #ifdef ENABLE_ALTITUDE
    &altitude,
    #endif
};

const int numSensors = sizeof(sensors) / sizeof(sensors[0]);