/*
 * sensor_library.h - Hardware Sensor Library
 *
 * Defines hardware-specific sensors (MAX6675, VDO_5BAR, etc.) with their
 * read functions, conversion functions, and calibration data. These live in
 * flash memory and are referenced when setting up inputs.
 */

#ifndef SENSOR_LIBRARY_H
#define SENSOR_LIBRARY_H

#include <Arduino.h>
#include "../inputs/input.h"
#include "sensor_calibration_data.h"

// Forward declare read functions
extern void readMAX6675(Input*);
extern void readMAX31855(Input*);
extern void readThermistorLookup(Input*);
extern void readThermistorSteinhart(Input*);
extern void readPressurePolynomial(Input*);
extern void readPressureLinear(Input*);
extern void readVoltageDivider(Input*);
extern void readWPhaseRPM(Input*);
extern void readBME280Temp(Input*);
extern void readBME280Pressure(Input*);
extern void readBME280Humidity(Input*);
extern void readBME280Elevation(Input*);
extern void readDigitalFloatSwitch(Input*);

// Forward declare conversion functions
extern float convertTemperature(float value, Units targetUnits);
extern float convertPressure(float value, Units targetUnits);
extern float convertVoltage(float value, Units targetUnits);
extern float convertRPM(float value, Units targetUnits);
extern float convertHumidity(float value, Units targetUnits);
extern float convertElevation(float value, Units targetUnits);
extern float convertFloatSwitch(float value, Units targetUnits);

// Forward declare OBD conversion functions
extern float obdConvertTemperature(float value);
extern float obdConvertPressure(float value);
extern float obdConvertVoltage(float value);
extern float obdConvertRPM(float value);
extern float obdConvertHumidity(float value);
extern float obdConvertElevation(float value);
extern float obdConvertFloatSwitch(float value);

// Helper functions to get conversion function pointers from measurement type
// These allow us to derive conversion functions at runtime instead of storing them
typedef float (*DisplayConvertFunc)(float, Units);
typedef float (*ObdConvertFunc)(float);

DisplayConvertFunc getDisplayConvertFunc(MeasurementType type);
ObdConvertFunc getObdConvertFunc(MeasurementType type);

// ===== SENSOR INFO STRUCTURE =====
struct SensorInfo {
    Sensor sensor;
    const char* name;
    void (*readFunction)(Input*);
    MeasurementType measurementType;
    CalibrationType calibrationType;
    const void* defaultCalibration;
};

// ===== SENSOR LIBRARY (PROGMEM - Flash Memory) =====
static const PROGMEM SensorInfo SENSOR_LIBRARY[] = {
    // ===== THERMOCOUPLES =====
    {
        .sensor = MAX6675,
        .name = "K-Type Thermocouple (MAX6675)",
        .readFunction = readMAX6675,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },
    {
        .sensor = MAX31855,
        .name = "K-Type Thermocouple (MAX31855)",
        .readFunction = readMAX31855,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },

    // ===== VDO THERMISTORS - LOOKUP =====
    {
        .sensor = VDO_120C_LOOKUP,
        .name = "VDO 120C (Lookup)",
        .readFunction = readThermistorLookup,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .defaultCalibration = &vdo120_lookup_cal
    },
    {
        .sensor = VDO_150C_LOOKUP,
        .name = "VDO 150C (Lookup)",
        .readFunction = readThermistorLookup,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .defaultCalibration = &vdo150_lookup_cal
    },

    // ===== VDO THERMISTORS - STEINHART =====
    {
        .sensor = VDO_120C_STEINHART,
        .name = "VDO 120C (Steinhart)",
        .readFunction = readThermistorSteinhart,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = &vdo120_steinhart_cal
    },
    {
        .sensor = VDO_150C_STEINHART,
        .name = "VDO 150C (Steinhart)",
        .readFunction = readThermistorSteinhart,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = &vdo150_steinhart_cal
    },

    // ===== PRESSURE SENSORS =====
    {
        .sensor = VDO_2BAR,
        .name = "VDO 2 Bar",
        .readFunction = readPressurePolynomial,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo2bar_polynomial_cal
    },
    {
        .sensor = VDO_5BAR,
        .name = "VDO 5 Bar",
        .readFunction = readPressurePolynomial,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo5bar_polynomial_cal
    },
    {
        .sensor = GENERIC_BOOST,
        .name = "Generic Boost",
        .readFunction = readPressureLinear,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .defaultCalibration = &generic_boost_linear_cal
    },
    {
        .sensor = MPX4250AP,
        .name = "MPX4250AP",
        .readFunction = readPressureLinear,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .defaultCalibration = &mpx4250ap_linear_cal
    },

    // ===== VOLTAGE SENSORS =====
    {
        .sensor = VOLTAGE_DIVIDER,
        .name = "Voltage Divider",
        .readFunction = readVoltageDivider,
        .measurementType = MEASURE_VOLTAGE,
        .calibrationType = CAL_VOLTAGE_DIVIDER,
        .defaultCalibration = nullptr
    },

    // ===== RPM SENSORS =====
    {
        .sensor = W_PHASE_RPM,
        .name = "W-Phase RPM",
        .readFunction = readWPhaseRPM,
        .measurementType = MEASURE_RPM,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },

    // ===== BME280 SENSORS =====
    {
        .sensor = BME280_TEMP,
        .name = "BME280 Temperature",
        .readFunction = readBME280Temp,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },
    {
        .sensor = BME280_PRESSURE,
        .name = "BME280 Pressure",
        .readFunction = readBME280Pressure,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },
    {
        .sensor = BME280_HUMIDITY,
        .name = "BME280 Humidity",
        .readFunction = readBME280Humidity,
        .measurementType = MEASURE_HUMIDITY,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },
    {
        .sensor = BME280_ELEVATION,
        .name = "BME280 Elevation",
        .readFunction = readBME280Elevation,
        .measurementType = MEASURE_ELEVATION,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    },

    // ===== DIGITAL SENSORS =====
    {
        .sensor = FLOAT_SWITCH,
        .name = "Float Switch",
        .readFunction = readDigitalFloatSwitch,
        .measurementType = MEASURE_DIGITAL,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr
    }
};

#define NUM_SENSORS (sizeof(SENSOR_LIBRARY) / sizeof(SensorInfo))

// ===== HELPER FUNCTIONS =====

// Get Sensor info from flash memory
inline const SensorInfo* getSensorInfo(Sensor sensor) {
    for (byte i = 0; i < NUM_SENSORS; i++) {
        if (pgm_read_byte(&SENSOR_LIBRARY[i].sensor) == sensor) {
            return &SENSOR_LIBRARY[i];
        }
    }
    return nullptr;
}

// Load entire sensor info from PROGMEM into RAM (cleaner code)
inline void loadSensorInfo(const SensorInfo* flashInfo, SensorInfo* ramCopy) {
    memcpy_P(ramCopy, flashInfo, sizeof(SensorInfo));
}

#endif // SENSOR_LIBRARY_H
