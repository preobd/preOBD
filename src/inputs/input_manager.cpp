/*
 * input_manager.cpp - Input Configuration & Management Implementation
 * Works with both EEPROM config and compile-time config
 */

#include "../config.h"
#include "../version.h"
#include "input_manager.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include <EEPROM.h>
#include <string.h>

// ===== GLOBAL STATE =====
Input inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;

// ===== EEPROM LAYOUT =====
// EEPROM stores configuration persistently for runtime mode.
// Layout: [Header (8 bytes)] [Input 0] [Input 1] ... [Input N]

#define EEPROM_MAGIC 0x4F454D53            // "OEMS" in ASCII - validates EEPROM has our data
#define EEPROM_HEADER_SIZE sizeof(EEPROMHeader)  // Header size
#define EEPROM_INPUT_SIZE sizeof(Input)    // ~100 bytes per input

struct EEPROMHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t numInputs;
    uint8_t reserved;
};

// ===== STATIC CONFIG GENERATOR (Compile-Time) =====
#ifdef USE_STATIC_CONFIG

// Include custom calibrations for static builds
#include "../advanced_config.h"

/*
 * ============================================================================
 * POPULATE_INPUT Macro System
 * ============================================================================
 *
 * PURPOSE:
 * Converts compile-time config.h definitions into runtime Input structures.
 * This must be a macro (not a function) because we need C preprocessor token
 * pasting (##) to read INPUT_0_PIN, INPUT_1_PIN, etc.
 *
 * HOW IT WORKS:
 * 1. Reads INPUT_N_PIN, INPUT_N_APPLICATION, INPUT_N_SENSOR from config.h
 * 2. Loads default values from ApplicationPreset (flash)
 * 3. Loads sensor info from SensorInfo (flash)
 * 4. Optionally applies INPUT_N_UNITS override if defined
 * 5. Optionally applies INPUT_N_CUSTOM_CALIBRATION if defined
 *
 * THE _POPULATE_UNITS_N PATTERN:
 * We define empty placeholder macros (_POPULATE_UNITS_0, _POPULATE_UNITS_1, etc.)
 * that do nothing by default. If INPUT_N_UNITS is defined in config.h, we
 * #undef and redefine the corresponding _POPULATE_UNITS_N to apply the override.
 * This allows optional per-input unit overrides without #ifdef inside the macro.
 *
 * Same pattern applies to _POPULATE_CUSTOM_CAL_N for calibration overrides.
 *
 * DEBUGGING:
 * If inputs aren't populating correctly:
 * 1. Verify INPUT_N_PIN/APPLICATION/SENSOR are all defined in config.h
 * 2. Check that N matches idx (INPUT_0 -> index 0, INPUT_1 -> index 1)
 * 3. Add Serial.print() statements inside the macro temporarily
 *
 * ============================================================================
 */
