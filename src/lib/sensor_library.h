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
extern void readLinearSensor(Input*);
extern void readVoltageDivider(Input*);
extern void readWPhaseRPM(Input*);
extern void readBME280Temp(Input*);
extern void readBME280Pressure(Input*);
extern void readBME280Humidity(Input*);
extern void readBME280Elevation(Input*);
extern void readDigitalFloatSwitch(Input*);

// Forward declare init functions (sensors that need special initialization)
extern void initThermocoupleCS(Input*);
extern void initWPhaseRPM(Input*);
extern void initFloatSwitch(Input*);
extern void initBME280(Input*);

// Forward declare unit conversion functions (registry-based)
extern float convertFromBaseUnits(float baseValue, uint8_t unitsIndex);
extern float convertToBaseUnits(float displayValue, uint8_t unitsIndex);
extern const char* getUnitStringByIndex(uint8_t unitsIndex);

// Forward declare OBD conversion functions
extern float obdConvertTemperature(float value);
extern float obdConvertPressure(float value);
extern float obdConvertVoltage(float value);
extern float obdConvertRPM(float value);
extern float obdConvertHumidity(float value);
extern float obdConvertElevation(float value);
extern float obdConvertFloatSwitch(float value);

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
static const char PSTR_MAX6675_LABEL[] PROGMEM = "K-Type Thermocouple (MAX6675)";
static const char PSTR_MAX31855[] PROGMEM = "MAX31855";
static const char PSTR_MAX31855_LABEL[] PROGMEM = "K-Type Thermocouple (MAX31855)";
static const char PSTR_VDO_120C_LOOKUP[] PROGMEM = "VDO_120C_LOOKUP";
static const char PSTR_VDO_120C_LOOKUP_LABEL[] PROGMEM = "VDO 120C (Lookup)";
static const char PSTR_VDO_150C_LOOKUP[] PROGMEM = "VDO_150C_LOOKUP";
static const char PSTR_VDO_150C_LOOKUP_LABEL[] PROGMEM = "VDO 150C (Lookup)";
static const char PSTR_VDO_120C_STEINHART[] PROGMEM = "VDO_120C_STEINHART";
static const char PSTR_VDO_120C_STEINHART_LABEL[] PROGMEM = "VDO 120C (Steinhart)";
static const char PSTR_VDO_150C_STEINHART[] PROGMEM = "VDO_150C_STEINHART";
static const char PSTR_VDO_150C_STEINHART_LABEL[] PROGMEM = "VDO 150C (Steinhart)";
static const char PSTR_THERMISTOR_LOOKUP[] PROGMEM = "THERMISTOR_LOOKUP";
static const char PSTR_THERMISTOR_STEINHART[] PROGMEM = "THERMISTOR_STEINHART";
static const char PSTR_GENERIC_BOOST[] PROGMEM = "GENERIC_BOOST";
static const char PSTR_GENERIC_BOOST_LABEL[] PROGMEM = "Generic Boost";
static const char PSTR_GENERIC_TEMP_LINEAR[] PROGMEM = "GENERIC_TEMP_LINEAR";
static const char PSTR_GENERIC_TEMP_LINEAR_LABEL[] PROGMEM = "Generic Linear Temperature";
static const char PSTR_GENERIC_PRESSURE_150PSI[] PROGMEM = "GENERIC_PRESSURE_150PSI";
static const char PSTR_GENERIC_PRESSURE_150PSI_LABEL[] PROGMEM = "Generic 150 PSI Pressure";
static const char PSTR_AEM_30_2130_150[] PROGMEM = "AEM_30_2130_150";
static const char PSTR_AEM_30_2130_150_LABEL[] PROGMEM = "AEM 150 PSI Pressure";
static const char PSTR_MPX4250AP[] PROGMEM = "MPX4250AP";
static const char PSTR_VDO_2BAR[] PROGMEM = "VDO_2BAR";
static const char PSTR_VDO_2BAR_LABEL[] PROGMEM = "VDO 2 Bar";
static const char PSTR_VDO_5BAR[] PROGMEM = "VDO_5BAR";
static const char PSTR_VDO_5BAR_LABEL[] PROGMEM = "VDO 5 Bar";
static const char PSTR_VOLTAGE_DIVIDER[] PROGMEM = "VOLTAGE_DIVIDER";
static const char PSTR_VOLTAGE_DIVIDER_LABEL[] PROGMEM = "Voltage Divider";
static const char PSTR_W_PHASE_RPM[] PROGMEM = "W_PHASE_RPM";
static const char PSTR_W_PHASE_RPM_LABEL[] PROGMEM = "W-Phase RPM";
static const char PSTR_BME280_TEMP[] PROGMEM = "BME280_TEMP";
static const char PSTR_BME280_TEMP_LABEL[] PROGMEM = "BME280 Temperature";
static const char PSTR_BME280_PRESSURE[] PROGMEM = "BME280_PRESSURE";
static const char PSTR_BME280_PRESSURE_LABEL[] PROGMEM = "BME280 Pressure";
static const char PSTR_BME280_HUMIDITY[] PROGMEM = "BME280_HUMIDITY";
static const char PSTR_BME280_HUMIDITY_LABEL[] PROGMEM = "BME280 Humidity";
static const char PSTR_BME280_ELEVATION[] PROGMEM = "BME280_ELEVATION";
static const char PSTR_BME280_ELEVATION_LABEL[] PROGMEM = "BME280 Elevation";
static const char PSTR_FLOAT_SWITCH[] PROGMEM = "FLOAT_SWITCH";
static const char PSTR_FLOAT_SWITCH_LABEL[] PROGMEM = "Float Switch";

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

    // ===== VDO THERMISTORS - LOOKUP =====
    // Index 3: VDO_120C_LOOKUP
    {

        .name = PSTR_VDO_120C_LOOKUP,
        .label = PSTR_VDO_120C_LOOKUP_LABEL,
        .description = nullptr,
        .readFunction = readThermistorLookup,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .defaultCalibration = &vdo120_lookup_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,       // VDO sensor minimum
        .maxValue = 150.0,       // VDO 120°C maximum
        .nameHash = 0xAE3C,  // djb2_hash("VDO_120C_LOOKUP")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 4: VDO_150C_LOOKUP
    {

        .name = PSTR_VDO_150C_LOOKUP,
        .label = PSTR_VDO_150C_LOOKUP_LABEL,
        .description = nullptr,
        .readFunction = readThermistorLookup,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .defaultCalibration = &vdo150_lookup_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = -40.0,       // VDO sensor minimum
        .maxValue = 180.0,       // VDO 150°C maximum
        .nameHash = 0x619F,  // djb2_hash("VDO_150C_LOOKUP")
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

    // ===== GENERIC THERMISTORS (PLACEHOLDERS) =====
    // Index 7: THERMISTOR_LOOKUP (placeholder - not yet implemented)
    {

        .name = PSTR_THERMISTOR_LOOKUP,
        .label = nullptr,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_LOOKUP,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0xF00F,  // djb2_hash("THERMISTOR_LOOKUP")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 8: THERMISTOR_STEINHART (placeholder - not yet implemented)
    {

        .name = PSTR_THERMISTOR_STEINHART,
        .label = nullptr,
        .description = nullptr,
        .readFunction = nullptr,
        .initFunction = nullptr,
        .measurementType = MEASURE_TEMPERATURE,
        .calibrationType = CAL_THERMISTOR_STEINHART,
        .defaultCalibration = nullptr,
        .minReadInterval = 0,
        .minValue = -40.0,
        .maxValue = 150.0,
        .nameHash = 0xC927,  // djb2_hash("THERMISTOR_STEINHART")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== LINEAR TEMPERATURE SENSORS =====
    // Index 9: GENERIC_TEMP_LINEAR
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
    // Index 10: GENERIC_BOOST
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
    // Index 11: GENERIC_PRESSURE_150PSI
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
    // Index 12: AEM_30_2130_150
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
    // Index 13: MPX4250AP
    {

        .name = PSTR_MPX4250AP,
        .label = PSTR_MPX4250AP,
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
    // Index 14: VDO_2BAR
    {

        .name = PSTR_VDO_2BAR,
        .label = PSTR_VDO_2BAR_LABEL,
        .description = nullptr,
        .readFunction = readPressurePolynomial,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo2bar_polynomial_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 2.0,
        .nameHash = 0x1ED4,  // djb2_hash("VDO_2BAR")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },
    // Index 15: VDO_5BAR
    {

        .name = PSTR_VDO_5BAR,
        .label = PSTR_VDO_5BAR_LABEL,
        .description = nullptr,
        .readFunction = readPressurePolynomial,
        .initFunction = nullptr,
        .measurementType = MEASURE_PRESSURE,
        .calibrationType = CAL_PRESSURE_POLYNOMIAL,
        .defaultCalibration = &vdo5bar_polynomial_cal,
        .minReadInterval = SENSOR_READ_INTERVAL_MS,
        .minValue = 0.0,
        .maxValue = 5.0,
        .nameHash = 0xC3F7,  // djb2_hash("VDO_5BAR")
        .pinTypeRequirement = PIN_ANALOG  // Uses analogRead
    },

    // ===== VOLTAGE SENSORS =====
    // Index 16: VOLTAGE_DIVIDER
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
    // Index 17: W_PHASE_RPM
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

    // ===== BME280 SENSORS =====
    // Index 18: BME280_TEMP
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
    // Index 19: BME280_PRESSURE
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
    // Index 20: BME280_HUMIDITY
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
    // Index 21: BME280_ELEVATION
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
    // Index 22: FLOAT_SWITCH
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

// ===== END HELPER FUNCTIONS =====

#endif // SENSOR_LIBRARY_H
