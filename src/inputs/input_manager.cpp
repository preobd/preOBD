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
#include "../lib/message_router.h"  // For msg.control
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#ifdef USE_STATIC_CONFIG
#include "../lib/generated/application_presets_static.h"
#include "../lib/generated/sensor_library_static.h"
#else
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#endif
#include "../lib/pin_registry.h"

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

    msg.control.println(F("Initializing from static configuration..."));

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

    msg.control.print(F("✓ Loaded "));
    msg.control.print(numActiveInputs);
    msg.control.println(F(" inputs from static config"));
    return true;  // Static config always valid

#else
    // ===== RUNTIME EEPROM CONFIGURATION MODE =====
    // Try to load from EEPROM
    bool eepromLoaded = loadInputConfig();
    if (!eepromLoaded) {
        msg.debug.info(TAG_CONFIG, "No valid config in EEPROM - starting with blank configuration");
    }
    return eepromLoaded;
#endif
}

// ===== EEPROM PERSISTENCE =====

#ifndef USE_STATIC_CONFIG

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

    msg.control.print(F("✓ Saved "));
    msg.control.print(savedCount);
    msg.control.println(F(" inputs to EEPROM (hash-based)"));

    // Calculate checksum
    uint8_t checksum = calculateConfigChecksum();

    // Write header with checksum
    EEPROMHeader header;
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.numInputs = numActiveInputs;
    header.reserved = checksum;  // Store checksum in reserved field

    EEPROM.put(0, header);

    msg.debug.debug(TAG_CONFIG, "Checksum: 0x%02X", checksum);

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
        msg.debug.warn(TAG_CONFIG, "EEPROM version mismatch (found %d, expected %d) - ignoring", header.version, EEPROM_VERSION);
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
        msg.control.println(F("ERROR: EEPROM checksum mismatch! Configuration corrupted."));
        msg.control.println(F("Please reconfigure inputs and run SAVE."));
        msg.debug.error(TAG_CONFIG, "Checksum mismatch: Stored 0x%02X, Calculated 0x%02X", storedChecksum, calculatedChecksum);

        // Clear corrupted data
        memset(inputs, 0, sizeof(inputs));
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            inputs[i].pin = 0xFF;
        }
        numActiveInputs = 0;

        return false;
    }

    msg.debug.debug(TAG_CONFIG, "Checksum verified: 0x%02X", storedChecksum);
    msg.debug.info(TAG_CONFIG, "Loaded %d inputs from EEPROM", numActiveInputs);
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

    msg.control.println(F("Configuration reset"));
}

