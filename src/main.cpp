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

#include "lib/sensor_types.h"
#include "lib/sensor_library.h"
#include "lib/system_config.h"
#include "inputs/input.h"
#include "inputs/input_manager.h"
#ifndef USE_STATIC_CONFIG
    #include "inputs/serial_config.h"   // Only needed for EEPROM/serial config mode
    #include "lib/system_mode.h"        // System mode (CONFIG/RUN)
    #include "lib/button_handler.h"     // Multi-function button handler
#endif
#include "lib/display_manager.h"        // Display runtime state management
#include "outputs/output_base.h"

// Declare output module functions
extern void initOutputModules();
extern void sendToOutputs(Input*);
extern void updateOutputs();

// Declare display functions
extern void initLCD();
extern void updateLCD(Input**, int);

// Alarm logic module
#include "inputs/alarm_logic.h"

// Test mode (if enabled)
#ifdef ENABLE_TEST_MODE
#include "test/test_mode.h"
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
    if (isDisplayActive() && now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
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
        const SensorInfo* sensorInfo = getSensorByIndex(inputs[i].sensorIndex);
        uint16_t interval = sensorInfo ? pgm_read_word(&sensorInfo->minReadInterval) : SENSOR_READ_INTERVAL_MS;

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
        updateAllInputAlarms(now);  // Update alarm state for all inputs
        lastAlarmCheck = now;
    }
    #endif
}


// Update LCD display in RUN mode
static void updateDisplay(uint32_t now) {
    #ifdef ENABLE_LCD
    if (isDisplayActive() && now - lastLCDUpdate >= LCD_UPDATE_INTERVAL_MS) {
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

    // Initialize system config (loads from EEPROM or uses defaults from config.h)
#ifndef USE_STATIC_CONFIG
    initSystemConfig();
#endif

    // Initialize input manager (loads from EEPROM or config.h)
#ifndef USE_STATIC_CONFIG
    bool eepromConfigLoaded = initInputManager();
#else
    initInputManager();
#endif

    // Initialize display
    #ifdef ENABLE_LCD
    initLCD();
    #endif

#ifndef USE_STATIC_CONFIG
    // Initialize button handler (only in EEPROM mode)
    initButtonHandler();
#endif

    // Initialize display manager (works in both static and EEPROM modes)
    // In static mode, this is a no-op (always returns true for isDisplayActive)
    initDisplayManager();

    // Initialize output modules
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
#else
    Serial.println(F(""));
#endif

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

    // Process button events (short press = silence alarm, long press = toggle display)
    ButtonPress buttonEvent = updateButtonHandler();
    if (buttonEvent == BUTTON_SHORT_PRESS) {
        // Short press handled by alarm system (already reads the button)
        // No action needed here
    } else if (buttonEvent == BUTTON_LONG_PRESS) {
        // Long press toggles display
        toggleDisplayRuntime();
    }

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
