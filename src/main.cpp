/*
 * main.cpp - openEMS (Open Engine Monitoring System)
 * Main program loop
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
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

void setup() {
    // Set analog reference for voltage measurements
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        analogReference(INTERNAL);
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        analogReference(INTERNAL1V1);
    #elif defined(__MK20DX256__) || defined(__MK20DX128__) || defined(__MK64FX512__) || defined(__MK66FX1M0__)
        analogReference(INTERNAL);  // Teensy 1.2V reference
    #endif
    
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {};  // Wait up to 3 seconds for serial
    Serial.println("=== openEMS v1.0 ===");
    Serial.println("Open Engine Monitoring System");
    Serial.println("Initializing...");
    
    // Initialize SPI for thermocouples
    SPI.begin();
    
    // Initialize chip select pins for thermocouples
    #ifdef ENABLE_CHT
    pinMode(CHT_INPUT, OUTPUT);
    digitalWrite(CHT_INPUT, HIGH);
    #endif
    
    #ifdef ENABLE_EGT
    pinMode(EGT_INPUT, OUTPUT);
    digitalWrite(EGT_INPUT, HIGH);
    #endif
    
    // Initialize I2C for BME280 and LCD
    Wire.begin();
    
    // Initialize BME280
    #if defined(ENABLE_AMBIENT_TEMP) || defined(ENABLE_BAROMETRIC_PRESSURE)
    if (!bme.begin(0x76, &Wire)) {
        Serial.println("BME280 not found! Disabling related sensors.");
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
        Serial.println("BME280 initialized");
    }
    #endif
    
    // Initialize display
    #ifdef ENABLE_LCD
    initLCD();
    #endif
    
    // Initialize alarm system
    initAlarm();
    
    // Initialize output modules
    initOutputModules();
    
    // Wait for sensors to stabilize
    delay(500);
    
    Serial.println("Initialization complete!");
    Serial.print("Active sensors: ");
    Serial.println(numSensors);
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