#endif // USE_STATIC_CONFIG

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

    // Check if pin is reserved by a bus (I2C, SPI, CAN)
    // Skip this check for virtual pins (I2C sensors use 0xF0+)
    if (input->pin < 0xF0 && !isPinAvailable(input->pin)) {
        msg.control.print(F("ERROR: Pin "));
        if (input->pin >= A0) {
            msg.control.print(F("A"));
            msg.control.print(input->pin - A0);
        } else {
            msg.control.print(input->pin);
        }
        msg.control.println(F(" is reserved by a bus (I2C/SPI/CAN)"));
        return false;
    }

    // Check for pin conflicts with other enabled inputs
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        Input* other = &inputs[i];

        // Skip self-comparison and uninitialized slots
        if (other == input || other->pin == 0xFF) {
            continue;
        }

        // Check if another enabled input uses the same pin
        if (other->flags.isEnabled && other->pin == input->pin) {
            msg.control.print(F("ERROR: Pin "));
            if (input->pin >= 0xF0) {
                msg.control.print(F("I2C"));
            } else if (input->pin >= A0) {
                msg.control.print(F("A"));
                msg.control.print(input->pin - A0);
            } else {
                msg.control.print(input->pin);
            }
            msg.control.println(F(" already in use"));
            return false;
        }
    }

    // Check alarm threshold sanity (only if alarms enabled)
    if (input->flags.alarm) {
        if (input->minValue >= input->maxValue) {
            msg.control.print(F("ERROR: Invalid alarm range ("));
            msg.control.print(input->minValue);
            msg.control.print(F(" >= "));
            msg.control.print(input->maxValue);
            msg.control.println(F(")"));
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
            msg.control.print(F("ERROR: No free input slots (max "));
            msg.control.print(MAX_INPUTS);
            msg.control.println(F(" inputs)"));
            msg.control.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
            return false;
        }
        input = &inputs[slot];
        input->pin = pin;
    }

    // Get Application preset from flash
    const ApplicationPreset* flashPreset = getApplicationByIndex(appIndex);
    if (flashPreset == nullptr) {
        msg.control.println(F("ERROR: Invalid Type"));
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
        msg.control.println(F("ERROR: Input not configured"));
        return false;
    }

    // Get Sensor info from flash
    const SensorInfo* flashInfo = getSensorByIndex(sensorIndex);
    if (flashInfo == nullptr) {
        msg.control.println(F("ERROR: Invalid Sensor Type"));
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

    // Validate range (allow both to be 0 for disabled alarms)
    if (minValue >= maxValue && !(minValue == 0 && maxValue == 0)) {
        msg.control.print(F("ERROR: Min alarm ("));
        msg.control.print(minValue);
        msg.control.print(F(") must be less than max alarm ("));
        msg.control.print(maxValue);
        msg.control.println(F(")"));
        return false;
    }

    // Validate against sensor capabilities
    const SensorInfo* sensorInfo = getSensorByIndex(input->sensorIndex);
    if (sensorInfo && input->sensorIndex != 0) {  // Skip validation for SENSOR_NONE
        // Check if alarm range exceeds sensor's physical capabilities
        if (minValue < sensorInfo->minValue || maxValue > sensorInfo->maxValue) {
            msg.control.print(F("WARNING: Alarm range ("));
            msg.control.print(minValue);
            msg.control.print(F(" - "));
            msg.control.print(maxValue);
            msg.control.print(F(") exceeds sensor capability ("));
            msg.control.print(sensorInfo->minValue);
            msg.control.print(F(" - "));
            msg.control.print(sensorInfo->maxValue);

            // Print sensor name for clarity
            char sensorName[32];
            strcpy_P(sensorName, (const char*)sensorInfo->name);
            msg.control.print(F(") for "));
            msg.control.println(sensorName);

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

#ifndef USE_STATIC_CONFIG

void readAllInputs() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled && inputs[i].readFunction != nullptr) {
            inputs[i].readFunction(&inputs[i]);
        }
    }
}

// ===== INFO/LISTING =====

// Helper to print alarm state
static void printAlarmState(AlarmState state) {
    switch (state) {
        case ALARM_DISABLED: msg.control.print(F("DISABLED")); break;
        case ALARM_INIT: msg.control.print(F("INIT")); break;
        case ALARM_WARMUP: msg.control.print(F("WARMUP")); break;
        case ALARM_READY: msg.control.print(F("READY")); break;
        case ALARM_ACTIVE: msg.control.print(F("ACTIVE")); break;
        default: msg.control.print(F("UNKNOWN")); break;
    }
}

// Helper to print pin name (A0, 1, I2C:0, etc)
static void printPin(uint8_t pin) {
    if (pin >= 0xF0) {
        msg.control.print(F("I2C:"));
        msg.control.print(pin - 0xF0);
    } else if (pin >= A0) {
        msg.control.print(F("A"));
        msg.control.print(pin - A0);
    } else {
        msg.control.print(pin);
    }
}

void printInputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Input Info ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  Enabled: "));
    msg.control.println(input->flags.isEnabled ? F("YES") : F("NO"));

    msg.control.print(F("  Application: "));
    msg.control.println(getApplicationNameByIndex(input->applicationIndex));

    msg.control.print(F("  Sensor: "));
    msg.control.println(getSensorNameByIndex(input->sensorIndex));

    msg.control.print(F("  Display Name: '"));
    msg.control.print(input->displayName);
    msg.control.println(F("'"));

    msg.control.print(F("  Short Name: '"));
    msg.control.print(input->abbrName);
    msg.control.println(F("'"));

    msg.control.print(F("  Units: "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.print(F("  Alarm Status: "));
    printAlarmState(input->alarmContext.state);
    msg.control.println();

    msg.control.print(F("  Current Value: "));
    msg.control.print(input->value, 2);
    msg.control.print(F(" "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.println();
    msg.control.println(F("To see alarm config: INFO <pin> ALARM"));
    msg.control.println(F("To see calibration:  INFO <pin> CALIBRATION"));
    msg.control.println();
}

void printInputAlarmInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Alarm Info ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  Enabled: "));
    msg.control.println(input->flags.alarm ? F("YES") : F("NO"));

    msg.control.print(F("  State: "));
    printAlarmState(input->alarmContext.state);
    msg.control.println();

    msg.control.print(F("  Min Threshold: "));
    msg.control.println(input->minValue, 2);
    msg.control.print(F("  Max Threshold: "));
    msg.control.println(input->maxValue, 2);

    msg.control.print(F("  Warmup Time: "));
    msg.control.print(input->alarmContext.warmupTime_ms);
    msg.control.println(F(" ms"));

    msg.control.print(F("  Persistence Time: "));
    msg.control.print(input->alarmContext.persistTime_ms);
    msg.control.println(F(" ms"));

    msg.control.print(F("  Time in State: "));
    extern unsigned long millis(void);
    msg.control.print(millis() - input->alarmContext.stateEntryTime);
    msg.control.println(F(" ms"));

    msg.control.println();
}

void printInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Calibration ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();
    msg.control.print(F("  Type: "));
    if (!input->flags.useCustomCalibration) {
        msg.control.println(F("Preset (PROGMEM)"));
    } else if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
        msg.control.println(F("Steinhart-Hart Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.steinhart.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_c, 10);
    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
        msg.control.println(F("Beta Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.beta.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  Beta: "));
        msg.control.println(input->customCalibration.beta.beta, 1);
        msg.control.print(F("  R0: "));
        msg.control.print(input->customCalibration.beta.r0, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  T0: "));
        msg.control.print(input->customCalibration.beta.t0, 2);
        msg.control.println(F(" K"));
    } else if (input->calibrationType == CAL_LINEAR) {
        msg.control.println(F("Linear Custom"));
        msg.control.print(F("  Voltage Range: "));
        msg.control.print(input->customCalibration.pressureLinear.voltage_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.voltage_max, 2);
        msg.control.println(F(" V"));
        msg.control.print(F("  Output Range: "));
        msg.control.print(input->customCalibration.pressureLinear.output_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.output_max, 2);
        msg.control.println();
    } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        msg.control.println(F("Polynomial Custom (VDO)"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.pressurePolynomial.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_c, 10);
    } else if (input->calibrationType == CAL_RPM) {
        msg.control.println(F("RPM Custom"));
        msg.control.print(F("  Poles: "));
        msg.control.println(input->customCalibration.rpm.poles);
        msg.control.print(F("  Pulley Ratio: "));
        msg.control.println(input->customCalibration.rpm.pulley_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.rpm.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.rpm.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  RPM Range: "));
        msg.control.print(input->customCalibration.rpm.min_rpm);
        msg.control.print(F("-"));
        msg.control.println(input->customCalibration.rpm.max_rpm);
    } else if (input->calibrationType == CAL_SPEED) {
        msg.control.println(F("Speed Custom"));
        msg.control.print(F("  Pulses/Rev: "));
        msg.control.println(input->customCalibration.speed.pulses_per_rev);
        msg.control.print(F("  Tire Circumference: "));
        msg.control.print(input->customCalibration.speed.tire_circumference_mm);
        msg.control.println(F(" mm"));
        msg.control.print(F("  Drive Ratio: "));
        msg.control.println(input->customCalibration.speed.final_drive_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.speed.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.speed.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  Max Speed: "));
        msg.control.print(input->customCalibration.speed.max_speed_kph);
        msg.control.println(F(" km/h"));
    }
    msg.control.println();
}

void listAllInputs() {
    msg.control.println(F("Active Inputs:"));
    bool found = false;

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            found = true;
            msg.control.print(F("  "));
            if (inputs[i].pin >= 0xF0) {
                msg.control.print(F("I2C:"));
                msg.control.print(inputs[i].pin - 0xF0);
            } else if (inputs[i].pin >= A0) {
                msg.control.print(F("A"));
                msg.control.print(inputs[i].pin - A0);
            } else {
                msg.control.print(inputs[i].pin);
            }
            msg.control.print(F(": "));
            msg.control.print(inputs[i].abbrName);
            msg.control.print(F(" ("));
            msg.control.print(inputs[i].displayName);
            msg.control.print(F(") = "));
            msg.control.print(inputs[i].value);
            msg.control.print(F(" "));
            msg.control.println((__FlashStringHelper*)getUnitStringByIndex(inputs[i].unitsIndex));
        }
    }

    if (!found) {
        msg.control.println(F("  (none)"));
    }
}

void listApplicationPresets() {
    msg.control.println(F("Available Application Presets:"));

    // Group by measurement type
    msg.control.println(F("Temperature:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_TEMPERATURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            // Pad to 20 chars for alignment
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Pressure:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_PRESSURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Electrical:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Other:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type != MEASURE_TEMPERATURE && type != MEASURE_PRESSURE && type != MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }
}

/**
 * List sensors - supports three modes:
 *   1. No filter: Show category summary with sensor counts
 *   2. Category filter: Show sensors in that category (e.g., "NTC_THERMISTOR", "NTC")
 *   3. Measurement filter: Show all sensors of that type (e.g., "TEMPERATURE", "PRESSURE")
 */
void listSensors(const char* filter) {
    // Helper lambda to print a single sensor
    auto printSensor = [](const SensorInfo* sensor) {
        const char* name = READ_SENSOR_NAME(sensor);
        const char* label = (const char*)pgm_read_ptr(&sensor->label);
        msg.control.print(F("  "));
        msg.control.print((__FlashStringHelper*)name);
        uint8_t nameLen = strlen_P(name);
        for (uint8_t j = nameLen; j < 24; j++) msg.control.write(' ');
        msg.control.print(F("- "));
        if (label) msg.control.println((__FlashStringHelper*)label);
        else msg.control.println();
    };

    // No filter: Show category summary
    if (filter == nullptr) {
        msg.control.println(F("Sensor Categories:"));
        msg.control.println();

        for (uint8_t cat = 0; cat < CAT_COUNT; cat++) {
            uint8_t count = countSensorsInCategory((SensorCategory)cat);
            if (count > 0) {
                const SensorCategoryInfo* catInfo = getCategoryInfo((SensorCategory)cat);
                const char* catName = READ_CATEGORY_NAME(catInfo);
                const char* catLabel = READ_CATEGORY_LABEL(catInfo);

                msg.control.print(F("  "));
                msg.control.print((__FlashStringHelper*)catName);
                uint8_t nameLen = strlen_P(catName);
                for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
                msg.control.print(F("- "));
                msg.control.print((__FlashStringHelper*)catLabel);
                msg.control.print(F(" ("));
                msg.control.print(count);
                msg.control.println(F(")"));
            }
        }

        msg.control.println();
        msg.control.println(F("Measurement Type Filters:"));
        msg.control.print(F("  TEMPERATURE           - All temperature sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_TEMPERATURE));
        msg.control.println(F(")"));
        msg.control.print(F("  PRESSURE              - All pressure sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_PRESSURE));
        msg.control.println(F(")"));

        msg.control.println();
        msg.control.println(F("Usage: LIST SENSORS <category>    - Show sensors in category"));
        msg.control.println(F("       LIST SENSORS TEMPERATURE   - Show all temperature sensors"));
        msg.control.println(F("       SET <pin> SENSOR <category> <preset>"));
        msg.control.println();
        msg.control.println(F("Aliases: NTC -> THERMISTOR"));
        msg.control.println(F("         TC -> THERMOCOUPLE"));
        msg.control.println(F("         RPM, SPEED -> FREQUENCY"));
        return;
    }

    // Check if filter is a measurement type (TEMPERATURE, PRESSURE)
    int8_t measFilter = getMeasurementTypeFilter(filter);
    if (measFilter >= 0) {
        MeasurementType measType = (MeasurementType)measFilter;
        const char* typeName = (measType == MEASURE_TEMPERATURE) ? "Temperature" : "Pressure";

        msg.control.print(F("All "));
        msg.control.print(typeName);
        msg.control.println(F(" Sensors:"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
            if (sensorMeasType == measType) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        return;
    }

    // Check if filter is a category name or alias
    SensorCategory cat = getCategoryByName(filter);
    if (cat < CAT_COUNT) {
        const SensorCategoryInfo* catInfo = getCategoryInfo(cat);
        const char* catLabel = READ_CATEGORY_LABEL(catInfo);

        msg.control.print((__FlashStringHelper*)catLabel);
        msg.control.println(F(":"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            if (getSensorCategory(i) == cat) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("Usage: SET <pin> SENSOR <category> <preset>"));
        if (cat == CAT_ENVIRONMENTAL) {
            msg.control.println(F("Note: Use 'I2C' for pin, e.g., SET I2C AMBIENT_TEMP BME280_TEMP"));
        } else if (cat == CAT_PRESSURE || cat == CAT_THERMISTOR) {
            msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        }
        return;
    }

    // Unknown filter
    msg.control.print(F("ERROR: Unknown category or filter '"));
    msg.control.print(filter);
    msg.control.println(F("'"));
    msg.control.println(F("Use: LIST SENSORS to see available categories"));
}

#endif // USE_STATIC_CONFIG