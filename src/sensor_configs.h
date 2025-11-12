/*
 * sensor_configs.h - Sensor calibration database
 * 
 * This file contains all preset sensor calibrations.
 * Users don't typically edit this - they just pick a sensor ID in config.h
 * 
 * Advanced users can add their own calibrations here or in advanced_config.h
 */

#ifndef SENSOR_CONFIGS_H
#define SENSOR_CONFIGS_H

#include "sensor_types.h"
#include "sensor_library.h"

// Forward declare read functions
extern void readMAX6675(Sensor*);
extern void readMAX31855(Sensor*);
extern void readThermistorLookup(Sensor*);
extern void readThermistorSteinhart(Sensor*);
extern void readPressureLinear(Sensor*);
extern void readPressurePolynomial(Sensor*);
extern void readVoltageDivider(Sensor*);
extern void readVoltageDirect(Sensor*);
extern void readBME280Temp(Sensor*);
extern void readBME280Pressure(Sensor*);
extern void readBME280Humidity(Sensor*);
extern void readBME280Altitude(Sensor*);

// Forward declare conversion functions
extern float convertTemperature(float, DisplayUnits);
extern float convertPressure(float, DisplayUnits);
extern float convertVoltage(float, DisplayUnits);
extern float convertHumidity(float, DisplayUnits);
extern float convertAltitude(float, DisplayUnits);
extern float obdConvertTemp(float);
extern float obdConvertPressure(float);
extern float obdConvertVoltage(float);
extern float obdConvertDirect(float);
extern float obdConvertHumidity(float);
extern float obdConvertAltitude(float);

// ===== VDO 120°C LOOKUP TABLES =====

static const float vdo120_resistance[] = {
    1743.15, 1364.07, 1075.63, 850.09, 676.95, 543.54, 439.29, 356.64, 291.46,
    239.56, 197.29, 161.46, 134.03, 113.96, 97.05, 82.36, 70.12, 59.73, 51.21,
    44.32, 38.47, 33.4, 29.12, 25.53, 22.44, 19.75, 17.44, 15.46, 13.75, 12.26, 10.96
};

static const float vdo120_temperature[] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
    105, 110, 115, 120, 125, 130, 135, 140, 145, 150
};

// ===== VDO 150°C LOOKUP TABLES =====

static const float vdo150_resistance[] = {
    3240.18, 2473.60, 1905.87, 1486.65, 1168.64, 926.71, 739.98, 594.90, 481.53,
    392.57, 322.17, 266.19, 221.17, 184.72, 155.29, 131.38, 112.08, 96.40, 82.96,
    71.44, 61.92, 54.01, 47.24, 41.42, 36.51, 32.38, 28.81, 25.70, 23.0, 20.66, 18.59,
    16.74, 15.11, 13.66, 12.38, 11.25, 10.24
};

static const float vdo150_temperature[] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 105,
    110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175, 180
};

// ===== THERMISTOR LOOKUP CALIBRATIONS =====

static const ThermistorLookupCalibration vdo120_lookup_cal = {
    .bias_resistor = 2200.0,
    .resistance_table = vdo120_resistance,
    .temperature_table = vdo120_temperature,
    .table_size = 31
};

static const ThermistorLookupCalibration vdo150_lookup_cal = {
    .bias_resistor = 2200.0,
    .resistance_table = vdo150_resistance,
    .temperature_table = vdo150_temperature,
    .table_size = 37
};

// ===== THERMISTOR STEINHART-HART CALIBRATIONS =====

// VDO 120°C using Steinhart-Hart (curve-fitted from lookup table)
static const ThermistorSteinhartCalibration vdo120_steinhart_cal = {
    .bias_resistor = 2200.0,
    .steinhart_a = 1.764445997570e-03,
    .steinhart_b = 2.499534389889e-04,
    .steinhart_c = 6.773335597401e-08
};

// VDO 150°C using Steinhart-Hart (curve-fitted from lookup table)
static const ThermistorSteinhartCalibration vdo150_steinhart_cal = {
    .bias_resistor = 2200.0,
    .steinhart_a = 1.591623373219e-03,
    .steinhart_b = 2.659356969556e-04,
    .steinhart_c = -1.610552525653e-07
};

// Generic 10K NTC with β=3950 (very common thermistor)
static const ThermistorSteinhartCalibration generic_10k_3950_cal = {
    .bias_resistor = 10000.0,
    .steinhart_a = 1.129241e-3,
    .steinhart_b = 2.341077e-4,
    .steinhart_c = 8.775468e-8
};

// Generic 10K NTC with β=3435
static const ThermistorSteinhartCalibration generic_10k_3435_cal = {
    .bias_resistor = 10000.0,
    .steinhart_a = 1.468e-3,
    .steinhart_b = 2.383e-4,
    .steinhart_c = 1.007e-7
};

