/*
 * main.cpp - openEMS (Open Engine Monitoring System)
 * Main program loop
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "platform.h"
#include "sensor_types.h"
#include "sensors.h"
#include "outputs/output_base.h"

// Declare output module functions
extern void initOutputModules();
extern void sendToOutputs(Sensor*);
extern void updateOutputs();

// Declare display functions
extern void initLCD();
extern void updateLCD(Sensor**, int);

// Declare alarm functions
extern void initAlarm();
extern void checkSensorAlarm(Sensor*);
extern void updateAlarm();

// BME280 sensor (if enabled)
#if defined(ENABLE_AMBIENT_TEMP) || defined(ENABLE_BAROMETRIC_PRESSURE)
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#ifdef BME280_INPUT
Adafruit_BME280 bme(BME280_INPUT);
#else
Adafruit_BME280 bme;
#endif
#endif

void setupADC() {
    // Configure analog reference based on platform
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        // Arduino Uno/Nano - 5V system
        analogReference(INTERNAL);  // 1.1V reference
        Serial.println("ADC: Using INTERNAL 1.1V reference (ATmega328)");
        
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        // Arduino Mega - 5V system
        analogReference(INTERNAL1V1);  // 1.1V reference
        Serial.println("ADC: Using INTERNAL1V1 reference (Mega)");
        
    #elif defined(__MK20DX256__) || defined(__MK20DX128__)
        // Teensy 3.1/3.2 - 3.3V system
        analogReference(INTERNAL);  // 1.2V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);  // Average 4 samples for stability
        Serial.println("ADC: Using INTERNAL 1.2V reference (Teensy 3.1/3.2)");
        Serial.print("ADC: Resolution set to ");
        Serial.print(ADC_RESOLUTION);
        Serial.println(" bits");
        
    #elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
        // Teensy 3.5/3.6 - 3.3V system
        analogReference(INTERNAL);  // 1.2V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
        Serial.println("ADC: Using INTERNAL 1.2V reference (Teensy 3.5/3.6)");
        Serial.print("ADC: Resolution set to ");
        Serial.print(ADC_RESOLUTION);
        Serial.println(" bits");
        
    #elif defined(__IMXRT1062__)
        // Teensy 4.0/4.1 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
        Serial.println("ADC: Using 3.3V reference (Teensy 4.x)");
        Serial.print("ADC: Resolution set to ");
        Serial.print(ADC_RESOLUTION);
        Serial.println(" bits");
        
    #elif defined(ARDUINO_SAM_DUE)
        // Arduino Due - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        Serial.println("ADC: Using 3.3V reference (Due)");
        Serial.print("ADC: Resolution set to ");
        Serial.print(ADC_RESOLUTION);
        Serial.println(" bits");
        
    #elif defined(ESP32)
        // ESP32 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        // Set attenuation for full 0-3.3V range
        // ADC_11db = 0-3.3V (default but less accurate)
        // ADC_6db = 0-2.2V (more accurate, use if voltage divider allows)
        analogSetAttenuation(ADC_11db);
        Serial.println("ADC: Using 3.3V reference (ESP32)");
        Serial.print("ADC: Resolution set to ");
        Serial.print(ADC_RESOLUTION);
        Serial.println(" bits");
        Serial.println("ADC: Attenuation set to 11db (0-3.3V range)");
        Serial.println("NOTE: ESP32 ADC is non-linear, consider calibration");
        
    #else
        // Unknown platform - use default
        Serial.println("WARNING: Unknown platform, using default ADC settings");
        #ifdef ADC_RESOLUTION
        analogReadResolution(ADC_RESOLUTION);
        #endif
    #endif
    
    // Print system configuration
    Serial.println("=== System Configuration ===");
    Serial.print("System voltage: ");
    Serial.print(SYSTEM_VOLTAGE);
    Serial.println("V");
    Serial.print("ADC reference: ");
    Serial.print(AREF_VOLTAGE);
    Serial.println("V");
    Serial.print("ADC resolution: ");
    Serial.print(ADC_RESOLUTION);
    Serial.println(" bits");
    Serial.print("ADC max value: ");
    Serial.println(ADC_MAX_VALUE);
    
    Serial.println("============================");
}

void setup() {

    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {};  // Wait up to 3 seconds for serial
    Serial.println("=== openEMS v1.0 ===");
    Serial.println("Open Engine Monitoring System");
    Serial.println("Initializing...");
    
    // Configure ADC for this platform
    setupADC();

    // Initialize SPI for thermocouples
    SPI.begin();
    
    // Initialize chip select pins for thermocouples
    #ifdef ENABLE_CHT
    pinMode(CHT_INPUT, OUTPUT);
    digitalWrite(CHT_INPUT, HIGH);
    Serial.println("✓ CHT chip select initialized");
    #endif
    
    #ifdef ENABLE_EGT
    pinMode(EGT_INPUT, OUTPUT);
    digitalWrite(EGT_INPUT, HIGH);
    Serial.println("✓ EGT chip select initialized");
    #endif
    
   // Initialize I2C for BME280 and LCD
    Wire.begin();
    #if defined(ESP32)
    // ESP32 may need explicit SDA/SCL pins
    // Wire.begin(SDA_PIN, SCL_PIN);  // Uncomment and set if needed
    Wire.setClock(100000);  // 100kHz for stability
    #else
    Wire.setClock(400000);  // 400kHz for most platforms
    #endif
    Serial.println("✓ I2C initialized");
    
    // Initialize BME280
    #if defined(ENABLE_AMBIENT_TEMP) || defined(ENABLE_BAROMETRIC_PRESSURE)
    Serial.print("Initializing BME280... ");
    if (!bme.begin(0x76, &Wire)) {
        Serial.println("FAILED!");
        Serial.println("   BME280 not found at 0x76, trying 0x77...");
        if (!bme.begin(0x77, &Wire)) {
            Serial.println("   BME280 not found at 0x77 either!");
            Serial.println("   Disabling BME280 sensors");
            // Disable BME280 sensors if initialization fails
            #ifdef ENABLE_AMBIENT_TEMP
            extern Sensor ambientAirTemp;
            ambientAirTemp.isEnabled = false;
            #endif
            #ifdef ENABLE_BAROMETRIC_PRESSURE
            extern Sensor absBarPressure;
            absBarPressure.isEnabled = false;
            #endif
        } else {
            Serial.println("OK (at 0x77)");
        }
    } else {
        Serial.println("OK (at 0x76)");
    }
    #endif
        // Initialize display
    #ifdef ENABLE_LCD
    Serial.print("Initializing LCD... ");
    initLCD();
    #endif
    
    // Initialize alarm system
    Serial.print("Initializing alarm system... ");
    initAlarm();
    Serial.println("OK");
    
    // Initialize output modules
    Serial.println("Initializing output modules...");
    initOutputModules();
    
    // Wait for sensors to stabilize
    Serial.println("");
    Serial.println("Waiting for sensors to stabilize...");
    delay(500);
    
    Serial.println("");
    Serial.println("====================================");
    Serial.println("  Initialization complete!");
    Serial.print("  Active sensors: ");
    Serial.println(numSensors);
    Serial.println("====================================");
    Serial.println("");
}

void loop() {
    // Read all enabled sensors
    for (int i = 0; i < numSensors; i++) {
        if (sensors[i]->isEnabled && sensors[i]->readFunction != nullptr) {
            sensors[i]->readFunction(sensors[i]);
        }
    }
    
    // Send data to output modules
    for (int i = 0; i < numSensors; i++) {
        if (sensors[i]->isEnabled) {
            sendToOutputs(sensors[i]);
        }
    }
    
    // Check alarms
    for (int i = 0; i < numSensors; i++) {
        if (sensors[i]->isEnabled) {
            checkSensorAlarm(sensors[i]);
        }
    }
    
    // Update alarm state
    updateAlarm();
    
    // Update display
    #ifdef ENABLE_LCD
    updateLCD(sensors, numSensors);
    #endif
    
    // Update output modules (for any housekeeping)
    updateOutputs();
    
    // Delay between iterations
    delay(LOOP_DELAY_MS);
}
