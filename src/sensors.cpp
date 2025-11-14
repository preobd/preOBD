/*
 * sensors.cpp - Define all sensors with their configurations
 */

#include "sensor_types.h"
#include "sensors.h"
#include "sensor_configs.h"
#include "config.h"

// ===== SENSOR DEFINITIONS =====

#ifdef ENABLE_CHT
    const SensorConfig* cht_config = getSensorConfig(CHT_SENSOR_TYPE);
    
    Sensor CHT = {
        .input = CHT_INPUT,
        .obd2pid = 0xC8,
        .obd2length = 1,
        .value = 0,
        .sensorType = cht_config->internalType,
        .abbrName = "CHT",
        .displayName = cht_config->name,
        // Use sensor-specific override or global default
        #ifdef CHT_DISPLAY_UNITS
        .displayUnits = CHT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = CHT_MIN,
        .maxValue = CHT_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = cht_config->readFunction,
        .displayConvert = cht_config->displayConvert,
        .obdConvert = cht_config->obdConvert,
        .calibrationData = cht_config->calibrationData,
        .calibrationType = cht_config->calibrationType
    };
#endif

#ifdef ENABLE_EGT
    const SensorConfig* egt_config = getSensorConfig(EGT_SENSOR_TYPE);
    
    Sensor EGT = {
        .input = EGT_INPUT,
        .obd2pid = 0x78,
        .obd2length = 2,  // 2 bytes for high-temp range (was 9, but max single-frame is 6)
        .value = 0,
        .sensorType = egt_config->internalType,
        .abbrName = "EGT",
        .displayName = egt_config->name,
        #ifdef EGT_DISPLAY_UNITS
        .displayUnits = EGT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = EGT_MIN,
        .maxValue = EGT_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = egt_config->readFunction,
        .displayConvert = egt_config->displayConvert,
        .obdConvert = egt_config->obdConvert,
        .calibrationData = egt_config->calibrationData,
        .calibrationType = egt_config->calibrationType
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
        .value = 0,
        .sensorType = coolant_config->internalType,
        .abbrName = "WTR",
        .displayName = coolant_config->name,
        #ifdef COOLANT_DISPLAY_UNITS
        .displayUnits = COOLANT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = COOLANT_TEMP_MIN,
        .maxValue = COOLANT_TEMP_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = coolant_config->readFunction,
        .displayConvert = coolant_config->displayConvert,
        .obdConvert = coolant_config->obdConvert,
        #ifdef COOLANT_CUSTOM_CALIBRATION
        .calibrationData = &coolant_custom_cal,
        .calibrationType = CAL_THERMISTOR_STEINHART
        #else
        .calibrationData = coolant_config->calibrationData,
        .calibrationType = coolant_config->calibrationType
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
        .value = 0,
        .sensorType = oil_config->internalType,
        .abbrName = "OIL",
        .displayName = oil_config->name,
        #ifdef OIL_DISPLAY_UNITS
        .displayUnits = OIL_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = OIL_TEMP_MIN,
        .maxValue = OIL_TEMP_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = oil_config->readFunction,
        .displayConvert = oil_config->displayConvert,
        .obdConvert = oil_config->obdConvert,
        #ifdef OIL_TEMP_CUSTOM_CALIBRATION
        .calibrationData = &oil_custom_cal,
        .calibrationType = CAL_THERMISTOR_STEINHART
        #else
        .calibrationData = oil_config->calibrationData,
        .calibrationType = oil_config->calibrationType
        #endif
    };
#endif

#ifdef ENABLE_TCASE_TEMP
    const SensorConfig* tcase_config = getSensorConfig(TCASE_TEMP_SENSOR_TYPE);
    
    Sensor tcaseTemp = {
        .input = TCASE_TEMP_INPUT,
        .obd2pid = 0xC9,
        .obd2length = 1,
        .value = 0,
        .sensorType = tcase_config->internalType,
        .abbrName = "TRANS",
        .displayName = tcase_config->name,
        #ifdef TCASE_DISPLAY_UNITS
        .displayUnits = TCASE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = TCASE_TEMP_MIN,
        .maxValue = TCASE_TEMP_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = tcase_config->readFunction,
        .displayConvert = tcase_config->displayConvert,
        .obdConvert = tcase_config->obdConvert,
        .calibrationData = tcase_config->calibrationData,
        .calibrationType = tcase_config->calibrationType
    };
#endif

#ifdef ENABLE_BOOST_PRESSURE
    const SensorConfig* boost_config = getSensorConfig(BOOST_PRESSURE_SENSOR_TYPE);
    
    Sensor boostPressure = {
        .input = BOOST_PRESSURE_INPUT,
        .obd2pid = 0x6F,
        .obd2length = 2,  // 2 bytes sufficient for pressure range (was 3)
        .value = 0,
        .sensorType = boost_config->internalType,
        .abbrName = "BST",
        .displayName = boost_config->name,
        #ifdef BOOST_DISPLAY_UNITS
        .displayUnits = BOOST_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = BOOST_PRESSURE_MIN,
        .maxValue = BOOST_PRESSURE_MAX,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = boost_config->readFunction,
        .displayConvert = boost_config->displayConvert,
        .obdConvert = boost_config->obdConvert,
        .calibrationData = boost_config->calibrationData,
        .calibrationType = boost_config->calibrationType
    };
#endif

