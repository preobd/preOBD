/*
 * sensors.cpp - Define all sensors with their configurations
 */

#include "sensor_types.h"
#include "sensors.h"
#include "config.h"

// Declare all read functions
extern void readMAX6675(Sensor*);
extern void readMAX31855(Sensor*);
extern void readVDO120(Sensor*);
extern void readVDO150(Sensor*);
extern void readSteinhart(Sensor*);
extern void readVDO5BAR(Sensor*);
extern void readVDO2BAR(Sensor*);
extern void readGenericBoost(Sensor*);
extern void readMPX4250AP(Sensor*);
extern void readVoltageDivider(Sensor*);
extern void readBME280Temp(Sensor*);
extern void readBME280Pressure(Sensor*);

// Declare conversion functions
extern float convertTemperature(float, DisplayUnits);
extern float convertPressure(float, DisplayUnits);
extern float convertVoltage(float, DisplayUnits);
extern float obdConvertTemp(float);
extern float obdConvertPressure(float);
extern float obdConvertVoltage(float);
extern float obdConvertDirect(float);

// ===== SENSOR DEFINITIONS =====

#ifdef ENABLE_CHT
Sensor CHT = {
    .input = CHT_INPUT,
    .obd2pid = 0xC8,
    .obd2length = 1,
    .value = 0,
    .sensorType = MAX6675,
    .abbrName = "CHT",
    .displayName = "Cylinder Head Temperature",
    .displayUnits = CELSIUS,
    .minValue = CHT_MIN,
    .maxValue = CHT_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readMAX6675,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertDirect
};
#endif

#ifdef ENABLE_EGT
Sensor EGT = {
    .input = EGT_INPUT,
    .obd2pid = 0x78,
    .obd2length = 9,
    .value = 0,
    .sensorType = MAX6675,
    .abbrName = "EGT",
    .displayName = "Exhaust Gas Temperature",
    .displayUnits = CELSIUS,
    .minValue = EGT_MIN,
    .maxValue = EGT_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readMAX6675,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertDirect
};
#endif

#ifdef ENABLE_COOLANT_TEMP
Sensor coolantTemp = {
    .input = COOLANT_TEMP_INPUT,
    .obd2pid = 0x05,
    .obd2length = 1,
    .value = 0,
    .sensorType = VDO120,
    .abbrName = "WTR",
    .displayName = "Engine Coolant Temperature",
    .displayUnits = CELSIUS,
    .minValue = COOLANT_TEMP_MIN,
    .maxValue = COOLANT_TEMP_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readVDO120,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif

#ifdef ENABLE_OIL_TEMP
Sensor oilTemp = {
    .input = OIL_TEMP_INPUT,
    .obd2pid = 0x5C,
    .obd2length = 1,
    .value = 0,
    .sensorType = VDO150,
    .abbrName = "OIL",
    .displayName = "Oil Temperature",
    .displayUnits = CELSIUS,
    .minValue = OIL_TEMP_MIN,
    .maxValue = OIL_TEMP_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readVDO150,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif

#ifdef ENABLE_TCASE_TEMP
Sensor tcaseTemp = {
    .input = TCASE_TEMP_INPUT,
    .obd2pid = 0xC9,
    .obd2length = 1,
    .value = 0,
    .sensorType = VDO120,
    .abbrName = "TRANS",
    .displayName = "Transfer Case Oil Temperature",
    .displayUnits = CELSIUS,
    .minValue = TCASE_TEMP_MIN,
    .maxValue = TCASE_TEMP_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readVDO120,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif

#ifdef ENABLE_BOOST_PRESSURE
Sensor boostPressure = {
    .input = BOOST_PRESSURE_INPUT,
    .obd2pid = 0x6F,
    .obd2length = 3,
    .value = 0,
    .sensorType = VDO_2BAR,
    .abbrName = "BST",
    .displayName = "Turbocharger Inlet Pressure",
    .displayUnits = BAR,
    .minValue = BOOST_PRESSURE_MIN,
    .maxValue = BOOST_PRESSURE_MAX,
    .alarm = false,
    .display = true,
    .isEnabled = true,
    .readFunction = readVDO2BAR,
    .displayConvert = convertPressure,
    .obdConvert = obdConvertPressure
};
#endif

#ifdef ENABLE_OIL_PRESSURE
Sensor oilPressure = {
    .input = OIL_PRESSURE_INPUT,
    .obd2pid = 0xCA,
    .obd2length = 1,
    .value = 0,
    .sensorType = VDO_5BAR,
    .abbrName = "OPS",
    .displayName = "Engine Oil Pressure",
    .displayUnits = BAR,
    .minValue = OIL_PRESSURE_MIN,
    .maxValue = OIL_PRESSURE_MAX,
    .alarm = true,
    .display = true,
    .isEnabled = true,
    .readFunction = readVDO5BAR,
    .displayConvert = convertPressure,
    .obdConvert = obdConvertPressure
};
#endif

#ifdef ENABLE_PRIMARY_BATTERY
Sensor primaryBattery = {
    .input = PRIMARY_BATTERY_INPUT,
    .obd2pid = 0xCB,
    .obd2length = 1,
    .value = 0,
    .sensorType = VOLTAGE_DIVIDER,
    .abbrName = "BAT",
    .displayName = "Primary Battery Voltage",
    .displayUnits = VOLTS,
    .minValue = 0,
    .maxValue = 0,
    .alarm = false,
    .display = true,
    .isEnabled = true,
    .readFunction = readVoltageDivider,
    .displayConvert = convertVoltage,
    .obdConvert = obdConvertVoltage
};
#endif

#ifdef ENABLE_AUXILIARY_BATTERY
Sensor auxiliaryBattery = {
    .input = SECONDARY_BATTERY_INPUT,
    .obd2pid = 0xCC,
    .obd2length = 1,
    .value = 0,
    .sensorType = VOLTAGE_DIVIDER,
    .abbrName = "AUX",
    .displayName = "Secondary Battery Voltage",
    .displayUnits = VOLTS,
    .minValue = 0,
    .maxValue = 0,
    .alarm = false,
    .display = true,
    .isEnabled = true,
    .readFunction = readVoltageDivider,
    .displayConvert = convertVoltage,
    .obdConvert = obdConvertVoltage
};
#endif

#ifdef ENABLE_AMBIENT_TEMP
Sensor ambientAirTemp = {
    .input = 0,
    .obd2pid = 0x46,
    .obd2length = 1,
    .value = 0,
    .sensorType = BME280_TEMP,
    .abbrName = "AMB",
    .displayName = "Ambient Air Temperature",
    .displayUnits = CELSIUS,
    .minValue = 0,
    .maxValue = 0,
    .alarm = false,
    .display = true,
    .isEnabled = true,
    .readFunction = readBME280Temp,
    .displayConvert = convertTemperature,
    .obdConvert = obdConvertTemp
};
#endif

#ifdef ENABLE_BAROMETRIC_PRESSURE
Sensor absBarPressure = {
    .input = 0,
    .obd2pid = 0x33,
    .obd2length = 1,
    .value = 0,
    .sensorType = BME280_PRESSURE,
    .abbrName = "ATM",
    .displayName = "Absolute Barometric Pressure",
    .displayUnits = KPA,
    .minValue = 0,
    .maxValue = 0,
    .alarm = false,
    .display = true,
    .isEnabled = true,
    .readFunction = readBME280Pressure,
    .displayConvert = convertPressure,
    .obdConvert = obdConvertPressure
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
};

// Export the number of sensors
const int numSensors = sizeof(sensors) / sizeof(sensors[0]);
