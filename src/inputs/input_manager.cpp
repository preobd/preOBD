/*
 * input_manager.cpp - Input Configuration & Management Implementation
 */

#include <EEPROM.h>
#include <string.h>
 #include "../config.h"
#include "../version.h"
#include "input_manager.h"
#include "alarm_logic.h"
#include "../lib/units_registry.h"
#ifdef USE_STATIC_CONFIG
#include "../lib/generated/application_presets_static.h"
#include "../lib/generated/sensor_library_static.h"
#else
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#endif

// ===== GLOBAL STATE =====
Input inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;

// ===== EEPROM LAYOUT =====
// EEPROM stores configuration persistently for runtime mode.
// Layout: [Header (8 bytes)] [InputEEPROM 0] [InputEEPROM 1] ... [InputEEPROM N]
//
// IMPORTANT: We store hashes (not indices) in EEPROM for stability.
// Registry indices can change when entries are reordered, but hashes remain stable.
// At boot time, we resolve hashes back to current indices.

#define EEPROM_MAGIC 0x4F454D53            // "OEMS" in ASCII - validates EEPROM has our data
#define EEPROM_HEADER_SIZE sizeof(EEPROMHeader)  // Header size
#define EEPROM_INPUT_SIZE sizeof(InputEEPROM)    // ~70 bytes per input (smaller than Input!)

struct EEPROMHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t numInputs;
    uint8_t reserved;
};

// InputEEPROM - Compact struct for EEPROM storage
// Only stores user-configurable fields (not runtime data or function pointers)
// Uses hashes instead of indices for stability across registry reordering
struct InputEEPROM {
    // === Hardware ===
    uint8_t pin;                    // Physical pin

    // === User Configuration (stored as hashes) ===
    char abbrName[8];               // "CHT", "OIL"
    char displayName[32];           // "Cylinder Head Temp"
    uint16_t applicationHash;       // djb2_hash of application name
    uint16_t sensorHash;            // djb2_hash of sensor name
    uint16_t unitsHash;             // djb2_hash of units name

    // === Alarm Thresholds (in STANDARD UNITS) ===
    float minValue;
    float maxValue;

    // === OBDII ===
    uint8_t obd2pid;
    uint8_t obd2length;

    // === Flags ===
    uint8_t flagsByte;              // Packed flags

    // === Calibration ===
    uint8_t calibrationType;
    CalibrationOverride customCalibration;  // 16 bytes
};

// ===== STATIC CONFIG (Compile-Time) =====
#ifdef USE_STATIC_CONFIG

// Include custom calibrations for static builds
#include "../advanced_config.h"

/*
 * ============================================================================
 * Static Config Helper Macros
 * ============================================================================
 *
 * These simple macros use token pasting (##) to read INPUT_N_PIN,
 * INPUT_N_APPLICATION, and INPUT_N_SENSOR from config.h at compile time.
 *
 * The actual configuration logic is handled by the same setInputApplication()
 * and setInputSensor() functions used at runtime - no code duplication!
 *
 * Optional per-input overrides (units, custom calibration) are applied after
 * the main configuration.
 * ============================================================================
 */

