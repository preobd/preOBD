/*
 * main.cpp - openEMS (Open Engine Monitoring System)
 * Main program loop
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "lib/platform.h"

// Input-based architecture (supports both EEPROM and compile-time config)
#include "lib/sensor_types.h"
#include "inputs/input.h"
#include "inputs/input_manager.h"
#ifndef USE_STATIC_CONFIG
    #include "inputs/serial_config.h"  // Only needed for EEPROM/serial config mode
#endif
#include "outputs/output_base.h"

// Declare output module functions
extern void initOutputModules();
extern void sendToOutputs(Input*);
extern void updateOutputs();

// Declare display functions
extern void initLCD();
extern void updateLCD(Input**, int);

// Declare alarm functions
extern void initAlarm();
extern void checkSensorAlarm(Input*);
extern void updateAlarm();

// BME280 sensor (if any BME280 sensors are configured)
#ifdef USE_BME280
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;
#endif

void setupADC() {
    // Configure analog reference based on platform
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        // Arduino Uno/Nano - 5V system
        analogReference(DEFAULT);  // 5V reference        
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        // Arduino Mega - 5V system
        analogReference(DEFAULT);  // 5V reference
    #elif defined(__MK20DX256__) || defined(__MK20DX128__)
        // Teensy 3.1/3.2 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);  // Average 4 samples for stability
    #elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
        // Teensy 3.5/3.6 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);        
    #elif defined(__IMXRT1062__)
        // Teensy 4.0/4.1 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
    #elif defined(ARDUINO_SAM_DUE)
        // Arduino Due - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
    #elif defined(ESP32)
        // ESP32 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        // Set attenuation for full 0-3.3V range
        // ADC_11db = 0-3.3V (default but less accurate)
        // ADC_6db = 0-2.2V (more accurate, use if voltage divider allows)
        analogSetAttenuation(ADC_11db);
        Serial.println(F("ADC: Attenuation set to 11db (0-3.3V range)"));
        Serial.println(F("NOTE: ESP32 ADC is non-linear, consider calibration"));
    #else
        // Unknown platform - use default
        Serial.println(F("WARNING: Unknown platform, using default ADC settings"));
        #ifdef ADC_RESOLUTION
        analogReadResolution(ADC_RESOLUTION);
        #endif
    #endif
    
    // Print system configuration
    Serial.println(F("=== System Configuration ==="));
    Serial.print(F("System voltage: "));
    Serial.print(SYSTEM_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("ADC reference: "));
    Serial.print(AREF_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("ADC resolution: "));
    Serial.print(ADC_RESOLUTION);
    Serial.println(F(" bits"));
    Serial.print(F("ADC max value: "));
    Serial.println(ADC_MAX_VALUE);

    Serial.println(F("========================================"));
}

void setup() {

    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {};  // Wait up to 3 seconds for serial

    Serial.println(F("                                        "));
    Serial.println(F("                       ______  _______  "));
    Serial.println(F("   ___  ___  ___ ___  / __/  |/  / __/  "));
    Serial.println(F("  / _ \\/ _ \\/ -_) _ \\/ _// /|_/ /\\ \\    "));
    Serial.println(F("  \\___/ .__/\\__/_//_/___/_/  /_/___/    "));
    Serial.println(F("     /_/                                "));
    Serial.println(F("                                        "));
    Serial.println(F("openEngine Monitoring System v1.0 ======"));
    Serial.println(F("                                        "));
    
    // Configure ADC for this platform
    setupADC();

    Serial.println(F("Initializing..."));

    // Initialize input manager (loads from EEPROM or sensors_config.h)
    initInputManager();

#ifndef USE_STATIC_CONFIG
    // Initialize serial configuration interface (only in EEPROM mode)
    initSerialConfig();
#endif

    // Initialize SPI for thermocouples and other SPI sensors
    SPI.begin();
    Serial.println(F("✓ SPI bus initialized"));

    // Note: CS pins for thermocouples are initialized in initInputManager()
    // RPM and digital sensors would be initialized here if needed
    /*
        // Initialize chip select pins for thermocouples
    #ifdef ENABLE_CHT
    pinMode(CHT_INPUT, OUTPUT);
    digitalWrite(CHT_INPUT, HIGH);
    Serial.println(F("✓ CHT chip select initialized"));
    #endif

    #ifdef ENABLE_EGT
    pinMode(EGT_INPUT, OUTPUT);
    digitalWrite(EGT_INPUT, HIGH);
    Serial.println(F("✓ EGT chip select initialized"));
    #endif

    // Initialize RPM sensing
    #ifdef ENABLE_ENGINE_RPM
    extern void initRPM(byte);
    initRPM(RPM_INPUT);
    #endif

    // Initialize coolant level sensor
    #ifdef ENABLE_COOLANT_LEVEL
    pinMode(COOLANT_LEVEL_INPUT, INPUT);
    Serial.println(F("✓ Coolant level sensor initialized"));
    #endif
    */

    // Initialize I2C for BME280 and LCD
    Wire.begin();
    #if defined(ESP32)
    // ESP32 may need explicit SDA/SCL pins
    // Wire.begin(SDA_PIN, SCL_PIN);  // Uncomment and set if needed
    Wire.setClock(100000);  // 100kHz for stability
    #else
    Wire.setClock(400000);  // 400kHz for most platforms
    #endif
    Serial.println(F("✓ I2C initialized"));
    
    // Initialize BME280 (if any BME280 sensors are configured)
    #ifdef USE_BME280
    Serial.print(F("Initializing BME280... "));
    if (!bme.begin(0x76, &Wire)) {
        Serial.println(F("FAILED!"));
        Serial.println(F("   BME280 not found at 0x76, trying 0x77..."));
        if (!bme.begin(0x77, &Wire)) {
            Serial.println(F("   BME280 not found - BME280 sensors will read NAN"));
            // Note: In input-based architecture, sensors are disabled via serial commands

        } else {
            Serial.println(F("OK (at 0x77)"));
        }
    } else {
        Serial.println(F("OK (at 0x76)"));
    }
    #endif
    // Initialize display
    #ifdef ENABLE_LCD
    Serial.print(F("Initializing LCD... "));
    initLCD();
    #endif

    // Initialize alarm system
    Serial.print(F("Initializing alarm system... "));
    initAlarm();
    Serial.println(F("OK"));

    // Initialize output modules
    Serial.println(F("Initializing output modules..."));
    initOutputModules();

    // Wait for sensors to stabilize
    Serial.println(F(""));
    Serial.println(F("Waiting for sensors to stabilize..."));
    delay(1000);  // Increased from 500ms - MAX6675 needs ~220ms for first conversion

    Serial.println(F(""));
    Serial.println(F("========================================"));
    Serial.println(F("  Initialization complete!"));
