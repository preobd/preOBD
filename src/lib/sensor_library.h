/*
 * sensor_library.h - Hardware Sensor Library (Registry Architecture)
 *
 * This file defines the catalog of supported sensors. Each entry contains:
 *   - Primary key name (for lookups)
 *   - Display label (human-readable)
 *   - Read function (how to get data from hardware)
 *   - Measurement type (temperature, pressure, etc.)
 *   - Calibration type and default calibration data
 *   - Physical sensor limits (for validation)
 *
 * HOW TO ADD A NEW SENSOR:
 * 1. Add calibration data to sensor_calibration_data.h (if needed)
 * 2. Add PROGMEM strings for name and label
 * 3. Add SensorInfo entry to SENSOR_LIBRARY[] below with unique name
 * 4. Compute hash: python3 -c "h=5381; s='YOUR_NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
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

// Forward declare read functions
extern void readMAX6675(Input*);
extern void readMAX31855(Input*);
extern void readThermistorLookup(Input*);
extern void readThermistorSteinhart(Input*);
extern void readPressurePolynomial(Input*);
extern void readPressureTable(Input*);
extern void readLinearSensor(Input*);
extern void readVoltageDivider(Input*);
extern void readWPhaseRPM(Input*);
extern void readBME280Temp(Input*);
extern void readBME280Pressure(Input*);
extern void readBME280Humidity(Input*);
extern void readBME280Elevation(Input*);
extern void readDigitalFloatSwitch(Input*);
extern void readHallSpeed(Input*);

// Forward declare init functions (sensors that need special initialization)
extern void initThermocoupleCS(Input*);
extern void initWPhaseRPM(Input*);
extern void initFloatSwitch(Input*);
extern void initBME280(Input*);
extern void initHallSpeed(Input*);

// Forward declare unit conversion functions (registry-based)
extern float convertFromBaseUnits(float baseValue, uint8_t unitsIndex);
extern float convertToBaseUnits(float displayValue, uint8_t unitsIndex);

// Forward declare OBD conversion functions
extern float obdConvertTemperature(float value);
extern float obdConvertPressure(float value);
extern float obdConvertVoltage(float value);
extern float obdConvertRPM(float value);
extern float obdConvertHumidity(float value);
extern float obdConvertElevation(float value);
extern float obdConvertFloatSwitch(float value);
extern float obdConvertSpeed(float value);

// Helper function to get OBD conversion function pointer from measurement type
typedef float (*ObdConvertFunc)(float);

ObdConvertFunc getObdConvertFunc(MeasurementType type);

// ===== PIN TYPE REQUIREMENT ENUMERATION =====
// Defines what type of pin a sensor requires for operation
enum PinTypeRequirement {
    PIN_ANALOG,     // Sensor requires analog pin (uses analogRead)
    PIN_DIGITAL,    // Sensor requires digital pin (uses digitalWrite, digitalRead, interrupts)
    PIN_I2C         // Sensor uses I2C bus (pin field must be "I2C")
};

// ===== SENSOR CATEGORY ENUMERATION =====
// Categories group sensors by technology/calibration type for two-layer selection.
// Derived from calibrationType + measurementType at runtime (no storage overhead).
enum SensorCategory : uint8_t {
    CAT_THERMOCOUPLE = 0,      // K-type thermocouple amplifiers (CAL_NONE + TEMP + PIN_DIGITAL)
    CAT_NTC_THERMISTOR,        // NTC thermistor temperature sensors (CAL_THERMISTOR_*)
    CAT_LINEAR_TEMP,           // Linear temperature sensors (CAL_LINEAR + TEMP)
    CAT_LINEAR_PRESSURE,       // Linear pressure sensors (CAL_LINEAR + PRESSURE)
    CAT_RESISTIVE_PRESSURE,    // Resistive/piezoresistive pressure senders (CAL_PRESSURE_POLYNOMIAL)
    CAT_VOLTAGE,               // Voltage measurement sensors
    CAT_RPM,                   // Engine RPM sensors
    CAT_SPEED,                 // Vehicle speed sensors
    CAT_I2C,                   // I2C bus sensors (BME280, etc.)
    CAT_DIGITAL,               // Digital input sensors (float switch, etc.)
    CAT_COUNT                  // Number of categories
};

// Sensor category info structure for display and lookup
struct SensorCategoryInfo {
    const char* name;          // Primary key: "THERMOCOUPLE", "NTC_THERMISTOR"
    const char* label;         // Display label: "K-Type Thermocouples"
    uint16_t nameHash;         // Precomputed hash for fast lookup
};

// ===== SENSOR INFO STRUCTURE =====
struct SensorInfo {
    const char* name;                // PRIMARY KEY: "MAX6675", "VDO_120C_LOOKUP"
    const char* label;               // Display string: "K-Type Thermocouple (MAX6675)"
    const char* description;         // Help text (nullable)
    void (*readFunction)(Input*);
    void (*initFunction)(Input*);    // Optional: nullptr if no special init needed
    MeasurementType measurementType;
    CalibrationType calibrationType;
    const void* defaultCalibration;
    uint16_t minReadInterval;        // Minimum ms between reads (0 = use global default)
    float minValue;                  // Sensor's physical minimum (in standard units)
    float maxValue;                  // Sensor's physical maximum (in standard units)
    uint16_t nameHash;               // Precomputed djb2_hash(name) for fast lookup
    PinTypeRequirement pinTypeRequirement;  // What type of pin this sensor requires
};

// ===== STRING LITERALS IN PROGMEM =====
static const char PSTR_NONE[] PROGMEM = "NONE";
static const char PSTR_MAX6675[] PROGMEM = "MAX6675";
static const char PSTR_MAX6675_LABEL[] PROGMEM = "K-Type Thermocouple (0-1024°C)";
static const char PSTR_MAX31855[] PROGMEM = "MAX31855";
static const char PSTR_MAX31855_LABEL[] PROGMEM = "K-Type Thermocouple (-270-1372°C)";
static const char PSTR_VDO_120C_TABLE[] PROGMEM = "VDO_120C_TABLE";
static const char PSTR_VDO_120C_TABLE_LABEL[] PROGMEM = "VDO 120C (table)";
static const char PSTR_VDO_150C_TABLE[] PROGMEM = "VDO_150C_TABLE";
static const char PSTR_VDO_150C_TABLE_LABEL[] PROGMEM = "VDO 150C (table)";
static const char PSTR_VDO_120C_STEINHART[] PROGMEM = "VDO_120C_STEINHART";
static const char PSTR_VDO_120C_STEINHART_LABEL[] PROGMEM = "VDO 120C (Steinhart-Hart)";
static const char PSTR_VDO_150C_STEINHART[] PROGMEM = "VDO_150C_STEINHART";
static const char PSTR_VDO_150C_STEINHART_LABEL[] PROGMEM = "VDO 150C (Steinhart-Hart)";
static const char PSTR_NTC_TABLE[] PROGMEM = "GENERIC_NTC_TABLE";
static const char PSTR_NTC_TABLE_LABEL[] PROGMEM = "Generic NTC (custom table)";
static const char PSTR_NTC_STEINHART[] PROGMEM = "GENERIC_NTC_STEINHART";
static const char PSTR_NTC_STEINHART_LABEL[] PROGMEM = "Generic NTC (custom Steinhart-Hart)";
static const char PSTR_NTC_BETA[] PROGMEM = "GENERIC_NTC_BETA";
static const char PSTR_NTC_BETA_LABEL[] PROGMEM = "Generic NTC (custom Beta equation)";
static const char PSTR_GENERIC_BOOST[] PROGMEM = "GENERIC_BOOST";
static const char PSTR_GENERIC_BOOST_LABEL[] PROGMEM = "0.5-4.5V linear (0-5 bar)";
static const char PSTR_GENERIC_TEMP_LINEAR[] PROGMEM = "GENERIC_TEMP_LINEAR";
static const char PSTR_GENERIC_TEMP_LINEAR_LABEL[] PROGMEM = "0.5-4.5V linear (-40 to 150°C)";
static const char PSTR_GENERIC_PRESSURE_150PSI[] PROGMEM = "GENERIC_PRESSURE_150PSI";
static const char PSTR_GENERIC_PRESSURE_150PSI_LABEL[] PROGMEM = "0.5-4.5V linear (0-150 PSI / 10 bar)";
static const char PSTR_AEM_30_2130_150[] PROGMEM = "AEM_30_2130_150";
static const char PSTR_AEM_30_2130_150_LABEL[] PROGMEM = "AEM 150 PSI (0-150 PSI / 10 bar)";
static const char PSTR_MPX4250AP[] PROGMEM = "MPX4250AP";
static const char PSTR_MPX4250AP_LABEL[] PROGMEM = "Freescale/NXP (20-250 kPa)";
static const char PSTR_VDO_2BAR_CURVE[] PROGMEM = "VDO_2BAR_CURVE";
static const char PSTR_VDO_2BAR_CURVE_LABEL[] PROGMEM = "VDO 2 Bar (curve fit)";
static const char PSTR_VDO_5BAR_CURVE[] PROGMEM = "VDO_5BAR_CURVE";
static const char PSTR_VDO_5BAR_CURVE_LABEL[] PROGMEM = "VDO 5 Bar (curve fit)";
static const char PSTR_VDO_2BAR_TABLE[] PROGMEM = "VDO_2BAR_TABLE";
static const char PSTR_VDO_2BAR_TABLE_LABEL[] PROGMEM = "VDO 2 Bar (table)";
static const char PSTR_VDO_5BAR_TABLE[] PROGMEM = "VDO_5BAR_TABLE";
static const char PSTR_VDO_5BAR_TABLE_LABEL[] PROGMEM = "VDO 5 Bar (table)";
static const char PSTR_VOLTAGE_DIVIDER[] PROGMEM = "VOLTAGE_DIVIDER";
static const char PSTR_VOLTAGE_DIVIDER_LABEL[] PROGMEM = "Battery voltage (12V divider)";
static const char PSTR_W_PHASE_RPM[] PROGMEM = "W_PHASE_RPM";
static const char PSTR_W_PHASE_RPM_LABEL[] PROGMEM = "W-phase alternator RPM";
static const char PSTR_BME280_TEMP[] PROGMEM = "BME280_TEMP";
static const char PSTR_BME280_TEMP_LABEL[] PROGMEM = "BME280 temperature (I2C)";
static const char PSTR_BME280_PRESSURE[] PROGMEM = "BME280_PRESSURE";
static const char PSTR_BME280_PRESSURE_LABEL[] PROGMEM = "BME280 barometric pressure (I2C)";
static const char PSTR_BME280_HUMIDITY[] PROGMEM = "BME280_HUMIDITY";
static const char PSTR_BME280_HUMIDITY_LABEL[] PROGMEM = "BME280 relative humidity (I2C)";
static const char PSTR_BME280_ELEVATION[] PROGMEM = "BME280_ELEVATION";
static const char PSTR_BME280_ELEVATION_LABEL[] PROGMEM = "BME280 altitude (I2C)";
static const char PSTR_FLOAT_SWITCH[] PROGMEM = "FLOAT_SWITCH";
static const char PSTR_FLOAT_SWITCH_LABEL[] PROGMEM = "Float/level switch (digital)";
static const char PSTR_HALL_SPEED[] PROGMEM = "HALL_SPEED";
static const char PSTR_HALL_SPEED_LABEL[] PROGMEM = "Hall Effect Speed Sensor";
static const char PSTR_MPX5700AP[] PROGMEM = "MPX5700AP";
static const char PSTR_MPX5700AP_LABEL[] PROGMEM = "Freescale/NXP (15-700 kPa)";

// ===== SENSOR LIBRARY (PROGMEM - Flash Memory) =====
//
// To add a new sensor:
// 1. Add PROGMEM strings for name and label above
// 2. Add SensorInfo entry at END of this array
// 3. Compute nameHash using Python one-liner in header
//
// Placeholder entries (name = nullptr) reserve slots for unimplemented sensors.
//
static const PROGMEM SensorInfo SENSOR_LIBRARY[] = {
    // Index 0: SENSOR_NONE (placeholder)
    {

        .name = PSTR_NONE,
        .label = nullptr,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = 0.0,
        .maxValue = 0.0,
        .nameHash = 0x2F75,  // djb2_hash("NONE")
        .pinTypeRequirement = PIN_ANALOG  // Default for placeholder
    },

    // ===== THERMOCOUPLES =====
    // Index 1: MAX6675
    {

        .name = PSTR_MAX6675,
        .label = PSTR_MAX6675_LABEL,
        .description = nullptr,
        .readFunction = readMAX6675,
        .initFunction = initThermocoupleCS,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = 250,  // MAX6675 needs ~220ms for temperature conversion
        .minValue = 0.0,      // K-type thermocouple minimum
        .maxValue = 1024.0,      // MAX6675 maximum (K-type can go to 1372°C)
        .nameHash = 0x2A23,  // djb2_hash("MAX6675")
        .pinTypeRequirement = PIN_DIGITAL  // Uses SPI CS pin (digitalWrite)
    },
    // Index 2: MAX31855
    {

        .name = PSTR_MAX31855,
        .label = PSTR_MAX31855_LABEL,
        .description = nullptr,
        .readFunction = readMAX31855,
        .initFunction = initThermocoupleCS,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = 100,  // MAX31855 is faster than MAX6675
        .minValue = -200.0,      // K-type thermocouple minimum
        .maxValue = 1350.0,      // MAX31855 maximum
        .nameHash = 0x6B91,  // djb2_hash("MAX31855")
        .pinTypeRequirement = PIN_DIGITAL  // Uses SPI CS pin (digitalWrite)
    },

    // ===== VDO THERMISTORS - TABLE =====
    // Index 3: VDO_120C_TABLE
    {

        .name = PSTR_VDO_120C_TABLE,
        .label = PSTR_VDO_120C_TABLE_LABEL,
        .description = nullptr,
        .readFunction = readThermistorLookup,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_TABLE,
        .defaultCalibration = &vdo120_lookup_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,       // VDO sensor minimum
        .maxValue = 150.0,       // VDO 120°C maximum
        .nameHash = 0x7FEA,  // djb2_hash("VDO_120C_TABLE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 4: VDO_150C_TABLE
    {

        .name = PSTR_VDO_150C_TABLE,
        .label = PSTR_VDO_150C_TABLE_LABEL,
        .description = nullptr,
        .readFunction = readThermistorLookup,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_TABLE,
        .defaultCalibration = &vdo150_lookup_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,       // VDO sensor minimum
        .maxValue = 180.0,       // VDO 150°C maximum
        .nameHash = 0xD2ED,  // djb2_hash("VDO_150C_TABLE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== VDO THERMISTORS - STEINHART =====
    // Index 5: VDO_120C_STEINHART
    {

        .name = PSTR_VDO_120C_STEINHART,
        .label = PSTR_VDO_120C_STEINHART_LABEL,
        .description = nullptr,
        .readFunction = readThermistorSteinhart,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = &vdo120_steinhart_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0x7434,  // djb2_hash("VDO_120C_STEINHART")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 6: VDO_150C_STEINHART
    {

        .name = PSTR_VDO_150C_STEINHART,
        .label = PSTR_VDO_150C_STEINHART_LABEL,
        .description = nullptr,
        .readFunction = readThermistorSteinhart,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = &vdo150_steinhart_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,
        .maxValue = 180.0,
        .nameHash = 0x90B7,  // djb2_hash("VDO_150C_STEINHART")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== GENERIC NTC THERMISTORS (PLACEHOLDERS) =====
    // Index 7: NTC_TABLE (placeholder - not yet implemented)
    {

        .name = PSTR_NTC_TABLE,
        .label = PSTR_NTC_TABLE_LABEL,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_TABLE,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0x482D,  // djb2_hash("GENERIC_NTC_TABLE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 8: NTC_STEINHART (placeholder - not yet implemented)
    {

        .name = PSTR_NTC_STEINHART,
        .label = PSTR_NTC_STEINHART_LABEL,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0xA5F7,  // djb2_hash("GENERIC_NTC_STEINHART")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 9: NTC_BETA (placeholder - not yet implemented)
    {

        .name = PSTR_NTC_BETA,
        .label = PSTR_NTC_BETA_LABEL,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_BETA,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0x1F61,  // djb2_hash("GENERIC_NTC_BETA")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== LINEAR TEMPERATURE SENSORS =====
    // Index 10: GENERIC_TEMP_LINEAR
    {

        .name = PSTR_GENERIC_TEMP_LINEAR,
        .label = PSTR_GENERIC_TEMP_LINEAR_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &generic_temp_linear_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0xDF11,  // djb2_hash("GENERIC_TEMP_LINEAR")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== PRESSURE SENSORS =====
    // Index 11: GENERIC_BOOST
    {

        .name = PSTR_GENERIC_BOOST,
        .label = PSTR_GENERIC_BOOST_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &generic_boost_linear_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -1.0,        // ~1 bar vacuum
        .maxValue = 3.0,         // 3 bar boost
        .nameHash = 0x59C8,  // djb2_hash("GENERIC_BOOST")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 12: GENERIC_PRESSURE_150PSI
    {

        .name = PSTR_GENERIC_PRESSURE_150PSI,
        .label = PSTR_GENERIC_PRESSURE_150PSI_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &generic_pressure_150psi_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 10.34,       // 150 PSI = 10.34 bar
        .nameHash = 0xA67B,  // djb2_hash("GENERIC_PRESSURE_150PSI")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 13: AEM_30_2130_150
    {

        .name = PSTR_AEM_30_2130_150,
        .label = PSTR_AEM_30_2130_150_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &aem_30_2130_150_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 10.34,       // 150 PSI = 10.34 bar
        .nameHash = 0x31B4,  // djb2_hash("AEM_30_2130_150")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 14: MPX4250AP
    {

        .name = PSTR_MPX4250AP,
        .label = PSTR_MPX4250AP_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &mpx4250ap_linear_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.2,         // 20 kPa minimum
        .maxValue = 2.5,         // 250 kPa maximum
        .nameHash = 0xDF76,  // djb2_hash("MPX4250AP")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 15: MPX5700AP
    {
        .name = PSTR_MPX5700AP,
        .label = PSTR_MPX5700AP_LABEL,
        .description = nullptr,
        .readFunction = readLinearSensor,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_LINEAR,
        .defaultCalibration = &mpx5700ap_linear_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.15,        // 15 kPa minimum
        .maxValue = 7.0,         // 700 kPa maximum
        .nameHash = 0xC4B7,  // djb2_hash("MPX5700AP")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 16: VDO_2BAR_CURVE
    {

        .name = PSTR_VDO_2BAR_CURVE,
        .label = PSTR_VDO_2BAR_CURVE_LABEL,
        .description = nullptr,
        .readFunction = readPressurePolynomial,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo2bar_polynomial_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 2.0,
        .nameHash = 0x6FB8,  // djb2_hash("VDO_2BAR_CURVE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 17: VDO_5BAR_CURVE
    {

        .name = PSTR_VDO_5BAR_CURVE,
        .label = PSTR_VDO_5BAR_CURVE_LABEL,
        .description = nullptr,
        .readFunction = readPressurePolynomial,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo5bar_polynomial_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 5.0,
        .nameHash = 0x231B,  // djb2_hash("VDO_5BAR_CURVE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 18: VDO_2BAR_TABLE
    {

        .name = PSTR_VDO_2BAR_TABLE,
        .label = PSTR_VDO_2BAR_TABLE_LABEL,
        .description = nullptr,
        .readFunction = readPressureTable,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_TABLE,
        .defaultCalibration = &vdo2bar_table_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 2.0,
        .nameHash = 0xD35B,  // djb2_hash("VDO_2BAR_TABLE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 19: VDO_5BAR_TABLE
    {

        .name = PSTR_VDO_5BAR_TABLE,
        .label = PSTR_VDO_5BAR_TABLE_LABEL,
        .description = nullptr,
        .readFunction = readPressureTable,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_TABLE,
        .defaultCalibration = &vdo5bar_table_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 5.0,
        .nameHash = 0x86BE,  // djb2_hash("VDO_5BAR_TABLE")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== VOLTAGE SENSORS =====
    // Index 20: VOLTAGE_DIVIDER
    {

        .name = PSTR_VOLTAGE_DIVIDER,
        .label = PSTR_VOLTAGE_DIVIDER_LABEL,
        .description = nullptr,
        .readFunction = readVoltageDivider,
        .initFunction = nullptr,
        .measurementType = MEASURE_VOLTAGE,
        .calibrationType = CAL_VOLTAGE_DIVIDER,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 30.0,        // Typical automotive max
        .nameHash = 0x311D,  // djb2_hash("VOLTAGE_DIVIDER")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== RPM SENSORS =====
    // Index 21: W_PHASE_RPM
    {

        .name = PSTR_W_PHASE_RPM,
        .label = PSTR_W_PHASE_RPM_LABEL,
        .description = nullptr,
        .readFunction = readWPhaseRPM,
        .initFunction = initWPhaseRPM,
        .measurementType = MEASURE_RPM,
        .calibrationType = CAL_RPM,
        .defaultCalibration = &default_rpm_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 10000.0,     // Typical max RPM
        .nameHash = 0x1F3A,  // djb2_hash("W_PHASE_RPM")
        .pinTypeRequirement = PIN_DIGITAL  // Uses digitalPinToInterrupt
    },

    // ===== SPEED SENSORS =====
    // Index 22: HALL_SPEED
    {
        .name = PSTR_HALL_SPEED,
        .label = PSTR_HALL_SPEED_LABEL,
        .description = nullptr,
        .readFunction = readHallSpeed,
        .initFunction = initHallSpeed,
        .measurementType = MEASURE_SPEED,
        .calibrationType = CAL_SPEED,
        .defaultCalibration = &hall_speed_sensor_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 300.0,     // 300 km/h maximum
        .nameHash = 0xB076,    // djb2_hash("HALL_SPEED")
        .pinTypeRequirement = PIN_DIGITAL  // Uses digitalPinToInterrupt
    },

    // ===== BME280 SENSORS =====
    // Index 23: BME280_TEMP
    {

        .name = PSTR_BME280_TEMP,
        .label = PSTR_BME280_TEMP_LABEL,
        .description = nullptr,
        .readFunction = readBME280Temp,
        .initFunction = initBME280,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,       // BME280 spec minimum
        .maxValue = 85.0,        // BME280 spec maximum
        .nameHash = 0x72A8,  // djb2_hash("BME280_TEMP")
        .pinTypeRequirement = PIN_I2C  // Uses I2C bus (pin must be "I2C")
    },
    // Index 24: BME280_PRESSURE
    {

        .name = PSTR_BME280_PRESSURE,
        .label = PSTR_BME280_PRESSURE_LABEL,
        .description = nullptr,
        .readFunction = readBME280Pressure,
        .initFunction = initBME280,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.3,         // 300 hPa minimum
        .maxValue = 1.1,         // 1100 hPa maximum
        .nameHash = 0x454B,  // djb2_hash("BME280_PRESSURE")
        .pinTypeRequirement = PIN_I2C  // Uses I2C bus (pin must be "I2C")
    },
    // Index 25: BME280_HUMIDITY
    {

        .name = PSTR_BME280_HUMIDITY,
        .label = PSTR_BME280_HUMIDITY_LABEL,
        .description = nullptr,
        .readFunction = readBME280Humidity,
        .initFunction = initBME280,
        .measurementType = MEASURE_HUMIDITY,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 100.0,
        .nameHash = 0x381F,  // djb2_hash("BME280_HUMIDITY")
        .pinTypeRequirement = PIN_I2C  // Uses I2C bus (pin must be "I2C")
    },
    // Index 26: BME280_ELEVATION
    {

        .name = PSTR_BME280_ELEVATION,
        .label = PSTR_BME280_ELEVATION_LABEL,
        .description = nullptr,
        .readFunction = readBME280Elevation,
        .initFunction = initBME280,
        .measurementType = MEASURE_ELEVATION,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -500.0,      // Below sea level
        .maxValue = 9000.0,      // ~9000m = ~29500 feet (Mt Everest)
        .nameHash = 0x2619,  // djb2_hash("BME280_ELEVATION")
        .pinTypeRequirement = PIN_I2C  // Uses I2C bus (pin must be "I2C")
    },

    // ===== DIGITAL SENSORS =====
    // Index 27: FLOAT_SWITCH
    {

        .name = PSTR_FLOAT_SWITCH,
        .label = PSTR_FLOAT_SWITCH_LABEL,
        .description = nullptr,
        .readFunction = readDigitalFloatSwitch,
        .initFunction = initFloatSwitch,
        .measurementType = MEASURE_DIGITAL,
        .calibrationType = CAL_NONE,
        .defaultCalibration = nullptr,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,         // Digital: 0 or 1
        .maxValue = 1.0,
        .nameHash = 0xF22C,  // djb2_hash("FLOAT_SWITCH")
        .pinTypeRequirement = PIN_DIGITAL  // Uses digitalRead
    }
};

// Automatically calculate the number of sensors
constexpr uint8_t NUM_SENSORS = sizeof(SENSOR_LIBRARY) / sizeof(SENSOR_LIBRARY[0]);

// ===== SENSOR CATEGORY STRINGS (PROGMEM) =====
static const char PSTR_CAT_THERMOCOUPLE[] PROGMEM = "THERMOCOUPLE";
static const char PSTR_CAT_THERMOCOUPLE_LABEL[] PROGMEM = "K-Type Thermocouples";
static const char PSTR_CAT_NTC_THERMISTOR[] PROGMEM = "NTC_THERMISTOR";
static const char PSTR_CAT_NTC_THERMISTOR_LABEL[] PROGMEM = "NTC Thermistors";
static const char PSTR_CAT_LINEAR_TEMP[] PROGMEM = "LINEAR_TEMP";
static const char PSTR_CAT_LINEAR_TEMP_LABEL[] PROGMEM = "Linear Temperature Sensors";
static const char PSTR_CAT_LINEAR_PRESSURE[] PROGMEM = "LINEAR_PRESSURE";
static const char PSTR_CAT_LINEAR_PRESSURE_LABEL[] PROGMEM = "Linear Pressure Sensors";
static const char PSTR_CAT_RESISTIVE_PRESSURE[] PROGMEM = "RESISTIVE_PRESSURE";
static const char PSTR_CAT_RESISTIVE_PRESSURE_LABEL[] PROGMEM = "Resistive Pressure Senders";
static const char PSTR_CAT_VOLTAGE[] PROGMEM = "VOLTAGE";
static const char PSTR_CAT_VOLTAGE_LABEL[] PROGMEM = "Voltage Sensors";
static const char PSTR_CAT_RPM[] PROGMEM = "RPM";
static const char PSTR_CAT_RPM_LABEL[] PROGMEM = "RPM Sensors";
static const char PSTR_CAT_SPEED[] PROGMEM = "SPEED";
static const char PSTR_CAT_SPEED_LABEL[] PROGMEM = "Speed Sensors";
static const char PSTR_CAT_I2C[] PROGMEM = "I2C";
static const char PSTR_CAT_I2C_LABEL[] PROGMEM = "I2C Bus Sensors";
static const char PSTR_CAT_DIGITAL[] PROGMEM = "DIGITAL";
static const char PSTR_CAT_DIGITAL_LABEL[] PROGMEM = "Digital Input Sensors";

// ===== SENSOR CATEGORY REGISTRY (PROGMEM) =====
// Hash values computed with: python3 -c "h=5381; s='NAME'; [h:=(h<<5)+h+ord(c.upper()) for c in s]; print(f'0x{h&0xFFFF:04X}')"
static const PROGMEM SensorCategoryInfo SENSOR_CATEGORIES[] = {
    { PSTR_CAT_THERMOCOUPLE,       PSTR_CAT_THERMOCOUPLE_LABEL,       0xA69C },  // THERMOCOUPLE
    { PSTR_CAT_NTC_THERMISTOR,     PSTR_CAT_NTC_THERMISTOR_LABEL,     0xC0BA },  // NTC_THERMISTOR
    { PSTR_CAT_LINEAR_TEMP,        PSTR_CAT_LINEAR_TEMP_LABEL,        0x9095 },  // LINEAR_TEMP
    { PSTR_CAT_LINEAR_PRESSURE,    PSTR_CAT_LINEAR_PRESSURE_LABEL,    0x91B8 },  // LINEAR_PRESSURE
    { PSTR_CAT_RESISTIVE_PRESSURE, PSTR_CAT_RESISTIVE_PRESSURE_LABEL, 0xF99B },  // RESISTIVE_PRESSURE
    { PSTR_CAT_VOLTAGE,            PSTR_CAT_VOLTAGE_LABEL,            0x03F7 },  // VOLTAGE
    { PSTR_CAT_RPM,                PSTR_CAT_RPM_LABEL,                0x1A54 },  // RPM
    { PSTR_CAT_SPEED,              PSTR_CAT_SPEED_LABEL,              0xFEF6 },  // SPEED
    { PSTR_CAT_I2C,                PSTR_CAT_I2C_LABEL,                0xF023 },  // I2C
    { PSTR_CAT_DIGITAL,            PSTR_CAT_DIGITAL_LABEL,            0x9803 },  // DIGITAL
};

// ===== HELPER FUNCTIONS =====

// Get Sensor info from flash memory (O(1) direct array indexing)
inline const SensorInfo* getSensorInfo(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    const SensorInfo* info = &SENSOR_LIBRARY[index];
    // Validate entry (check if label is non-null for implemented sensors)
    if (pgm_read_ptr(&info->label) == nullptr) return nullptr;
    return info;
}

// Get Sensor info by index (same as above, different parameter type)
inline const SensorInfo* getSensorByIndex(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    return &SENSOR_LIBRARY[index];
}

// Get Sensor index by name hash (O(n) search)
inline uint8_t getSensorIndexByHash(uint16_t hash) {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        uint16_t sensorHash = pgm_read_word(&SENSOR_LIBRARY[i].nameHash);
        if (sensorHash == hash) {
            return i;
        }
    }
    return 0;  // SENSOR_NONE
}

// Get Sensor index by name (O(n) search)
inline uint8_t getSensorIndexByName(const char* name) {
    if (!name) return 0;
    uint16_t hash = djb2_hash(name);
    return getSensorIndexByHash(hash);
}

// Load entire sensor info from PROGMEM into RAM (cleaner code)
inline void loadSensorInfo(const SensorInfo* flashInfo, SensorInfo* ramCopy) {
    memcpy_P(ramCopy, flashInfo, sizeof(SensorInfo));
}

// Get sensor measurement type from SENSOR_LIBRARY (O(1) direct array indexing)
inline MeasurementType getSensorMeasurementType(uint8_t index) {
    if (index >= NUM_SENSORS) return MEASURE_TEMPERATURE;
    return (MeasurementType)pgm_read_byte(&SENSOR_LIBRARY[index].measurementType);
}

// Helper macros for reading individual fields from PROGMEM
#define READ_SENSOR_NAME(info) ((const char*)pgm_read_ptr(&(info)->name))
#define READ_SENSOR_LABEL(info) ((const char*)pgm_read_ptr(&(info)->label))
#define READ_SENSOR_DESCRIPTION(info) ((const char*)pgm_read_ptr(&(info)->description))
#define READ_SENSOR_MIN_VALUE(info) pgm_read_float(&(info)->minValue)
#define READ_SENSOR_MAX_VALUE(info) pgm_read_float(&(info)->maxValue)

/**
 * Get sensor name by index (reverse lookup for JSON export)
 *
 * @param index  Sensor index (0-NUM_SENSORS)
 * @return       Sensor name string in PROGMEM, or nullptr if invalid
 */
