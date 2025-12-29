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

// Transport abstraction layer
#include "lib/message_router.h"
#include "lib/message_api.h"
#include "lib/transport_serial.h"

// Global transport instances
SerialTransport usbSerial(&Serial, "USB", 115200);
#if defined(__MK66FX1M0__) || defined(__IMXRT1062__) || defined(__MK64FX512__) || defined(__MK20DX256__)
// Teensy platforms have hardware Serial1, Serial2, etc.
SerialTransport hwSerial1(&Serial1, "SERIAL1", 115200);
SerialTransport hwSerial2(&Serial2, "SERIAL2", 9600);  // Default 9600 for HC-05/HM-10
#elif defined(__AVR_ATmega2560__)
// Arduino Mega has Serial1-3
SerialTransport hwSerial1(&Serial1, "SERIAL1", 115200);
SerialTransport hwSerial2(&Serial2, "SERIAL2", 9600);  // Default 9600 for HC-05/HM-10
#endif

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

    // Initialize hardware UARTs for Bluetooth modules (if wired)
    #if defined(__MK66FX1M0__) || defined(__IMXRT1062__) || defined(__MK64FX512__) || defined(__MK20DX256__) || defined(__AVR_ATmega2560__)
    Serial1.begin(115200);  // Can be used for data output or control
    Serial2.begin(9600);    // Default baud for HC-05/HM-10 Bluetooth modules
    #endif

    // Initialize transport router
    router.registerTransport(TRANSPORT_USB_SERIAL, &usbSerial);
    #if defined(__MK66FX1M0__) || defined(__IMXRT1062__) || defined(__MK64FX512__) || defined(__MK20DX256__) || defined(__AVR_ATmega2560__)
    router.registerTransport(TRANSPORT_SERIAL1, &hwSerial1);
    router.registerTransport(TRANSPORT_SERIAL2, &hwSerial2);
    #endif
    router.begin();  // Load config from EEPROM

    msg.debug.println(F("                                        "));
    msg.debug.println(F("                       ______  _______  "));
    msg.debug.println(F("   ___  ___  ___ ___  / __/  |/  / __/  "));
    msg.debug.println(F("  / _ \\/ _ \\/ -_) _ \\/ _// /|_/ /\\ \\    "));
    msg.debug.println(F("  \\___/ .__/\\__/_//_/___/_/  /_/___/    "));
    msg.debug.println(F("     /_/                                "));
    msg.debug.println(F("                                        "));
    msg.debug.println(F("openEngine Monitoring System ==========="));
    msg.debug.print("Firmware version ");
    msg.debug.println(FIRMWARE_VERSION);
    msg.debug.println(F("                                        "));

    // Configure ADC for this platform
    setupADC();
    msg.debug.println(F("✓ ADC configured"));

    // Initialize SPI BEFORE input manager (needed for thermocouple CS pin setup)
    SPI.begin();
    msg.debug.println(F("✓ SPI bus initialized"));

    // Initialize I2C for BME280 and LCD
    Wire.begin();
    #if defined(ESP32)
    // ESP32 may need explicit SDA/SCL pins
    // Wire.begin(SDA_PIN, SCL_PIN);  // Uncomment and set if needed
    Wire.setClock(100000);  // 100kHz for stability
    #else
    Wire.setClock(400000);  // 400kHz for most platforms
    #endif
    msg.debug.println(F("✓ I2C initialized"));

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
    msg.debug.println(F(""));
    msg.debug.println(F("Waiting for sensors to stabilize..."));
    delay(1000);  // Increased from 500ms - MAX6675 needs ~220ms for first conversion

    // Initialize per-sensor read timers (all start at 0)
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        lastInputRead[i] = 0;
    }

    msg.debug.println(F(""));
    msg.debug.println(F("========================================"));
    msg.debug.println(F("  Initialization complete!"));
#ifdef USE_STATIC_CONFIG
    msg.debug.println(F("  Mode: Compile-Time Config"));
    msg.debug.print(F("  Active inputs: "));
    msg.debug.println(numActiveInputs);
    msg.debug.print(F("  System voltage: "));
    msg.debug.print(SYSTEM_VOLTAGE);
    msg.debug.println(F("V"));
    msg.debug.print(F("  ADC reference: "));
    msg.debug.print(AREF_VOLTAGE);
    msg.debug.println(F("V"));
    msg.debug.print(F("  ADC resolution: "));
    msg.debug.print(ADC_RESOLUTION);
    msg.debug.println(F(" bits"));
    msg.debug.print(F("  ADC max value: "));
    msg.debug.println(ADC_MAX_VALUE);
    msg.debug.println(F("========================================"));
    msg.debug.println(F(""));
#else
    msg.debug.println(F(""));
#endif

    // ===== TEST MODE ACTIVATION =====
#ifdef ENABLE_TEST_MODE
    // Initialize test mode system
    initTestMode();

    // Check if test mode trigger pin is pulled LOW
    pinMode(TEST_MODE_TRIGGER_PIN, INPUT_PULLUP);
    delay(10);  // Allow pin to stabilize

    if (digitalRead(TEST_MODE_TRIGGER_PIN) == LOW) {
        msg.debug.println(F(""));
        msg.debug.println(F("========================================"));
        msg.debug.println(F("  TEST MODE TRIGGER DETECTED!"));
        msg.debug.println(F("========================================"));
        msg.debug.println(F(""));

        // List available scenarios
        listTestScenarios();

        // Start default scenario (if not 0xFF)
        #if DEFAULT_TEST_SCENARIO != 0xFF
        startTestScenario(DEFAULT_TEST_SCENARIO);
        #else
        msg.debug.println(F("Test mode initialized but no default scenario set."));
        msg.debug.println(F("Use serial commands to start a scenario."));
        #endif
    } else {
        msg.debug.print(F("Test mode available (pin "));
        msg.debug.print(TEST_MODE_TRIGGER_PIN);
        msg.debug.println(F(" is HIGH, normal operation)"));
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
        msg.debug.println(F("Watchdog enabled (2s timeout)"));
    } else {
        msg.debug.println(F("Watchdog disabled (CONFIG mode)"));
    }
#else
    // Always enable watchdog in static config mode
    watchdogEnable(2000);
    msg.debug.println(F("Watchdog enabled (2s timeout)"));
#endif
}

void loop() {
    // Get current time once per loop
    uint32_t now = millis();

    // Reset watchdog at start of every loop iteration
    watchdogReset();

    // Update transport router (poll transports, handle housekeeping, process commands)
    router.update();  // Now handles command input from ALL transports

#ifndef USE_STATIC_CONFIG
    // NOTE: processSerialCommands() is now deprecated - router.update() handles it
    // Kept for reference but does nothing (see serial_config.cpp)

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