#define POPULATE_INPUT(N, idx) \
    do { \
        Input* input = &inputs[idx]; \
        input->pin = INPUT_##N##_PIN; \
        input->application = INPUT_##N##_APPLICATION; \
        input->sensor = INPUT_##N##_SENSOR; \
        input->flags.isEnabled = true; \
        \
        /* Load Application preset from flash */ \
        const ApplicationPreset* flashPreset = getApplicationPreset(INPUT_##N##_APPLICATION); \
        if (flashPreset) { \
            ApplicationPreset preset; \
            loadApplicationPreset(flashPreset, &preset); \
            strncpy(input->abbrName, preset.name, sizeof(input->abbrName) - 1); \
            input->abbrName[sizeof(input->abbrName) - 1] = '\0'; \
            strncpy(input->displayName, preset.displayName, sizeof(input->displayName) - 1); \
            input->displayName[sizeof(input->displayName) - 1] = '\0'; \
            input->displayUnits = preset.defaultUnits; \
            input->minValue = preset.defaultMinValue; \
            input->maxValue = preset.defaultMaxValue; \
            input->obd2pid = preset.obd2pid; \
            input->obd2length = preset.obd2length; \
            input->flags.alarm = preset.defaultAlarmEnabled; \
            input->flags.display = preset.defaultDisplayEnabled; \
            input->flags.useCustomCalibration = false; \
        } \
        \
        /* Load Sensor info from flash */ \
        const SensorInfo* flashInfo = getSensorInfo(INPUT_##N##_SENSOR); \
        if (flashInfo) { \
            SensorInfo info; \
            loadSensorInfo(flashInfo, &info); \
            input->readFunction = info.readFunction; \
            input->measurementType = info.measurementType; \
            input->calibrationType = info.calibrationType; \
            input->presetCalibration = info.defaultCalibration; \
        } \
        \
        /* Check for unit override */ \
        _POPULATE_UNITS_##N(input); \
        \
        /* Check for custom calibration override */ \
        _POPULATE_CUSTOM_CAL_##N(input); \
    } while(0)

// Helper macro to override display units if defined
// This creates a no-op by default, but gets redefined if INPUT_N_UNITS exists
#define _POPULATE_UNITS_0(input)
#define _POPULATE_UNITS_1(input)
#define _POPULATE_UNITS_2(input)
#define _POPULATE_UNITS_3(input)
#define _POPULATE_UNITS_4(input)
#define _POPULATE_UNITS_5(input)
#define _POPULATE_UNITS_6(input)
#define _POPULATE_UNITS_7(input)
#define _POPULATE_UNITS_8(input)
#define _POPULATE_UNITS_9(input)

// Redefine unit override helpers for each input that has INPUT_N_UNITS defined
#ifdef INPUT_0_UNITS
#undef _POPULATE_UNITS_0
#define _POPULATE_UNITS_0(input) (input)->displayUnits = INPUT_0_UNITS
#endif
#ifdef INPUT_1_UNITS
#undef _POPULATE_UNITS_1
#define _POPULATE_UNITS_1(input) (input)->displayUnits = INPUT_1_UNITS
#endif
#ifdef INPUT_2_UNITS
#undef _POPULATE_UNITS_2
#define _POPULATE_UNITS_2(input) (input)->displayUnits = INPUT_2_UNITS
#endif
#ifdef INPUT_3_UNITS
#undef _POPULATE_UNITS_3
#define _POPULATE_UNITS_3(input) (input)->displayUnits = INPUT_3_UNITS
#endif
#ifdef INPUT_4_UNITS
#undef _POPULATE_UNITS_4
#define _POPULATE_UNITS_4(input) (input)->displayUnits = INPUT_4_UNITS
#endif
#ifdef INPUT_5_UNITS
#undef _POPULATE_UNITS_5
#define _POPULATE_UNITS_5(input) (input)->displayUnits = INPUT_5_UNITS
#endif
#ifdef INPUT_6_UNITS
#undef _POPULATE_UNITS_6
#define _POPULATE_UNITS_6(input) (input)->displayUnits = INPUT_6_UNITS
#endif
#ifdef INPUT_7_UNITS
#undef _POPULATE_UNITS_7
#define _POPULATE_UNITS_7(input) (input)->displayUnits = INPUT_7_UNITS
#endif
#ifdef INPUT_8_UNITS
#undef _POPULATE_UNITS_8
#define _POPULATE_UNITS_8(input) (input)->displayUnits = INPUT_8_UNITS
#endif
#ifdef INPUT_9_UNITS
#undef _POPULATE_UNITS_9
#define _POPULATE_UNITS_9(input) (input)->displayUnits = INPUT_9_UNITS
#endif

// Helper macro to load custom calibration if defined
// This creates a no-op by default, but gets redefined for each input
#define _POPULATE_CUSTOM_CAL_0(input)
#define _POPULATE_CUSTOM_CAL_1(input)
#define _POPULATE_CUSTOM_CAL_2(input)
#define _POPULATE_CUSTOM_CAL_3(input)
#define _POPULATE_CUSTOM_CAL_4(input)
#define _POPULATE_CUSTOM_CAL_5(input)
#define _POPULATE_CUSTOM_CAL_6(input)
#define _POPULATE_CUSTOM_CAL_7(input)
#define _POPULATE_CUSTOM_CAL_8(input)
#define _POPULATE_CUSTOM_CAL_9(input)

// Redefine the custom cal helper for INPUT_0 if custom calibration is defined
#ifdef INPUT_0_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_0
#define _POPULATE_CUSTOM_CAL_0(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_0_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_0_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_0_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_0_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_1_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_1
#define _POPULATE_CUSTOM_CAL_1(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_1_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_1_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_1_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_1_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_2_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_2
#define _POPULATE_CUSTOM_CAL_2(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_2_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_2_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_2_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_2_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_3_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_3
#define _POPULATE_CUSTOM_CAL_3(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_3_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_3_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_3_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_3_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_4_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_4
#define _POPULATE_CUSTOM_CAL_4(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_4_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_4_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_4_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_4_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_5_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_5
#define _POPULATE_CUSTOM_CAL_5(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_5_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_5_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_5_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_5_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_6_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_6
#define _POPULATE_CUSTOM_CAL_6(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_6_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_6_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_6_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_6_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_7_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_7
#define _POPULATE_CUSTOM_CAL_7(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_7_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_7_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_7_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_7_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_8_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_8
#define _POPULATE_CUSTOM_CAL_8(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_8_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_8_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_8_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_8_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#ifdef INPUT_9_CUSTOM_CALIBRATION
#undef _POPULATE_CUSTOM_CAL_9
#define _POPULATE_CUSTOM_CAL_9(input) \
    do { \
        (input)->flags.useCustomCalibration = true; \
        if ((input)->calibrationType == CAL_THERMISTOR_STEINHART) { \
            memcpy(&(input)->customCalibration.steinhart, &input_9_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_LINEAR) { \
            memcpy(&(input)->customCalibration.pressureLinear, &input_9_custom_cal, sizeof(PressureLinearCalibration)); \
        } else if ((input)->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
            memcpy(&(input)->customCalibration.pressurePolynomial, &input_9_custom_cal, sizeof(PressurePolynomialCalibration)); \
        } else if ((input)->calibrationType == CAL_RPM) { \
            memcpy(&(input)->customCalibration.rpm, &input_9_custom_cal, sizeof(RPMCalibration)); \
        } \
    } while(0)
#endif

#endif // USE_STATIC_CONFIG

// ===== INITIALIZATION =====
bool initInputManager() {
    // Clear all inputs
    memset(inputs, 0, sizeof(inputs));
    numActiveInputs = 0;

    // Initialize with invalid values
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;  // Invalid pin
        inputs[i].application = APP_NONE;
        inputs[i].sensor = SENSOR_NONE;
    }

#ifdef USE_STATIC_CONFIG
    // ===== COMPILE-TIME CONFIGURATION MODE =====
    // Populate Input array from sensors_config.h
    // This mirrors the serial configuration logic but happens at startup

    Serial.println(F("Initializing from compile-time configuration..."));

    #ifdef INPUT_0_PIN
        POPULATE_INPUT(0, 0);
    #endif
    #ifdef INPUT_1_PIN
        POPULATE_INPUT(1, 1);
    #endif
    #ifdef INPUT_2_PIN
        POPULATE_INPUT(2, 2);
    #endif
    #ifdef INPUT_3_PIN
        POPULATE_INPUT(3, 3);
    #endif
    #ifdef INPUT_4_PIN
        POPULATE_INPUT(4, 4);
    #endif
    #ifdef INPUT_5_PIN
        POPULATE_INPUT(5, 5);
    #endif
    #ifdef INPUT_6_PIN
        POPULATE_INPUT(6, 6);
    #endif
    #ifdef INPUT_7_PIN
        POPULATE_INPUT(7, 7);
    #endif
    #ifdef INPUT_8_PIN
        POPULATE_INPUT(8, 8);
    #endif
    #ifdef INPUT_9_PIN
        POPULATE_INPUT(9, 9);
    #endif
    // Add more if needed (up to MAX_INPUTS)

    // Count active inputs and initialize sensors
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;

            // Call sensor-specific initialization function if it exists
            const SensorInfo* flashInfo = getSensorInfo(inputs[i].sensor);
            if (flashInfo) {
                SensorInfo info;
                loadSensorInfo(flashInfo, &info);
                if (info.initFunction) {
                    info.initFunction(&inputs[i]);
                }
            }
        }
    }

    Serial.print(F("✓ Loaded "));
    Serial.print(numActiveInputs);
    Serial.println(F(" inputs from compile-time config"));
    return true;  // Static config always valid

#else
    // ===== RUNTIME EEPROM CONFIGURATION MODE =====
    // Try to load from EEPROM
    bool eepromLoaded = loadInputConfig();
    if (!eepromLoaded) {
        Serial.println(F("No valid config in EEPROM - starting with blank configuration"));
    }
    return eepromLoaded;
#endif
}