// Generic 10K NTC with β=3380
static const ThermistorSteinhartCalibration generic_10k_3380_cal = {
    .bias_resistor = 10000.0,
    .steinhart_a = 1.536e-3,
    .steinhart_b = 2.393e-4,
    .steinhart_c = 1.031e-7
};

// ===== PRESSURE SENSOR CALIBRATIONS =====

// VDO 5-bar pressure sensor polynomial calibration
// Polynomial: R = -0.3682*P² + 36.465*P + 10.648
// Solved for P using quadratic formula
static const PressurePolynomialCalibration vdo_5bar_cal = {
    .bias_resistor = 2200.0,
    .poly_a = -0.3682,
    .poly_b = 36.465,
    .poly_c = 10.648
};

// VDO 2-bar pressure sensor polynomial calibration  
// Polynomial: R = -3.1515*P² + 93.686*P + 9.6307
static const PressurePolynomialCalibration vdo_2bar_cal = {
    .bias_resistor = 2200.0,
    .poly_a = -3.1515,
    .poly_b = 93.686,
    .poly_c = 9.6307
};

// Generic 0.5-4.5V linear sensor, 0-5 bar range
static const PressureLinearCalibration generic_0_5bar_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .pressure_min = 0.0,
    .pressure_max = 5.0
};

// Generic 0.5-4.5V linear sensor, 0-10 bar range
static const PressureLinearCalibration generic_0_10bar_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .pressure_min = 0.0,
    .pressure_max = 10.0
};

// Generic 0.5-4.5V linear sensor, 0-100 psi range (converted to bar)
static const PressureLinearCalibration generic_0_100psi_cal = {
    .voltage_min = 0.5,
    .voltage_max = 4.5,
    .pressure_min = 0.0,
    .pressure_max = 6.89476  // 100 psi in bar
};

// Freescale (NXP) MPX4250AP (20-250 kPa, 0.2V-4.7V)
static const PressureLinearCalibration mpx4250ap_cal = {
    .voltage_min = 0.2,
    .voltage_max = 4.7,
    .pressure_min = 0.2,    // 20 kPa in bar
    .pressure_max = 2.5     // 250 kPa in bar
};

// ===== SENSOR CONFIGURATION DATABASE =====

typedef struct {
    byte sensorId;
    const char* name;
    SensorType internalType;
    void (*readFunction)(Sensor*);
    float (*displayConvert)(float, DisplayUnits);
    float (*obdConvert)(float);
    CalibrationType calibrationType;
    void* calibrationData;
} SensorConfig;