#ifdef USE_STATIC_CONFIG
    Serial.println(F("  Mode: Input-Based (Compile-Time Config)"));
#else
    Serial.println(F("  Mode: Input-Based (EEPROM Config)"));
#endif
    Serial.print(F("  Active inputs: "));
    Serial.println(numActiveInputs);
    Serial.println(F("========================================"));
    Serial.println(F(""));
}

void loop() {
    // ===== UNIFIED INPUT-BASED ARCHITECTURE =====
    // Works with both EEPROM config and compile-time config

#ifndef USE_STATIC_CONFIG
    // Process serial configuration commands (only in EEPROM mode)
    processSerialCommands();
#endif

    // Read all enabled inputs
    readAllInputs();

    // Send data to output modules
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            sendToOutputs(&inputs[i]);
        }
    }

    // Check alarms
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            checkSensorAlarm(&inputs[i]);
        }
    }

    // Update alarm state
    updateAlarm();

    // Update display
    #ifdef ENABLE_LCD
    // Create pointer array for compatibility with updateLCD
    static Input* inputPtrs[MAX_INPUTS];
    uint8_t activeCount = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            inputPtrs[activeCount++] = &inputs[i];
        }
    }
    updateLCD(inputPtrs, activeCount);
    #endif

    // Update output modules (for any housekeeping)
    updateOutputs();

    // Delay between iterations
    delay(LOOP_DELAY_MS);
}