// ===== EEPROM PERSISTENCE =====

/**
 * Calculate XOR checksum of all active inputs
 * Used to detect EEPROM corruption
 */
static uint8_t calculateConfigChecksum() {
    uint8_t checksum = 0;

    for (uint8_t i = 0; i < numActiveInputs; i++) {
        // Find the i-th active input
        uint8_t activeCount = 0;
        for (uint8_t j = 0; j < MAX_INPUTS; j++) {
            if (inputs[j].pin != 0xFF && inputs[j].flags.isEnabled) {
                if (activeCount == i) {
                    // XOR all bytes of this input
                    const uint8_t* data = (const uint8_t*)&inputs[j];
                    for (size_t k = 0; k < sizeof(Input); k++) {
                        checksum ^= data[k];
                    }
                    break;
                }
                activeCount++;
            }
        }
    }

    return checksum;
}

bool saveInputConfig() {
    // Write inputs first (header needs checksum)
    uint16_t addr = EEPROM_HEADER_SIZE;
    uint8_t savedCount = 0;

    for (uint8_t i = 0; i < MAX_INPUTS && savedCount < numActiveInputs; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            EEPROM.put(addr, inputs[i]);
            addr += EEPROM_INPUT_SIZE;
            savedCount++;
        }
    }

    Serial.print(F("✓ Saved "));
    Serial.print(savedCount);
    Serial.println(F(" inputs to EEPROM"));

    // Calculate checksum
    uint8_t checksum = calculateConfigChecksum();

    // Write header with checksum
    EEPROMHeader header;
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.numInputs = numActiveInputs;
    header.reserved = checksum;  // Store checksum in reserved field

    EEPROM.put(0, header);

    Serial.print(F("✓ Checksum: 0x"));
    Serial.println(checksum, HEX);

    return true;
}

