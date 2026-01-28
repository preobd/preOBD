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
#ifdef USE_STATIC_CONFIG
#include "lib/generated/sensor_library_static.h"
#else
#include "lib/sensor_library.h"
#endif
#include "lib/system_config.h"
#include "lib/bus_manager.h"
#include "lib/serial_manager.h"
#include "lib/pin_registry.h"
#include "lib/sd_manager.h"
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
#include "lib/log_tags.h"
#include "lib/transport_serial.h"
#ifdef ESP32
// Include appropriate Bluetooth transport for ESP32 variant
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #include "lib/transports/transport_ble_esp32.h"
#else
    #include "lib/transports/transport_bluetooth_esp32.h"
#endif
#endif

// Global transport instances
SerialTransport usbSerial(&Serial, "USB", 115200);

// Hardware serial transports - created for all available ports
// Actual initialization happens in initConfiguredSerialPorts() based on config
#if NUM_SERIAL_PORTS >= 1
SerialTransport hwSerial1(&Serial1, "SERIAL1", 115200);
#endif
#if NUM_SERIAL_PORTS >= 2
SerialTransport hwSerial2(&Serial2, "SERIAL2", 115200);
#endif
#if NUM_SERIAL_PORTS >= 3
SerialTransport hwSerial3(&Serial3, "SERIAL3", 115200);
#endif
#if NUM_SERIAL_PORTS >= 4
SerialTransport hwSerial4(&Serial4, "SERIAL4", 115200);
#endif
#if NUM_SERIAL_PORTS >= 5
SerialTransport hwSerial5(&Serial5, "SERIAL5", 115200);
#endif
#if NUM_SERIAL_PORTS >= 6
SerialTransport hwSerial6(&Serial6, "SERIAL6", 115200);
#endif
#if NUM_SERIAL_PORTS >= 7
SerialTransport hwSerial7(&Serial7, "SERIAL7", 115200);
#endif
#if NUM_SERIAL_PORTS >= 8
SerialTransport hwSerial8(&Serial8, "SERIAL8", 115200);
#endif

#ifdef ESP32
// ESP32 Bluetooth transport (Classic or BLE depending on chip)
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    BLETransportESP32 btESP32("openEMS");
#else
    BluetoothTransportESP32 btESP32("openEMS");
#endif
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

    // Initialize system config (loads from EEPROM or uses defaults from config.h)
    // MUST happen before router.begin() so router can load correct transport mappings
#ifndef USE_STATIC_CONFIG
    initSystemConfig();
#endif

    // Initialize pin registry FIRST - before any code registers pins
    // This was previously called later, which caused the registry to be cleared
    // after serial ports had already registered their pins
    initPinRegistry();

    // Register system pins in the pin registry
    registerSystemPins();

    // Initialize configured serial ports based on SystemConfig.serial
    // This replaces the old hardcoded Serial1.begin() / Serial2.begin() calls
    initConfiguredSerialPorts();

    // Initialize transport router - register USB serial (always available)
    router.registerTransport(TRANSPORT_USB_SERIAL, &usbSerial);

    // Register all available hardware serial transports
    // They will only be usable if enabled via BUS SERIAL command
#if NUM_SERIAL_PORTS >= 1
    router.registerTransport(TRANSPORT_SERIAL1, &hwSerial1);
#endif
#if NUM_SERIAL_PORTS >= 2
    router.registerTransport(TRANSPORT_SERIAL2, &hwSerial2);
#endif
#if NUM_SERIAL_PORTS >= 3
    router.registerTransport(TRANSPORT_SERIAL3, &hwSerial3);
#endif
#if NUM_SERIAL_PORTS >= 4
    router.registerTransport(TRANSPORT_SERIAL4, &hwSerial4);
#endif
#if NUM_SERIAL_PORTS >= 5
    router.registerTransport(TRANSPORT_SERIAL5, &hwSerial5);
#endif
#if NUM_SERIAL_PORTS >= 6
    router.registerTransport(TRANSPORT_SERIAL6, &hwSerial6);
#endif
#if NUM_SERIAL_PORTS >= 7
    router.registerTransport(TRANSPORT_SERIAL7, &hwSerial7);
