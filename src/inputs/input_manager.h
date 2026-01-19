/*
 * input_manager.h - Input Configuration & Management
 *
 * Manages configuration and runtime operation of input pins, including:
 * - Initialization from EEPROM or static config
 * - Setting Application (measurement type) and Sensor (hardware)
 * - EEPROM persistence (in EEPROM/serial config mode)
 * - Runtime queries and modifications
 * - Custom calibration overrides
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "../config.h"
#include "../lib/platform.h"
#include <Arduino.h>
#include "input.h"

// Global inputs array
extern Input inputs[MAX_INPUTS];
extern uint8_t numActiveInputs;

// ===== INITIALIZATION =====
bool initInputManager();              // Initialize and load from EEPROM (returns true if EEPROM config loaded)

// ===== CONFIGURATION =====
bool setInputApplication(uint8_t pin, uint8_t appIndex);
bool setInputSensor(uint8_t pin, uint8_t sensorIndex);
bool setInputName(uint8_t pin, const char* name);
bool setInputDisplayName(uint8_t pin, const char* displayName);
bool setInputUnits(uint8_t pin, uint8_t unitsIndex);
bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue);
bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length);
bool enableInput(uint8_t pin, bool enable);
bool enableInputAlarm(uint8_t pin, bool enable);
bool enableInputDisplay(uint8_t pin, bool enable);
bool setInputAlarmWarmup(uint8_t pin, uint16_t warmupTime_ms);
bool setInputAlarmPersist(uint8_t pin, uint16_t persistTime_ms);
bool clearInput(uint8_t pin);

// ===== CALIBRATION OVERRIDES =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c);
bool setInputCalibrationLookup(uint8_t pin, float bias);
bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax);
bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c);
bool clearInputCalibration(uint8_t pin);

// ===== HELPER FUNCTIONS =====
Input* getInputByPin(uint8_t pin);   // Find input by pin number
Input* getInputByIndex(uint8_t index); // Get input by array index
uint8_t getInputIndex(uint8_t pin);  // Get array index for pin

#ifndef USE_STATIC_CONFIG

// ===== PERSISTENCE =====
bool saveInputConfig();               // Save all inputs to EEPROM
bool loadInputConfig();               // Load all inputs from EEPROM
void resetInputConfig();              // Clear all inputs and EEPROM

// ===== RUNTIME =====
void readAllInputs();                 // Read all enabled inputs

// ===== INFO =====
void printInputInfo(uint8_t pin);    // Print detailed input information
void printInputAlarmInfo(uint8_t pin);
void printInputCalibration(uint8_t pin);
void listAllInputs();                // List all active inputs
void listApplicationPresets();       // List available Applications
void listSensors(const char* filter = nullptr);  // List sensors (categories, by category, or by measurement type)

#endif // USE_STATIC_CONFIG

#endif // INPUT_MANAGER_H