bool loadInputConfig() {
    EEPROMHeader header;
    EEPROM.get(0, header);

    // Validate magic number
    if (header.magic != EEPROM_MAGIC) {
        return false;
    }

    // Check version
    if (header.version != EEPROM_VERSION) {
        Serial.println(F("EEPROM version mismatch - ignoring"));
        return false;
    }

    // Clear existing inputs
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;
    }

    // Read inputs
    uint16_t addr = EEPROM_HEADER_SIZE;
    numActiveInputs = header.numInputs;

    if (numActiveInputs > MAX_INPUTS) {
        numActiveInputs = MAX_INPUTS;
    }

    for (uint8_t i = 0; i < numActiveInputs; i++) {
        EEPROM.get(addr, inputs[i]);
        addr += EEPROM_INPUT_SIZE;

        // Re-initialize function pointers and sensor-specific data
        // (Function pointers can't be reliably stored in EEPROM)
        const SensorInfo* flashInfo = getSensorInfo(inputs[i].sensor);
        if (flashInfo) {
            SensorInfo info;
            loadSensorInfo(flashInfo, &info);
            inputs[i].readFunction = info.readFunction;
            inputs[i].measurementType = info.measurementType;
            inputs[i].calibrationType = info.calibrationType;

            // Restore preset calibration pointer if not using custom calibration
            if (!inputs[i].flags.useCustomCalibration) {
                inputs[i].presetCalibration = info.defaultCalibration;
            }

            // Call sensor-specific initialization function if it exists
            if (info.initFunction) {
                info.initFunction(&inputs[i]);
            }
        }
    }

    // Verify checksum
    uint8_t storedChecksum = header.reserved;
    uint8_t calculatedChecksum = calculateConfigChecksum();

    if (storedChecksum != calculatedChecksum) {
        Serial.print(F("ERROR: EEPROM checksum mismatch! Stored: 0x"));
        Serial.print(storedChecksum, HEX);
        Serial.print(F(", Calculated: 0x"));
        Serial.println(calculatedChecksum, HEX);
        Serial.println(F("Configuration may be corrupted. Please reconfigure."));

        // Clear corrupted data
        memset(inputs, 0, sizeof(inputs));
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            inputs[i].pin = 0xFF;
        }
        numActiveInputs = 0;

        return false;
    }

    Serial.print(F("✓ Checksum verified: 0x"));
    Serial.println(storedChecksum, HEX);

    Serial.print(F("✓ Loaded "));
    Serial.print(numActiveInputs);
    Serial.println(F(" inputs from EEPROM"));
    return true;
}

