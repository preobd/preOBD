/*
 * sensor_types.h - Sensor Info Structure and Type Definitions
 *
 * Defines the SensorInfo structure used by the sensor library registry,
 * along with pin type requirements and forward declarations for read/init functions.
 */

#ifndef SENSOR_LIBRARY_TYPES_H
#define SENSOR_LIBRARY_TYPES_H

#include <Arduino.h>
#include "../../inputs/input.h"
#include "../sensor_types.h"

// ===== PIN TYPE REQUIREMENT ENUMERATION =====
// Defines what type of pin a sensor requires for operation
// Not needed in static builds where pins are hardcoded
#ifndef USE_STATIC_CONFIG
enum PinTypeRequirement {
    PIN_ANALOG,     // Sensor requires analog pin (uses analogRead)
    PIN_DIGITAL,    // Sensor requires digital pin (uses digitalWrite, digitalRead, interrupts)
    PIN_I2C         // Sensor uses I2C bus (pin field must be "I2C")
};
#else
// Define dummy values for static builds so X_SENSOR macro compiles
// These are accepted as macro parameters but discarded in the expansion
#define PIN_ANALOG   0
#define PIN_DIGITAL  0
#define PIN_I2C      0
#endif

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
#ifndef USE_STATIC_CONFIG
    PinTypeRequirement pinTypeRequirement;  // What type of pin this sensor requires
#endif
};

// ===== FORWARD DECLARATIONS: READ FUNCTIONS =====
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

// ===== FORWARD DECLARATIONS: INIT FUNCTIONS =====
extern void initThermocoupleCS(Input*);
extern void initWPhaseRPM(Input*);
extern void initFloatSwitch(Input*);
extern void initBME280(Input*);
extern void initHallSpeed(Input*);

// ===== FORWARD DECLARATIONS: UNIT CONVERSION =====
extern float convertFromBaseUnits(float baseValue, uint8_t unitsIndex);
extern float convertToBaseUnits(float displayValue, uint8_t unitsIndex);

// ===== FORWARD DECLARATIONS: OBD CONVERSION =====
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

#endif // SENSOR_LIBRARY_TYPES_H