#endif
#if NUM_SERIAL_PORTS >= 8
    router.registerTransport(TRANSPORT_SERIAL8, &hwSerial8);
#endif

#ifdef ESP32
    if (btESP32.begin()) {
        router.registerTransport(TRANSPORT_ESP32_BT, &btESP32);
        msg.debug.info(TAG_BT, "ESP32 Bluetooth initialized");
    } else {
        msg.debug.warn(TAG_BT, "ESP32 Bluetooth failed to initialize");
    }
#endif
    router.begin();  // Load config from EEPROM

    msg.control.println(F("                                        "));
    msg.control.println(F("                       ______  _______  "));
    msg.control.println(F("   ___  ___  ___ ___  / __/  |/  / __/  "));
    msg.control.println(F("  / _ \\/ _ \\/ -_) _ \\/ _// /|_/ /\\ \\    "));
    msg.control.println(F("  \\___/ .__/\\__/_//_/___/_/  /_/___/    "));
    msg.control.println(F("     /_/                                "));
    msg.control.println(F("                                        "));
    msg.control.println(F("openEngine Monitoring System ==========="));
    msg.control.print(F("Firmware version "));
    msg.control.println(firmwareVersionString());
    msg.control.println(F("                                        "));

    // Configure ADC for this platform
    setupADC();
    msg.debug.info(TAG_ADC, "ADC configured");

    // Initialize configured buses (I2C, SPI, CAN) based on SystemConfig
    // This replaces the old hardcoded Wire.begin() and SPI.begin() calls
    initConfiguredBuses();

    // Initialize SD card (shared by SD logging and JSON config)
    initSD();

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
    msg.debug.info(TAG_SENSOR, "Waiting for sensors to stabilize...");
    delay(1000);  // Increased from 500ms - MAX6675 needs ~220ms for first conversion

    // Initialize per-sensor read timers (all start at 0)
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        lastInputRead[i] = 0;
    }

    msg.debug.info(TAG_SYSTEM, "Initialization complete!");
#ifdef USE_STATIC_CONFIG
    msg.debug.info(TAG_CONFIG, "Mode: Compile-Time Config");
    msg.debug.info(TAG_CONFIG, "Active inputs: %d", numActiveInputs);
    msg.debug.info(TAG_CONFIG, "System voltage: %.1fV", SYSTEM_VOLTAGE);
    msg.debug.info(TAG_CONFIG, "ADC reference: %.2fV", AREF_VOLTAGE);
    msg.debug.info(TAG_CONFIG, "ADC resolution: %d bits", ADC_RESOLUTION);
    msg.debug.info(TAG_CONFIG, "ADC max value: %d", ADC_MAX_VALUE);
#endif

    // ===== TEST MODE ACTIVATION =====
#ifdef ENABLE_TEST_MODE
    // Initialize test mode system
    initTestMode();

    // Check if test mode trigger pin is pulled LOW
    pinMode(TEST_MODE_TRIGGER_PIN, INPUT_PULLUP);
    delay(10);  // Allow pin to stabilize

    if (digitalRead(TEST_MODE_TRIGGER_PIN) == LOW) {
        msg.debug.info(TAG_SYSTEM, "TEST MODE TRIGGER DETECTED!");

        // List available scenarios
        listTestScenarios();

        // Start default scenario (if not 0xFF)
        #if DEFAULT_TEST_SCENARIO != 0xFF
        startTestScenario(DEFAULT_TEST_SCENARIO);
        #else
        msg.debug.info(TAG_SYSTEM, "Test mode initialized but no default scenario set.");
        msg.debug.info(TAG_SYSTEM, "Use serial commands to start a scenario.");
        #endif
    } else {
        msg.debug.info(TAG_SYSTEM, "Test mode available (pin %d is HIGH, normal operation)", TEST_MODE_TRIGGER_PIN);
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
        msg.debug.info(TAG_SYSTEM, "Watchdog enabled (2s timeout)");
    } else {
        msg.debug.info(TAG_SYSTEM, "Watchdog disabled (CONFIG mode)");
    }
#else
    // Always enable watchdog in static config mode
    watchdogEnable(2000);
    msg.debug.info(TAG_SYSTEM, "Watchdog enabled (2s timeout)");
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