inline const char* getSensorNameByIndex(uint8_t index) {
    if (index >= NUM_SENSORS) return nullptr;
    return READ_SENSOR_NAME(&SENSOR_LIBRARY[index]);
}

// ===== CATEGORY HELPER FUNCTIONS =====

/**
 * Derive sensor category from existing sensor properties.
 * Categories are computed at runtime - no extra storage per sensor.
 *
 * @param sensorIndex  Index into SENSOR_LIBRARY
 * @return             SensorCategory enum value
 */
inline SensorCategory getSensorCategory(uint8_t sensorIndex) {
    if (sensorIndex >= NUM_SENSORS) return CAT_THERMOCOUPLE;

    const SensorInfo* sensor = &SENSOR_LIBRARY[sensorIndex];
    MeasurementType measType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
    CalibrationType calType = (CalibrationType)pgm_read_byte(&sensor->calibrationType);
    PinTypeRequirement pinType = (PinTypeRequirement)pgm_read_byte(&sensor->pinTypeRequirement);

    // I2C sensors are their own category
    if (pinType == PIN_I2C) return CAT_I2C;

    // By measurement type (non-temp/pressure types)
    if (measType == MEASURE_DIGITAL) return CAT_DIGITAL;
    if (measType == MEASURE_RPM) return CAT_RPM;
    if (measType == MEASURE_SPEED) return CAT_SPEED;
    if (measType == MEASURE_VOLTAGE) return CAT_VOLTAGE;
    if (measType == MEASURE_HUMIDITY) return CAT_I2C;    // BME280
    if (measType == MEASURE_ELEVATION) return CAT_I2C;   // BME280

    // Temperature sensors by calibration type
    if (measType == MEASURE_TEMPERATURE) {
        if (calType == CAL_THERMISTOR_TABLE ||
            calType == CAL_THERMISTOR_STEINHART ||
            calType == CAL_THERMISTOR_BETA) {
            return CAT_NTC_THERMISTOR;
        }
        if (calType == CAL_LINEAR) return CAT_LINEAR_TEMP;
        // CAL_NONE temperature sensors with digital pins are thermocouples
        if (calType == CAL_NONE && pinType == PIN_DIGITAL) return CAT_THERMOCOUPLE;
    }

    // Pressure sensors by calibration type
    if (measType == MEASURE_PRESSURE) {
        if (calType == CAL_PRESSURE_POLYNOMIAL || calType == CAL_PRESSURE_TABLE) return CAT_RESISTIVE_PRESSURE;
        if (calType == CAL_LINEAR) return CAT_LINEAR_PRESSURE;
    }

    return CAT_THERMOCOUPLE;  // Default fallback
}