#ifdef ENABLE_OIL_PRESSURE
    const SensorConfig* oil_press_config = getSensorConfig(OIL_PRESSURE_SENSOR_TYPE);
    
    Sensor oilPressure = {
        .input = OIL_PRESSURE_INPUT,
        .obd2pid = 0xCA,
        .obd2length = 1,
        .value = 0,
        .sensorType = oil_press_config->internalType,
        .abbrName = "OPS",
        .displayName = oil_press_config->name,
        #ifdef OIL_PRESSURE_DISPLAY_UNITS
        .displayUnits = OIL_PRESSURE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = OIL_PRESSURE_MIN,
        .maxValue = OIL_PRESSURE_MAX,
        .alarm = true,
        .display = true,
        .isEnabled = true,
        .readFunction = oil_press_config->readFunction,
        .displayConvert = oil_press_config->displayConvert,
        .obdConvert = oil_press_config->obdConvert,
        .calibrationData = oil_press_config->calibrationData,
        .calibrationType = oil_press_config->calibrationType
    };
#endif

#ifdef ENABLE_PRIMARY_BATTERY
    const SensorConfig* bat1_config = getSensorConfig(PRIMARY_BATTERY_SENSOR_TYPE);
    
    Sensor primaryBattery = {
        .input = PRIMARY_BATTERY_INPUT,
        .obd2pid = 0xCB,
        .obd2length = 1,
        .value = 0,
        .sensorType = bat1_config->internalType,
        .abbrName = "BAT",
        .displayName = bat1_config->name,
        .displayUnits = VOLTS,  // Always volts
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = bat1_config->readFunction,
        .displayConvert = bat1_config->displayConvert,
        .obdConvert = bat1_config->obdConvert,
        .calibrationData = bat1_config->calibrationData,
        .calibrationType = bat1_config->calibrationType
    };
#endif

#ifdef ENABLE_AUXILIARY_BATTERY
    const SensorConfig* bat2_config = getSensorConfig(SECONDARY_BATTERY_SENSOR_TYPE);
    
    Sensor auxiliaryBattery = {
        .input = SECONDARY_BATTERY_INPUT,
        .obd2pid = 0xCC,
        .obd2length = 1,
        .value = 0,
        .sensorType = bat2_config->internalType,
        .abbrName = "AUX",
        .displayName = bat2_config->name,
        .displayUnits = VOLTS,  // Always volts
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = bat2_config->readFunction,
        .displayConvert = bat2_config->displayConvert,
        .obdConvert = bat2_config->obdConvert,
        .calibrationData = bat2_config->calibrationData,
        .calibrationType = bat2_config->calibrationType
    };
#endif

#ifdef ENABLE_AMBIENT_TEMP
    const SensorConfig* ambient_config = getSensorConfig(AMBIENT_TEMP_SENSOR_TYPE);
    
    Sensor ambientAirTemp = {
        .input = 0,
        .obd2pid = 0x46,
        .obd2length = 1,
        .value = 0,
        .sensorType = ambient_config->internalType,
        .abbrName = "AMB",
        .displayName = ambient_config->name,
        #ifdef AMBIENT_DISPLAY_UNITS
        .displayUnits = AMBIENT_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_TEMPERATURE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = ambient_config->readFunction,
        .displayConvert = ambient_config->displayConvert,
        .obdConvert = ambient_config->obdConvert,
        .calibrationData = ambient_config->calibrationData,
        .calibrationType = ambient_config->calibrationType
    };
#endif

#ifdef ENABLE_BAROMETRIC_PRESSURE
    const SensorConfig* baro_config = getSensorConfig(BARO_PRESSURE_SENSOR_TYPE);
    
    Sensor absBarPressure = {
        .input = 0,
        .obd2pid = 0x33,
        .obd2length = 1,
        .value = 0,
        .sensorType = baro_config->internalType,
        .abbrName = "ABP",
        .displayName = baro_config->name,
        #ifdef BARO_DISPLAY_UNITS
        .displayUnits = BARO_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_PRESSURE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = baro_config->readFunction,
        .displayConvert = baro_config->displayConvert,
        .obdConvert = baro_config->obdConvert,
        .calibrationData = baro_config->calibrationData,
        .calibrationType = baro_config->calibrationType
    };
#endif

#ifdef ENABLE_HUMIDITY
    const SensorConfig* humidity_config = getSensorConfig(HUMIDITY_SENSOR_TYPE);
    
    Sensor humidity = {
        .input = 0,
        .obd2pid = 0xA0,
        .obd2length = 1,
        .value = 0,
        .sensorType = humidity_config->internalType,
        .abbrName = " RH",
        .displayName = humidity_config->name,
        .displayUnits = PERCENT,  // Always percent
        .minValue = 0,
        .maxValue = 100,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = humidity_config->readFunction,
        .displayConvert = humidity_config->displayConvert,
        .obdConvert = humidity_config->obdConvert,
        .calibrationData = humidity_config->calibrationData,
        .calibrationType = humidity_config->calibrationType
    };
#endif

#ifdef ENABLE_ALTITUDE
    const SensorConfig* altitude_config = getSensorConfig(ALTITUDE_SENSOR_TYPE);
    
    Sensor altitude = {
        .input = 0,
        .obd2pid = 0xA1,
        .obd2length = 2,
        .value = 0,
        .sensorType = altitude_config->internalType,
        .abbrName = "ALT",
        .displayName = altitude_config->name,
        #ifdef ALTITUDE_DISPLAY_UNITS
        .displayUnits = ALTITUDE_DISPLAY_UNITS,
        #else
        .displayUnits = DEFAULT_ALTITUDE_UNITS,
        #endif
        .minValue = 0,
        .maxValue = 0,
        .alarm = false,
        .display = true,
        .isEnabled = true,
        .readFunction = altitude_config->readFunction,
        .displayConvert = altitude_config->displayConvert,
        .obdConvert = altitude_config->obdConvert,
        .calibrationData = altitude_config->calibrationData,
        .calibrationType = altitude_config->calibrationType
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