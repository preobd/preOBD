/*
 * main.cpp - openEMS (Open Engine Monitoring System)
 * Main program loop
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "version.h"
#include "lib/platform.h"
#include "lib/watchdog.h"

// Input-based architecture (supports both EEPROM and compile-time config)
#include "lib/sensor_types.h"
#include "lib/sensor_library.h"
#include "inputs/input.h"
#include "inputs/input_manager.h"
#ifndef USE_STATIC_CONFIG
    #include "inputs/serial_config.h"  // Only needed for EEPROM/serial config mode
    #include "lib/system_mode.h"        // System mode (CONFIG/RUN)
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

// Test mode (if enabled)
#ifdef ENABLE_TEST_MODE
#include "test/test_mode.h"
#endif

// BME280 sensor (if any BME280 sensors are configured)
#ifdef USE_BME280
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;
#endif

// ===== TIME-SLICING STATE =====
// Tracks last execution time for each time-sliced operation
static uint32_t lastInputRead[MAX_INPUTS];  // Per-sensor read timing
#ifdef ENABLE_ALARMS
static uint32_t lastAlarmCheck = 0;
#endif
#ifdef ENABLE_LCD
static uint32_t lastLCDUpdate = 0;
#endif

// ===== TIME-SLICED OPERATION FUNCTIONS =====
// Extract repetitive time-slice checks into named functions for clarity

#ifndef USE_STATIC_CONFIG
// Update LCD display in CONFIG mode
static void updateConfigModeDisplay(uint32_t now) {
    #ifdef ENABLE_LCD
    if (now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
        static Input* inputPtrs[MAX_INPUTS];
        uint8_t activeCount = 0;
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            // In CONFIG mode, show any sensor with a valid pin (configured)
            if (inputs[i].pin != 0xFF) {
                inputPtrs[activeCount++] = &inputs[i];
            }
        }
        updateLCD(inputPtrs, activeCount);
        lastLCDUpdate = now;
    }
    #endif
}
#endif

// Read sensors at their individual configured intervals
static void updateSensors(uint32_t now) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (!inputs[i].flags.isEnabled) continue;

        // Get sensor-specific minimum read interval
        const SensorInfo* sensorInfo = getSensorInfo(inputs[i].sensor);
        uint16_t interval = pgm_read_word(&sensorInfo->minReadInterval);

        // Check if enough time has elapsed for this sensor
        if (now - lastInputRead[i] >= interval) {
            inputs[i].readFunction(&inputs[i]);
            lastInputRead[i] = now;
        }
    }
}

// Check alarms for all enabled inputs
static void updateAlarms(uint32_t now) {
    #ifdef ENABLE_ALARMS
    if (now - lastAlarmCheck >= ALARM_CHECK_INTERVAL_MS) {
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].flags.isEnabled) {
                checkSensorAlarm(&inputs[i]);
            }
        }
        updateAlarm();  // Update buzzer state
        lastAlarmCheck = now;
    }
    #endif
}


// Update LCD display in RUN mode
static void updateDisplay(uint32_t now) {
    #ifdef ENABLE_LCD
    if (now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
        static Input* inputPtrs[MAX_INPUTS];
        uint8_t activeCount = 0;
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (inputs[i].flags.isEnabled) {
                inputPtrs[activeCount++] = &inputs[i];
            }
        }
        updateLCD(inputPtrs, activeCount);
        lastLCDUpdate = now;
    }
    #endif
}

// Update test mode (wrapper for conditional compilation)
static void updateTestMode_Wrapper() {
    #ifdef ENABLE_TEST_MODE
    if (isTestModeActive()) {
        updateTestMode();
    }
    #endif
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
    Serial.println(F("openEngine Monitoring System ==========="));
    Serial.println("Firmware version " FIRMWARE_VERSION);
    Serial.println(F("                                        "));

    // Configure ADC for this platform
    setupADC();
    Serial.println(F("✓ ADC configured"));

    // Initialize SPI BEFORE input manager (needed for thermocouple CS pin setup)
    SPI.begin();
    Serial.println(F("✓ SPI bus initialized"));

    // Initialize input manager (loads from EEPROM or config.h)
#ifndef USE_STATIC_CONFIG
    bool eepromConfigLoaded = initInputManager();
#else
    initInputManager();
#endif

    // Note: All sensor-specific initialization (CS pins, RPM interrupts, digital inputs)
    // is handled automatically by initInputManager() via sensor init function pointers

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

    // Initialize per-sensor read timers (all start at 0)
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        lastInputRead[i] = 0;
    }

    Serial.println(F(""));
    Serial.println(F("========================================"));
    Serial.println(F("  Initialization complete!"));
#ifdef USE_STATIC_CONFIG
    Serial.println(F("  Mode: Compile-Time Config"));
#else
    Serial.println(F("  Mode: EEPROM Config"));
#endif
    Serial.print(F("  Active inputs: "));
    Serial.println(numActiveInputs);
    Serial.print(F("  System voltage: "));
    Serial.print(SYSTEM_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("  ADC reference: "));
    Serial.print(AREF_VOLTAGE);
    Serial.println(F("V"));
    Serial.print(F("  ADC resolution: "));
    Serial.print(ADC_RESOLUTION);
    Serial.println(F(" bits"));
    Serial.print(F("  ADC max value: "));
    Serial.println(ADC_MAX_VALUE);
    Serial.println(F("========================================"));
    Serial.println(F(""));

    // ===== TEST MODE ACTIVATION =====
#ifdef ENABLE_TEST_MODE
    // Initialize test mode system
    initTestMode();

    // Check if test mode trigger pin is pulled LOW
    pinMode(TEST_MODE_TRIGGER_PIN, INPUT_PULLUP);
    delay(10);  // Allow pin to stabilize

    if (digitalRead(TEST_MODE_TRIGGER_PIN) == LOW) {
        Serial.println(F(""));
        Serial.println(F("========================================"));
        Serial.println(F("  TEST MODE TRIGGER DETECTED!"));
        Serial.println(F("========================================"));
        Serial.println(F(""));

        // List available scenarios
        listTestScenarios();

        // Start default scenario (if not 0xFF)
        #if DEFAULT_TEST_SCENARIO != 0xFF
        startTestScenario(DEFAULT_TEST_SCENARIO);
        #else
        Serial.println(F("Test mode initialized but no default scenario set."));
        Serial.println(F("Use serial commands to start a scenario."));
        #endif
    } else {
        Serial.print(F("Test mode available (pin "));
        Serial.print(TEST_MODE_TRIGGER_PIN);
        Serial.println(F(" is HIGH, normal operation)"));
    }
#endif

#ifndef USE_STATIC_CONFIG
    // Initialize serial configuration interface (only in EEPROM mode)
    initSerialConfig();

    // Initialize system mode and detect boot mode
    initSystemMode();
    SystemMode bootMode = detectBootMode(eepromConfigLoaded);
    setMode(bootMode);

    // Only enable watchdog in RUN mode (CONFIG mode doesn't need it)
    if (bootMode == MODE_RUN) {
        watchdogEnable(2000);
        Serial.println(F("Watchdog enabled (2s timeout)"));
    } else {
        Serial.println(F("Watchdog disabled (CONFIG mode)"));
    }
#else
    // Always enable watchdog in static config mode
    watchdogEnable(2000);
    Serial.println(F("Watchdog enabled (2s timeout)"));
#endif
}

void loop() {
    // Get current time once per loop
    uint32_t now = millis();

    // Reset watchdog at start of every loop iteration
    watchdogReset();

#ifndef USE_STATIC_CONFIG
    // Process serial configuration commands (non-blocking, always runs)
    processSerialCommands();

    // If in CONFIG mode, skip sensor reading and outputs
    if (isInConfigMode()) {
        updateConfigModeDisplay(now);
        return;  // Early return - don't read sensors or send outputs
    }
#endif

    // Read sensors, check alarms, send outputs, update display
    updateSensors(now);
    updateAlarms(now);
    sendToOutputs(now);  // Data-driven time-sliced output sending
    updateDisplay(now);
    updateOutputs();     // Housekeeping: drain buffers, handle RX

    // Update test mode if active
    updateTestMode_Wrapper();

    // NO DELAY - loop runs as fast as possible
    // Time-slicing controls when operations execute
}