/**
 * Get category info by category enum value
 */
inline const SensorCategoryInfo* getCategoryInfo(SensorCategory cat) {
    if (cat >= CAT_COUNT) return nullptr;
    return &SENSOR_CATEGORIES[cat];
}

/**
 * Get category by name or alias (case-insensitive)
 * Supports aliases: NTC, THERMISTOR -> NTC_THERMISTOR
 *                   TC -> THERMOCOUPLE
 *                   RESISTIVE, PIEZO -> RESISTIVE_PRESSURE
 *
 * @param name  Category name or alias
 * @return      SensorCategory enum, or CAT_COUNT if not found
 */
inline SensorCategory getCategoryByName(const char* name) {
    if (!name) return CAT_COUNT;

    uint16_t hash = djb2_hash(name);

    // Check primary names first
    for (uint8_t i = 0; i < CAT_COUNT; i++) {
        uint16_t catHash = pgm_read_word(&SENSOR_CATEGORIES[i].nameHash);
        if (catHash == hash) {
            return (SensorCategory)i;
        }
    }

    // Check aliases
    // NTC -> NTC_THERMISTOR (hash 0x09CA)
    if (hash == 0x09CA) return CAT_NTC_THERMISTOR;
    // THERMISTOR -> NTC_THERMISTOR (hash 0x4556)
    if (hash == 0x4556) return CAT_NTC_THERMISTOR;
    // TC -> THERMOCOUPLE (hash 0x755C)
    if (hash == 0x755C) return CAT_THERMOCOUPLE;
    // RESISTIVE -> RESISTIVE_PRESSURE (hash 0xA9A3)
    if (hash == 0xA9A3) return CAT_RESISTIVE_PRESSURE;
    // PIEZO -> RESISTIVE_PRESSURE (hash 0xE18C)
    if (hash == 0xE18C) return CAT_RESISTIVE_PRESSURE;

    return CAT_COUNT;  // Not found
}