static const SensorConfig SENSOR_CONFIGS[] = {
    // ===== THERMOCOUPLES =====
    {
        .sensorId = K_TYPE_THERMOCOUPLE_MAX6675,
        .name = "K-Type Thermocouple (MAX6675)",
        .internalType = MAX6675,
        .readFunction = readMAX6675,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertDirect,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    {
        .sensorId = K_TYPE_THERMOCOUPLE_MAX31855,
        .name = "K-Type Thermocouple (MAX31855)",
        .internalType = MAX31855,
        .readFunction = readMAX31855,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertDirect,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    
    // ===== VDO THERMISTORS - LOOKUP METHOD =====
    {
        .sensorId = VDO_120C_LOOKUP,
        .name = "VDO 120°C (Lookup)",
        .internalType = THERMISTOR_LOOKUP,
        .readFunction = readThermistorLookup,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .calibrationData = (void*)&vdo120_lookup_cal
    },
    {
        .sensorId = VDO_150C_LOOKUP,
        .name = "VDO 150°C (Lookup)",
        .internalType = THERMISTOR_LOOKUP,
        .readFunction = readThermistorLookup,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .calibrationData = (void*)&vdo150_lookup_cal
    },
    
    // ===== VDO THERMISTORS - STEINHART METHOD =====
    {
        .sensorId = VDO_120C_STEINHART,
        .name = "VDO 120°C (Steinhart)",
        .internalType = THERMISTOR_STEINHART,
        .readFunction = readThermistorSteinhart,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .calibrationData = (void*)&vdo120_steinhart_cal
    },
    {
        .sensorId = VDO_150C_STEINHART,
        .name = "VDO 150°C (Steinhart)",
        .internalType = THERMISTOR_STEINHART,
        .readFunction = readThermistorSteinhart,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .calibrationData = (void*)&vdo150_steinhart_cal
    },
    
    // ===== GENERIC NTC THERMISTORS =====
    {
        .sensorId = GENERIC_NTC_10K_3950,
        .name = "10K NTC (β=3950)",
        .internalType = THERMISTOR_STEINHART,
        .readFunction = readThermistorSteinhart,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .calibrationData = (void*)&generic_10k_3950_cal
    },
    {
        .sensorId = GENERIC_NTC_10K_3435,
        .name = "10K NTC (β=3435)",
        .internalType = THERMISTOR_STEINHART,
        .readFunction = readThermistorSteinhart,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .calibrationData = (void*)&generic_10k_3435_cal
    },
    {
        .sensorId = GENERIC_NTC_10K_3380,
        .name = "10K NTC (β=3380)",
        .internalType = THERMISTOR_STEINHART,
        .readFunction = readThermistorSteinhart,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .calibrationData = (void*)&generic_10k_3380_cal
    },
    
  
    // ===== PRESSURE SENSORS =====
    // VDO Pressure Sensors - Polynomial Method
    {
        .sensorId = VDO_5BAR_PRESSURE,
        .name = "VDO 5-bar Pressure",
        .internalType = VDO_5BAR,
        .readFunction = readPressurePolynomial,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .calibrationData = (void*)&vdo_5bar_cal
    },
    {
        .sensorId = VDO_2BAR_PRESSURE,
        .name = "VDO 2-bar Pressure",
        .internalType = VDO_2BAR,
        .readFunction = readPressurePolynomial,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .calibrationData = (void*)&vdo_2bar_cal
    },
    
    // Generic Linear Pressure Sensors
    {
        .sensorId = GENERIC_0_5V_5BAR,
        .name = "Generic 0-5 bar",
        .internalType = GENERIC_BOOST,
        .readFunction = readPressureLinear,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .calibrationData = (void*)&generic_0_5bar_cal
    },
    {
        .sensorId = GENERIC_0_5V_10BAR,
        .name = "Generic 0-10 bar",
        .internalType = GENERIC_BOOST,
        .readFunction = readPressureLinear,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .calibrationData = (void*)&generic_0_10bar_cal
    },
    {
        .sensorId = GENERIC_0_5V_100PSI,
        .name = "Generic 0-100 psi",
        .internalType = GENERIC_BOOST,
        .readFunction = readPressureLinear,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .calibrationData = (void*)&generic_0_100psi_cal
    },
    {
        .sensorId = MPX4250AP_PRESSURE,
        .name = "Freescale (NXP) MPX4250AP",
        .internalType = MPX4250AP,
        .readFunction = readPressureLinear,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_PRESSURE_LINEAR,
        .calibrationData = (void*)&mpx4250ap_cal
    },

    // ===== VOLTAGE SENSORS =====
    // Note: These use platform.h voltage divider defaults
    {
        .sensorId = STANDARD_12V_DIVIDER,
        .name = "12V Battery Monitor",
        .internalType = VOLTAGE_DIVIDER,
        .readFunction = readVoltageDivider,
        .displayConvert = convertVoltage,
        .obdConvert = obdConvertVoltage,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    
    // ===== BME280 SENSORS =====
    {
        .sensorId = BME280_AMBIENT_TEMPERATURE,
        .name = "BME280 Temperature",
        .internalType = BME280_TEMP,
        .readFunction = readBME280Temp,
        .displayConvert = convertTemperature,
        .obdConvert = obdConvertTemp,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    {
        .sensorId = BME280_BAROMETRIC_PRESSURE,
        .name = "BME280 Pressure",
        .internalType = BME280_PRESSURE,
        .readFunction = readBME280Pressure,
        .displayConvert = convertPressure,
        .obdConvert = obdConvertPressure,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    {
        .sensorId = BME280_RELATIVE_HUMIDITY,
        .name = "BME280 Humidity",
        .internalType = BME280_HUMIDITY,
        .readFunction = readBME280Humidity,
        .displayConvert = convertHumidity,
        .obdConvert = obdConvertHumidity,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    },
    {
        .sensorId = BME280_ESTIMATED_ALTITUDE,
        .name = "BME280 Altitude",
        .internalType = BME280_ALTITUDE,
        .readFunction = readBME280Altitude,
        .displayConvert = convertAltitude,
        .obdConvert = obdConvertAltitude,
        .calibrationType = CAL_NONE,
        .calibrationData = nullptr
    }
};

static const byte NUM_SENSOR_CONFIGS = sizeof(SENSOR_CONFIGS) / sizeof(SENSOR_CONFIGS[0]);

// ===== HELPER FUNCTION TO GET SENSOR CONFIG =====

inline const SensorConfig* getSensorConfig(byte sensorId) {
    for (byte i = 0; i < NUM_SENSOR_CONFIGS; i++) {
        if (SENSOR_CONFIGS[i].sensorId == sensorId) {
            return &SENSOR_CONFIGS[i];
        }
    }
    return nullptr;  // Sensor ID not found
}

#endif