// Simple macro to configure an input using the registry-based functions
#define CONFIGURE_INPUT(N, idx) \
    do { \
        uint8_t pin = INPUT_##N##_PIN; \
        setInputApplication(pin, INPUT_##N##_APPLICATION); \
        setInputSensor(pin, INPUT_##N##_SENSOR); \
    } while(0)

// Macro to apply optional unit override if INPUT_N_UNITS is defined
#define APPLY_UNITS_OVERRIDE(N) \
    APPLY_UNITS_OVERRIDE_##N()

// Default: no-op (will be redefined if INPUT_N_UNITS exists)
#define APPLY_UNITS_OVERRIDE_0()
#define APPLY_UNITS_OVERRIDE_1()
#define APPLY_UNITS_OVERRIDE_2()
#define APPLY_UNITS_OVERRIDE_3()
#define APPLY_UNITS_OVERRIDE_4()
#define APPLY_UNITS_OVERRIDE_5()
#define APPLY_UNITS_OVERRIDE_6()
#define APPLY_UNITS_OVERRIDE_7()
#define APPLY_UNITS_OVERRIDE_8()
#define APPLY_UNITS_OVERRIDE_9()

// Redefine for inputs that have unit overrides
#ifdef INPUT_0_UNITS
#undef APPLY_UNITS_OVERRIDE_0
#define APPLY_UNITS_OVERRIDE_0() setInputUnits(INPUT_0_PIN, INPUT_0_UNITS)
#endif
#ifdef INPUT_1_UNITS
#undef APPLY_UNITS_OVERRIDE_1
#define APPLY_UNITS_OVERRIDE_1() setInputUnits(INPUT_1_PIN, INPUT_1_UNITS)
#endif
#ifdef INPUT_2_UNITS
#undef APPLY_UNITS_OVERRIDE_2
#define APPLY_UNITS_OVERRIDE_2() setInputUnits(INPUT_2_PIN, INPUT_2_UNITS)
#endif
#ifdef INPUT_3_UNITS
#undef APPLY_UNITS_OVERRIDE_3
#define APPLY_UNITS_OVERRIDE_3() setInputUnits(INPUT_3_PIN, INPUT_3_UNITS)
#endif
#ifdef INPUT_4_UNITS
#undef APPLY_UNITS_OVERRIDE_4
#define APPLY_UNITS_OVERRIDE_4() setInputUnits(INPUT_4_PIN, INPUT_4_UNITS)
#endif
#ifdef INPUT_5_UNITS
#undef APPLY_UNITS_OVERRIDE_5
#define APPLY_UNITS_OVERRIDE_5() setInputUnits(INPUT_5_PIN, INPUT_5_UNITS)
#endif
#ifdef INPUT_6_UNITS
#undef APPLY_UNITS_OVERRIDE_6
#define APPLY_UNITS_OVERRIDE_6() setInputUnits(INPUT_6_PIN, INPUT_6_UNITS)
#endif
#ifdef INPUT_7_UNITS
#undef APPLY_UNITS_OVERRIDE_7
#define APPLY_UNITS_OVERRIDE_7() setInputUnits(INPUT_7_PIN, INPUT_7_UNITS)
#endif
#ifdef INPUT_8_UNITS
#undef APPLY_UNITS_OVERRIDE_8
#define APPLY_UNITS_OVERRIDE_8() setInputUnits(INPUT_8_PIN, INPUT_8_UNITS)
#endif
#ifdef INPUT_9_UNITS
#undef APPLY_UNITS_OVERRIDE_9
#define APPLY_UNITS_OVERRIDE_9() setInputUnits(INPUT_9_PIN, INPUT_9_UNITS)
#endif

// Macro to apply optional custom calibration if INPUT_N_CUSTOM_CALIBRATION is defined
#define APPLY_CUSTOM_CAL(N) \
    APPLY_CUSTOM_CAL_##N()

// Default: no-op (will be redefined if INPUT_N_CUSTOM_CALIBRATION exists)
#define APPLY_CUSTOM_CAL_0()
#define APPLY_CUSTOM_CAL_1()
#define APPLY_CUSTOM_CAL_2()
#define APPLY_CUSTOM_CAL_3()
#define APPLY_CUSTOM_CAL_4()
#define APPLY_CUSTOM_CAL_5()
#define APPLY_CUSTOM_CAL_6()
#define APPLY_CUSTOM_CAL_7()
#define APPLY_CUSTOM_CAL_8()
#define APPLY_CUSTOM_CAL_9()

// Redefine for inputs that have custom calibrations
#ifdef INPUT_0_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_0
#define APPLY_CUSTOM_CAL_0() \
    do { \
        Input* input = getInputByPin(INPUT_0_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_0_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_0_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_0_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_0_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_1_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_1
#define APPLY_CUSTOM_CAL_1() \
    do { \
        Input* input = getInputByPin(INPUT_1_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_1_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_1_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_1_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_1_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_2_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_2
#define APPLY_CUSTOM_CAL_2() \
    do { \
        Input* input = getInputByPin(INPUT_2_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_2_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_2_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_2_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_2_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_3_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_3
#define APPLY_CUSTOM_CAL_3() \
    do { \
        Input* input = getInputByPin(INPUT_3_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_3_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_3_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_3_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_3_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_4_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_4
#define APPLY_CUSTOM_CAL_4() \
    do { \
        Input* input = getInputByPin(INPUT_4_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_4_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_4_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_4_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_4_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_5_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_5
#define APPLY_CUSTOM_CAL_5() \
    do { \
        Input* input = getInputByPin(INPUT_5_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_5_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_5_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_5_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_5_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_6_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_6
#define APPLY_CUSTOM_CAL_6() \
    do { \
        Input* input = getInputByPin(INPUT_6_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_6_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_6_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_6_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_6_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_7_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_7
#define APPLY_CUSTOM_CAL_7() \
    do { \
        Input* input = getInputByPin(INPUT_7_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_7_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_7_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_7_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_7_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_8_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_8
#define APPLY_CUSTOM_CAL_8() \
    do { \
        Input* input = getInputByPin(INPUT_8_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_8_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_8_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_8_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_8_custom_cal, sizeof(RPMCalibration)); \
            } \
        } \
    } while(0)
#endif

#ifdef INPUT_9_CUSTOM_CALIBRATION
#undef APPLY_CUSTOM_CAL_9
#define APPLY_CUSTOM_CAL_9() \
    do { \
        Input* input = getInputByPin(INPUT_9_PIN); \
        if (input) { \
            input->flags.useCustomCalibration = true; \
            if (input->calibrationType == CAL_THERMISTOR_STEINHART) { \
                memcpy(&input->customCalibration.steinhart, &input_9_custom_cal, sizeof(ThermistorSteinhartCalibration)); \
            } else if (input->calibrationType == CAL_LINEAR) { \
                memcpy(&input->customCalibration.pressureLinear, &input_9_custom_cal, sizeof(LinearCalibration)); \
            } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) { \
                memcpy(&input->customCalibration.pressurePolynomial, &input_9_custom_cal, sizeof(PressurePolynomialCalibration)); \
            } else if (input->calibrationType == CAL_RPM) { \
                memcpy(&input->customCalibration.rpm, &input_9_custom_cal, sizeof(RPMCalibration)); \
            } \
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
        inputs[i].applicationIndex = 0;  // 0 = NONE
        inputs[i].sensorIndex = 0;       // 0 = NONE
    }

#ifdef USE_STATIC_CONFIG
    // ===== COMPILE-TIME CONFIGURATION MODE =====
    // Configure inputs using the registry-based functions
    // This reuses the same runtime configuration logic

    Serial.println(F("Initializing from static configuration..."));

    #ifdef INPUT_0_PIN
        CONFIGURE_INPUT(0, 0);
        APPLY_UNITS_OVERRIDE(0);
        APPLY_CUSTOM_CAL(0);
    #endif
    #ifdef INPUT_1_PIN
        CONFIGURE_INPUT(1, 1);
        APPLY_UNITS_OVERRIDE(1);
        APPLY_CUSTOM_CAL(1);
    #endif
    #ifdef INPUT_2_PIN
        CONFIGURE_INPUT(2, 2);
        APPLY_UNITS_OVERRIDE(2);
        APPLY_CUSTOM_CAL(2);
    #endif
    #ifdef INPUT_3_PIN
        CONFIGURE_INPUT(3, 3);
        APPLY_UNITS_OVERRIDE(3);
        APPLY_CUSTOM_CAL(3);
    #endif
    #ifdef INPUT_4_PIN
        CONFIGURE_INPUT(4, 4);
        APPLY_UNITS_OVERRIDE(4);
        APPLY_CUSTOM_CAL(4);
    #endif
    #ifdef INPUT_5_PIN
        CONFIGURE_INPUT(5, 5);
        APPLY_UNITS_OVERRIDE(5);
        APPLY_CUSTOM_CAL(5);
    #endif
    #ifdef INPUT_6_PIN
        CONFIGURE_INPUT(6, 6);
        APPLY_UNITS_OVERRIDE(6);
        APPLY_CUSTOM_CAL(6);
    #endif
    #ifdef INPUT_7_PIN
        CONFIGURE_INPUT(7, 7);
        APPLY_UNITS_OVERRIDE(7);
        APPLY_CUSTOM_CAL(7);
    #endif
    #ifdef INPUT_8_PIN
        CONFIGURE_INPUT(8, 8);
        APPLY_UNITS_OVERRIDE(8);
        APPLY_CUSTOM_CAL(8);
    #endif
    #ifdef INPUT_9_PIN
        CONFIGURE_INPUT(9, 9);
        APPLY_UNITS_OVERRIDE(9);
        APPLY_CUSTOM_CAL(9);
    #endif
    // Add more if needed (up to MAX_INPUTS)

    // Count active inputs and initialize sensors
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;

            // Call sensor-specific initialization function if it exists
            const SensorInfo* flashInfo = getSensorByIndex(inputs[i].sensorIndex);
            if (flashInfo) {
                SensorInfo info;
                loadSensorInfo(flashInfo, &info);
                if (info.initFunction) {
                    info.initFunction(&inputs[i]);
                }
            }
        }
    }

    // Initialize alarm contexts from application presets
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            // Get preset to load warmup/persist times
            const ApplicationPreset* flashPreset = getApplicationByIndex(inputs[i].applicationIndex);
            if (flashPreset) {
                ApplicationPreset preset;
                loadApplicationPreset(flashPreset, &preset);
                initInputAlarmContext(&inputs[i], millis(), preset.warmupTime_ms, preset.persistTime_ms);
            }
        }
    }

    Serial.print(F("✓ Loaded "));
    Serial.print(numActiveInputs);
    Serial.println(F(" inputs from static config"));
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

    // Read InputEEPROM structs from EEPROM and checksum them
    // This ensures we're checksumming what was actually saved, not runtime data
    uint16_t addr = EEPROM_HEADER_SIZE;
    for (uint8_t i = 0; i < numActiveInputs; i++) {
        InputEEPROM eepromInput;
        EEPROM.get(addr, eepromInput);

        // XOR all bytes of the EEPROM structure
        const uint8_t* data = (const uint8_t*)&eepromInput;
        for (size_t k = 0; k < sizeof(InputEEPROM); k++) {
            checksum ^= data[k];
        }

        addr += EEPROM_INPUT_SIZE;
    }

    return checksum;
}

bool saveInputConfig() {
    // Convert Input structs to InputEEPROM structs (indices → hashes)
    uint16_t addr = EEPROM_HEADER_SIZE;
    uint8_t savedCount = 0;

    for (uint8_t i = 0; i < MAX_INPUTS && savedCount < numActiveInputs; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            InputEEPROM eepromInput;
            memset(&eepromInput, 0, sizeof(InputEEPROM));

            // Copy simple fields
            eepromInput.pin = inputs[i].pin;
            strncpy(eepromInput.abbrName, inputs[i].abbrName, sizeof(eepromInput.abbrName) - 1);
            eepromInput.abbrName[sizeof(eepromInput.abbrName) - 1] = '\0';  // Ensure null termination
            strncpy(eepromInput.displayName, inputs[i].displayName, sizeof(eepromInput.displayName) - 1);
            eepromInput.displayName[sizeof(eepromInput.displayName) - 1] = '\0';  // Ensure null termination
            eepromInput.minValue = inputs[i].minValue;
            eepromInput.maxValue = inputs[i].maxValue;
            eepromInput.obd2pid = inputs[i].obd2pid;
            eepromInput.obd2length = inputs[i].obd2length;
            eepromInput.calibrationType = inputs[i].calibrationType;
            memcpy(&eepromInput.customCalibration, &inputs[i].customCalibration, sizeof(CalibrationOverride));

            // Pack flags into single byte
            eepromInput.flagsByte =
                (inputs[i].flags.isEnabled ? 0x01 : 0) |
                (inputs[i].flags.alarm ? 0x02 : 0) |
                (inputs[i].flags.display ? 0x04 : 0) |
                (inputs[i].flags.useCustomCalibration ? 0x08 : 0);

            // Convert indices to hashes by looking up names in registries
            const ApplicationPreset* appPreset = getApplicationByIndex(inputs[i].applicationIndex);
            if (appPreset) {
                eepromInput.applicationHash = pgm_read_word(&appPreset->nameHash);
            }

            const SensorInfo* sensorInfo = getSensorByIndex(inputs[i].sensorIndex);
            if (sensorInfo) {
                eepromInput.sensorHash = pgm_read_word(&sensorInfo->nameHash);
            }

            const UnitsInfo* unitsInfo = getUnitsByIndex(inputs[i].unitsIndex);
            if (unitsInfo) {
                eepromInput.unitsHash = pgm_read_word(&unitsInfo->nameHash);
            }

            // Write to EEPROM
            EEPROM.put(addr, eepromInput);
            addr += EEPROM_INPUT_SIZE;
            savedCount++;
        }
    }

    Serial.print(F("✓ Saved "));
    Serial.print(savedCount);
    Serial.println(F(" inputs to EEPROM (hash-based)"));

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
        Serial.print(F("EEPROM version mismatch (found "));
        Serial.print(header.version);
        Serial.print(F(", expected "));
        Serial.print(EEPROM_VERSION);
        Serial.println(F(") - ignoring"));
        return false;
    }

    // Clear existing inputs
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;
    }

    // Read inputs from EEPROM and convert hashes → indices
    uint16_t addr = EEPROM_HEADER_SIZE;
    numActiveInputs = header.numInputs;

    if (numActiveInputs > MAX_INPUTS) {
        numActiveInputs = MAX_INPUTS;
    }

    for (uint8_t i = 0; i < numActiveInputs; i++) {
        InputEEPROM eepromInput;
        EEPROM.get(addr, eepromInput);
        addr += EEPROM_INPUT_SIZE;

        // Copy simple fields
        inputs[i].pin = eepromInput.pin;
        strncpy(inputs[i].abbrName, eepromInput.abbrName, sizeof(inputs[i].abbrName));
        inputs[i].abbrName[sizeof(inputs[i].abbrName) - 1] = '\0';  // Ensure null termination
        strncpy(inputs[i].displayName, eepromInput.displayName, sizeof(inputs[i].displayName));
        inputs[i].displayName[sizeof(inputs[i].displayName) - 1] = '\0';  // Ensure null termination
        inputs[i].minValue = eepromInput.minValue;
        inputs[i].maxValue = eepromInput.maxValue;
        inputs[i].obd2pid = eepromInput.obd2pid;
        inputs[i].obd2length = eepromInput.obd2length;
        inputs[i].calibrationType = (CalibrationType)eepromInput.calibrationType;
        memcpy(&inputs[i].customCalibration, &eepromInput.customCalibration, sizeof(CalibrationOverride));

        // Unpack flags from single byte
        inputs[i].flags.isEnabled = (eepromInput.flagsByte & 0x01) != 0;
        inputs[i].flags.alarm = (eepromInput.flagsByte & 0x02) != 0;
        inputs[i].flags.display = (eepromInput.flagsByte & 0x04) != 0;
        inputs[i].flags.useCustomCalibration = (eepromInput.flagsByte & 0x08) != 0;

        // Resolve hashes to current indices
        inputs[i].applicationIndex = getApplicationIndexByHash(eepromInput.applicationHash);
        inputs[i].sensorIndex = getSensorIndexByHash(eepromInput.sensorHash);
        inputs[i].unitsIndex = getUnitsIndexByHash(eepromInput.unitsHash);

        // Re-initialize function pointers and sensor-specific data
        // (Function pointers can't be reliably stored in EEPROM)
        const SensorInfo* flashInfo = getSensorByIndex(inputs[i].sensorIndex);
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
            if (input->pin >= 0xF0) {
                Serial.print(F("I2C"));
            } else if (input->pin >= A0) {
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
bool setInputApplication(uint8_t pin, uint8_t appIndex) {
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
    const ApplicationPreset* flashPreset = getApplicationByIndex(appIndex);
    if (flashPreset == nullptr) {
        Serial.println(F("ERROR: Invalid Type"));
        return false;
    }

    // Load entire preset into RAM (cleaner than scattered pgm_read_* calls)
    ApplicationPreset preset;
    loadApplicationPreset(flashPreset, &preset);

    // Apply preset to input
    input->applicationIndex = appIndex;

    // Read abbreviation and label from PROGMEM pointers
    // (loadApplicationPreset copies the struct, but string pointers still point to PROGMEM)
    strncpy_P(input->abbrName, preset.abbreviation, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';

    if (preset.label) {
        strncpy_P(input->displayName, preset.label, sizeof(input->displayName) - 1);
    } else {
        strncpy_P(input->displayName, preset.name, sizeof(input->displayName) - 1);
    }
    input->displayName[sizeof(input->displayName) - 1] = '\0';

    // Don't set sensorIndex yet - let setInputSensor() do it
    // (This allows sensorChanged check to work correctly)
    uint8_t defaultSensor = preset.defaultSensor;
    input->unitsIndex = preset.defaultUnits;

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

    // Initialize alarm context from preset
    initInputAlarmContext(input, millis(), preset.warmupTime_ms, preset.persistTime_ms);

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
    // This also sets input->sensorIndex
    return setInputSensor(pin, defaultSensor);
}

bool setInputSensor(uint8_t pin, uint8_t sensorIndex) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) {
        Serial.println(F("ERROR: Input not configured"));
        return false;
    }

    // Get Sensor info from flash
    const SensorInfo* flashInfo = getSensorByIndex(sensorIndex);
    if (flashInfo == nullptr) {
        Serial.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    // Load entire sensor info into RAM
    SensorInfo info;
    loadSensorInfo(flashInfo, &info);

    // Check if sensor is actually changing (to avoid redundant init)
    bool sensorChanged = (input->sensorIndex != sensorIndex);

    // Apply sensor info to input
    input->sensorIndex = sensorIndex;
    input->readFunction = info.readFunction;
    input->measurementType = info.measurementType;
    input->calibrationType = info.calibrationType;

    // Point to calibration in PROGMEM (don't copy to RAM unless custom)
    input->presetCalibration = info.defaultCalibration;
    input->flags.useCustomCalibration = false;

    // Call sensor-specific initialization function only if sensor changed
    // (Prevents duplicate init when setting same sensor twice)
    if (sensorChanged && info.initFunction) {
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

bool setInputUnits(uint8_t pin, uint8_t unitsIndex) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->unitsIndex = unitsIndex;
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

    // Validate against sensor capabilities
    const SensorInfo* sensorInfo = getSensorByIndex(input->sensorIndex);
    if (sensorInfo && input->sensorIndex != 0) {  // Skip validation for SENSOR_NONE
        // Check if alarm range exceeds sensor's physical capabilities
        if (minValue < sensorInfo->minValue || maxValue > sensorInfo->maxValue) {
            Serial.print(F("WARNING: Alarm range ("));
            Serial.print(minValue);
            Serial.print(F(" - "));
            Serial.print(maxValue);
            Serial.print(F(") exceeds sensor capability ("));
            Serial.print(sensorInfo->minValue);
            Serial.print(F(" - "));
            Serial.print(sensorInfo->maxValue);

            // Print sensor name for clarity
            char sensorName[32];
            strcpy_P(sensorName, (const char*)sensorInfo->name);
            Serial.print(F(") for "));
            Serial.println(sensorName);

            // Don't fail - allow the user to set it, but warn them
            // This is useful for sensors that might be replaced or for edge cases
        }
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

bool setInputAlarmWarmup(uint8_t pin, uint16_t warmupTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.warmupTime_ms = warmupTime_ms;
    return true;
}

bool setInputAlarmPersist(uint8_t pin, uint16_t persistTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.persistTime_ms = persistTime_ms;
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
    input->customCalibration.pressureLinear.output_min = pMin;
    input->customCalibration.pressureLinear.output_max = pMax;

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
    if (pin >= 0xF0) {
        // Show virtual pin number (I2C:0, I2C:1, etc.)
        Serial.print(F("I2C:"));
        Serial.println(pin - 0xF0);
    } else if (pin >= A0) {
        Serial.print(F("A"));
        Serial.println(pin - A0);
    } else {
        Serial.println(pin);
    }
    Serial.print(F("  Name: "));
    Serial.println(input->abbrName);

    // Print application name from PROGMEM
    Serial.print(F("  Application: "));
    const ApplicationPreset* appPreset = getApplicationByIndex(input->applicationIndex);
    if (appPreset) {
        const char* label = READ_APP_LABEL(appPreset);
        Serial.println((const __FlashStringHelper*)label);
    } else {
        Serial.print(F("UNKNOWN ("));
        Serial.print(input->applicationIndex);
        Serial.println(F(")"));
    }

    // Print sensor name from PROGMEM
    Serial.print(F("  Sensor Type: "));
    const SensorInfo* sensorInfo = getSensorByIndex(input->sensorIndex);
    if (sensorInfo) {
        const char* label = READ_SENSOR_LABEL(sensorInfo);
        Serial.println((const __FlashStringHelper*)label);
    } else {
        Serial.print(F("UNKNOWN ("));
        Serial.print(input->sensorIndex);
        Serial.println(F(")"));
    }
    Serial.print(F("  Units: "));
    Serial.println(input->unitsIndex);
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
            if (inputs[i].pin >= 0xF0) {
                Serial.print(F("I2C:"));
                Serial.print(inputs[i].pin - 0xF0);
            } else if (inputs[i].pin >= A0) {
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
            Serial.print(F(" "));
            Serial.println((__FlashStringHelper*)getUnitStringByIndex(inputs[i].unitsIndex));
        }
    }

    if (!found) {
        Serial.println(F("  (none)"));
    }
}

void listApplicationPresets() {
    Serial.println(F("Available Application Presets:"));
    Serial.println(F("Temperature:"));
    Serial.println(F("  CHT                 - Cylinder Head Temp"));
    Serial.println(F("  EGT                 - Exhaust Gas Temp"));
    Serial.println(F("  COOLANT_TEMP        - Coolant Temperature"));
    Serial.println(F("  OIL_TEMP            - Oil Temperature"));
    Serial.println(F("  TCASE_TEMP          - Transfer Case/Trans Temp"));
    Serial.println(F("  AMBIENT_TEMP        - Outside Air Temp"));
    Serial.println(F("Pressure:"));
    Serial.println(F("  OIL_PRESSURE        - Oil Pressure"));
    Serial.println(F("  BOOST_PRESSURE      - Turbo/Supercharger Boost"));
    Serial.println(F("  FUEL_PRESSURE       - Fuel System Pressure"));
    Serial.println(F("  BAROMETRIC_PRESSURE - Atmospheric Pressure"));
    Serial.println(F("Electrical:"));
    Serial.println(F("  PRIMARY_BATTERY     - Main Battery Voltage"));
    Serial.println(F("  AUXILIARY_BATTERY   - Aux Battery Voltage"));
    Serial.println(F("Other:"));
    Serial.println(F("  COOLANT_LEVEL       - Coolant Level (Float)"));
    Serial.println(F("  HUMIDITY            - Relative Humidity"));
    Serial.println(F("  ELEVATION           - Altitude"));
    Serial.println(F("  ENGINE_RPM          - Engine Speed (W-phase)"));
}

void listSensors() {
    Serial.println(F("Available Sensor Types:"));
    Serial.println(F("Thermocouples:"));
    Serial.println(F("  MAX6675                - K-type via MAX6675 (0-1024°C)"));
    Serial.println(F("  MAX31855               - K-type via MAX31855 (-270-1372°C)"));
    Serial.println(F("Thermistors (VDO):"));
    Serial.println(F("  VDO_120C_LOOKUP        - VDO 120°C (lookup table)"));
    Serial.println(F("  VDO_150C_LOOKUP        - VDO 150°C (lookup table)"));
    Serial.println(F("  VDO_120C_STEINHART     - VDO 120°C (Steinhart-Hart)"));
    Serial.println(F("  VDO_150C_STEINHART     - VDO 150°C (Steinhart-Hart)"));
    Serial.println(F("Thermistors (Generic):"));
    Serial.println(F("  THERMISTOR_LOOKUP      - Generic (requires custom cal)"));
    Serial.println(F("  THERMISTOR_STEINHART   - Generic (requires custom cal)"));
    Serial.println(F("Temperature (Linear - 5V sensors):"));
    Serial.println(F("  GENERIC_TEMP_LINEAR    - 0.5-4.5V linear (-40 to 150°C)"));
    Serial.println(F("Pressure (Linear - 5V sensors):"));
    Serial.println(F("  GENERIC_BOOST          - 0.5-4.5V linear (0-5 bar)"));
    Serial.println(F("  GENERIC_PRESSURE_150PSI - 0.5-4.5V linear (0-150 PSI / 10 bar)"));
    Serial.println(F("  AEM_30_2130_150        - AEM 150 PSI (0-150 PSI / 10 bar)"));
    Serial.println(F("  MPX4250AP              - Freescale/NXP (20-250 kPa)"));
    Serial.println(F("Pressure (VDO Polynomial):"));
    Serial.println(F("  VDO_2BAR               - VDO 2-bar pressure"));
    Serial.println(F("  VDO_5BAR               - VDO 5-bar pressure"));
    Serial.println(F("Other:"));
    Serial.println(F("  VOLTAGE_DIVIDER        - Battery voltage (12V divider)"));
    Serial.println(F("  W_PHASE_RPM            - W-phase alternator RPM"));
    Serial.println(F("  BME280_TEMP            - BME280 temperature (I2C)"));
    Serial.println(F("  BME280_PRESSURE        - BME280 barometric pressure (I2C)"));
    Serial.println(F("  BME280_HUMIDITY        - BME280 relative humidity (I2C)"));
    Serial.println(F("  BME280_ELEVATION       - BME280 altitude (I2C)"));
    Serial.println(F("  FLOAT_SWITCH           - Float/level switch (digital)"));
    Serial.println();
    Serial.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
    Serial.println(F("Note: Use 'I2C' for new I2C sensors, 'I2C:0', 'I2C:1' etc. for existing ones"));
    Serial.println(F("      Examples: SET I2C AMBIENT_TEMP BME280_TEMP  or  INFO I2C:0"));
}