/**
 * Check if a name matches a measurement type filter (virtual category)
 * Returns the MeasurementType if matched, or -1 if not a measurement filter
 *
 * Supports: TEMPERATURE, PRESSURE, VOLTAGE, RPM, SPEED, HUMIDITY, ELEVATION, DIGITAL
 */
inline int8_t getMeasurementTypeFilter(const char* name) {
    if (!name) return -1;

    uint16_t hash = djb2_hash(name);

    // TEMPERATURE (hash 0x0353)
    if (hash == 0x0353) return MEASURE_TEMPERATURE;
    // PRESSURE (hash 0x233E)
    if (hash == 0x233E) return MEASURE_PRESSURE;
    // Note: VOLTAGE, RPM, SPEED, HUMIDITY, ELEVATION, DIGITAL match category names
    // so they'll be handled by getCategoryByName() first

    return -1;  // Not a measurement filter
}

/**
 * Count sensors in a category
 */
inline uint8_t countSensorsInCategory(SensorCategory cat) {
    uint8_t count = 0;
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) != nullptr) {
            if (getSensorCategory(i) == cat) count++;
        }
    }
    return count;
}

/**
 * Count sensors by measurement type
 */
inline uint8_t countSensorsByMeasurementType(MeasurementType measType) {
    uint8_t count = 0;
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) != nullptr) {
            MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
            if (sensorMeasType == measType) count++;
        }
    }
    return count;
}