void resetInputConfig() {
    // Clear EEPROM header
    EEPROMHeader header = {0};
    EEPROM.put(0, header);

    // Clear inputs
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;
    }
    numActiveInputs = 0;

    Serial.println(F("Configuration reset"));
}

// ===== HELPER FUNCTIONS =====

/**
 * Find an input configuration by its physical pin number.
 * @param pin  Pin number (use A0, A1, etc. for analog pins)
 * @return Pointer to Input struct, or nullptr if pin not configured
 */
Input* getInputByPin(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return &inputs[i];
        }
    }
    return nullptr;
}

/**
 * Find an input configuration by its array index.
 * @param index  Array index (0 to MAX_INPUTS-1)
 * @return Pointer to Input struct, or nullptr if index out of range
 * @note  Unlike getInputByPin(), this returns even unconfigured slots
 */
Input* getInputByIndex(uint8_t index) {
    if (index < MAX_INPUTS) {
        return &inputs[index];
    }
    return nullptr;
}

/**
 * Get the array index for a given pin number.
 * @param pin  Pin number to search for
 * @return Array index (0 to MAX_INPUTS-1), or 0xFF if not found
 */
uint8_t getInputIndex(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

/**
 * Find the first unused slot in the inputs array.
 * @return Array index of free slot, or 0xFF if array is full
 */
static uint8_t findFreeSlot() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) {
            return i;
        }
    }
    return 0xFF;  // No free slots
}

/**
 * Validate input configuration
 * Checks for:
 * - Pin conflicts (duplicate pin assignments)
 * - Alarm threshold sanity (min < max)
 * Returns true if valid, false if errors found
 */
static bool validateInputConfig(Input* input) {
    if (!input) return false;

    // Check for pin conflicts with other enabled inputs
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        Input* other = &inputs[i];

        // Skip self-comparison and uninitialized slots
        if (other == input || other->pin == 0xFF) {
            continue;
        }

        // Check if another enabled input uses the same pin
        if (other->flags.isEnabled && other->pin == input->pin) {
            Serial.print(F("ERROR: Pin "));
            if (input->pin >= A0) {
                Serial.print(F("A"));
                Serial.print(input->pin - A0);
            } else {
                Serial.print(input->pin);
            }
            Serial.println(F(" already in use"));
            return false;
        }
    }

    // Check alarm threshold sanity (only if alarms enabled)
    if (input->flags.alarm) {
        if (input->minValue >= input->maxValue) {
            Serial.print(F("ERROR: Invalid alarm range ("));
            Serial.print(input->minValue);
            Serial.print(F(" >= "));
            Serial.print(input->maxValue);
            Serial.println(F(")"));
            return false;
        }
    }

    return true;
}