/**
 * Find sensor index by category and preset name
 * Used for two-layer SET SENSOR <category> <preset> syntax
 *
 * @param cat     Category to search within
 * @param preset  Sensor name (can be partial match within category)
 * @return        Sensor index, or 0 if not found
 */
inline uint8_t getSensorIndexByCategoryAndName(SensorCategory cat, const char* preset) {
    if (!preset || cat >= CAT_COUNT) return 0;

    uint16_t presetHash = djb2_hash(preset);

    // Search for exact match within category
    for (uint8_t i = 1; i < NUM_SENSORS; i++) {
        const SensorInfo* sensor = &SENSOR_LIBRARY[i];
        if (pgm_read_ptr(&sensor->label) == nullptr) continue;

        if (getSensorCategory(i) == cat) {
            uint16_t sensorHash = pgm_read_word(&sensor->nameHash);
            if (sensorHash == presetHash) {
                return i;
            }
        }
    }

    return 0;  // Not found
}

// Helper macros for category info
#define READ_CATEGORY_NAME(info) ((const char*)pgm_read_ptr(&(info)->name))
#define READ_CATEGORY_LABEL(info) ((const char*)pgm_read_ptr(&(info)->label))

// ===== END HELPER FUNCTIONS =====

#endif // SENSOR_LIBRARY_H