// ===== CONFIGURATION FUNCTIONS =====
bool setInputApplication(uint8_t pin, Application app) {
    // Find or create input
    Input* input = getInputByPin(pin);
    bool isNewInput = (input == nullptr);
    
    if (input == nullptr) {
        uint8_t slot = findFreeSlot();
        if (slot == 0xFF) {
            Serial.print(F("ERROR: No free input slots (max "));
            Serial.print(MAX_INPUTS);
            Serial.println(F(" inputs)"));
            Serial.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
            return false;
        }
        input = &inputs[slot];
        input->pin = pin;
    }

    // Get Application preset from flash
    const ApplicationPreset* flashPreset = getApplicationPreset(app);
    if (flashPreset == nullptr) {
        Serial.println(F("ERROR: Invalid Type"));
        return false;
    }

    // Load entire preset into RAM (cleaner than scattered pgm_read_* calls)
    ApplicationPreset preset;
    loadApplicationPreset(flashPreset, &preset);

    // Apply preset to input
    input->application = preset.application;
    strncpy(input->abbrName, preset.name, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';
    strncpy(input->displayName, preset.displayName, sizeof(input->displayName) - 1);
    input->displayName[sizeof(input->displayName) - 1] = '\0';
    input->sensor = preset.defaultSensor;
    input->displayUnits = preset.defaultUnits;
    
    // CRITICAL: Store min/max in STANDARD UNITS (no conversion!)
    // Preset already has values in Celsius, bar, volts, etc.
    input->minValue = preset.defaultMinValue;
    input->maxValue = preset.defaultMaxValue;
    
    input->obd2pid = preset.obd2pid;
    input->obd2length = preset.obd2length;
    input->flags.alarm = preset.defaultAlarmEnabled;
    input->flags.display = preset.defaultDisplayEnabled;
    input->flags.isEnabled = true;
    input->flags.useCustomCalibration = false;  // Use preset calibration

    // Increment count if this is a new input
    if (isNewInput) {
        numActiveInputs++;
    }

    // Validate configuration before finalizing
    if (!validateInputConfig(input)) {
        // Validation failed - revert changes
        if (isNewInput) {
            input->pin = 0xFF;  // Mark as free
            numActiveInputs--;
        }
        return false;
    }

    // Set up sensor (function pointers + calibration)
    return setInputSensor(pin, input->sensor);
}

bool setInputSensor(uint8_t pin, Sensor sensor) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) {
        Serial.println(F("ERROR: Input not configured"));
        return false;
    }

    // Get Sensor info from flash
    const SensorInfo* flashInfo = getSensorInfo(sensor);
    if (flashInfo == nullptr) {
        Serial.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    // Load entire sensor info into RAM
    SensorInfo info;
    loadSensorInfo(flashInfo, &info);

    // Apply sensor info to input
    input->sensor = sensor;
    input->readFunction = info.readFunction;
    input->measurementType = info.measurementType;
    input->calibrationType = info.calibrationType;

    // Point to calibration in PROGMEM (don't copy to RAM unless custom)
    input->presetCalibration = info.defaultCalibration;
    input->flags.useCustomCalibration = false;

    // Call sensor-specific initialization function if it exists
    if (info.initFunction) {
        info.initFunction(input);
    }

    return true;
}

bool setInputName(uint8_t pin, const char* name) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->abbrName, name, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';
    return true;
}

bool setInputDisplayName(uint8_t pin, const char* displayName) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->displayName, displayName, sizeof(input->displayName) - 1);
    input->displayName[sizeof(input->displayName) - 1] = '\0';
    return true;
}

bool setInputUnits(uint8_t pin, Units units) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->displayUnits = units;
    return true;
}

bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    // Validate range
    if (minValue >= maxValue) {
        Serial.print(F("ERROR: Min alarm ("));
        Serial.print(minValue);
        Serial.print(F(") must be less than max alarm ("));
        Serial.print(maxValue);
        Serial.println(F(")"));
        return false;
    }

    // Store in STANDARD UNITS (caller's responsibility to provide correct units)
    input->minValue = minValue;
    input->maxValue = maxValue;
    return true;
}

bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->obd2pid = pid;
    input->obd2length = length;
    return true;
}

bool enableInput(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.isEnabled = enable;
    return true;
}

bool enableInputAlarm(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.alarm = enable;
    return true;
}

bool enableInputDisplay(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.display = enable;
    return true;
}

bool clearInput(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    memset(input, 0, sizeof(Input));
    input->pin = 0xFF;

    // Recount active inputs
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;
        }
    }

    return true;
}

// ===== CALIBRATION OVERRIDE FUNCTIONS =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.steinhart.bias_resistor = bias;
    input->customCalibration.steinhart.steinhart_a = a;
    input->customCalibration.steinhart.steinhart_b = b;
    input->customCalibration.steinhart.steinhart_c = c;

    return true;
}

bool setInputCalibrationLookup(uint8_t pin, float bias) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.lookup.bias_resistor = bias;

    return true;
}

bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressureLinear.voltage_min = vMin;
    input->customCalibration.pressureLinear.voltage_max = vMax;
    input->customCalibration.pressureLinear.pressure_min = pMin;
    input->customCalibration.pressureLinear.pressure_max = pMax;

    return true;
}

bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressurePolynomial.bias_resistor = bias;
    input->customCalibration.pressurePolynomial.poly_a = a;
    input->customCalibration.pressurePolynomial.poly_b = b;
    input->customCalibration.pressurePolynomial.poly_c = c;

    return true;
}

bool clearInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = false;
    memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

    return true;
}

// ===== RUNTIME =====
void readAllInputs() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled && inputs[i].readFunction != nullptr) {
            inputs[i].readFunction(&inputs[i]);
        }
    }
}

// ===== INFO/LISTING =====
void printInputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) {
        Serial.println(F("Input not configured"));
        return;
    }

    Serial.println(F("Input Configuration:"));
    Serial.print(F("  Pin: "));
    if (pin >= A0) {
        Serial.print(F("A"));
        Serial.println(pin - A0);
    } else {
        Serial.println(pin);
    }
    Serial.print(F("  Name: "));
    Serial.println(input->abbrName);
    Serial.print(F("  Display Name: "));
    Serial.println(input->displayName);
    Serial.print(F("  Application: "));
    Serial.println(input->application);
    Serial.print(F("  Sensor Type: "));
    Serial.println(input->sensor);
    Serial.print(F("  Units: "));
    Serial.println(input->displayUnits);
    Serial.print(F("  Alarm Range: "));
    Serial.print(input->minValue);
    Serial.print(F(" - "));
    Serial.println(input->maxValue);
    Serial.print(F("  Value: "));
    Serial.println(input->value);
    Serial.print(F("  Enabled: "));
    Serial.println(input->flags.isEnabled ? F("Yes") : F("No"));
    Serial.print(F("  Alarm: "));
    Serial.println(input->flags.alarm ? F("Yes") : F("No"));
    Serial.print(F("  Display: "));
    Serial.println(input->flags.display ? F("Yes") : F("No"));
}

void listAllInputs() {
    Serial.println(F("Active Inputs:"));
    bool found = false;

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            found = true;
            Serial.print(F("  "));
            if (inputs[i].pin >= A0) {
                Serial.print(F("A"));
                Serial.print(inputs[i].pin - A0);
            } else {
                Serial.print(inputs[i].pin);
            }
            Serial.print(F(": "));
            Serial.print(inputs[i].abbrName);
            Serial.print(F(" ("));
            Serial.print(inputs[i].displayName);
            Serial.print(F(") = "));
            Serial.print(inputs[i].value);
            Serial.println(inputs[i].displayUnits == CELSIUS ? F("°C") :
                          inputs[i].displayUnits == FAHRENHEIT ? F("°F") :
                          inputs[i].displayUnits == PSI ? F(" PSI") :
                          inputs[i].displayUnits == BAR ? F(" BAR") :
                          inputs[i].displayUnits == VOLTS ? F("V") : F(""));
        }
    }

    if (!found) {
        Serial.println(F("  (none)"));
    }
}

void listApplicationPresets() {
    Serial.println(F("Available Application Presets:"));
    Serial.println(F("  CHT             - Cylinder Head Temp"));
    Serial.println(F("  EGT             - Exhaust Gas Temp"));
    Serial.println(F("  COOLANT_TEMP    - Coolant Temperature"));
    Serial.println(F("  OIL_TEMP        - Oil Temperature"));
    Serial.println(F("  OIL_PRESSURE    - Oil Pressure"));
    Serial.println(F("  PRIMARY_BATTERY - Primary Battery Voltage"));
}

void listSensors() {
    Serial.println(F("Available Sensor Types:"));
    Serial.println(F("  MAX6675"));
    Serial.println(F("  MAX31855"));
    Serial.println(F("  VDO_120C_LOOKUP"));
    Serial.println(F("  VDO_150C_LOOKUP"));
    Serial.println(F("  VDO_120C_STEINHART"));
    Serial.println(F("  VDO_150C_STEINHART"));
    Serial.println(F("  VDO_2BAR"));
    Serial.println(F("  VDO_5BAR"));
    Serial.println(F("  VOLTAGE_DIVIDER"));
    Serial.println(F("  ... (see sensor_library.h for complete list)"));
}