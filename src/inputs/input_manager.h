/*
 * input_manager.h - Input Configuration & Management
 *
 * Manages configuration and runtime operation of input pins, including:
 * - Initialization from EEPROM or compile-time config (sensors_config.h)
 * - Setting Application (measurement type) and Sensor (hardware)
 * - EEPROM persistence (in EEPROM/serial config mode)
 * - Runtime queries and modifications
 * - Custom calibration overrides
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include "input.h"

// Platform-specific max inputs
#ifndef MAX_INPUTS
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        #define MAX_INPUTS 6   // Arduino Uno
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        #define MAX_INPUTS 16  // Arduino Mega
    #elif defined(__MK20DX256__) || defined(__MK20DX128__)
        #define MAX_INPUTS 24  // Teensy 3.x
    #elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
        #define MAX_INPUTS 32  // Teensy 3.5/3.6
    #elif defined(__IMXRT1062__)
        #define MAX_INPUTS 40  // Teensy 4.x
    #elif defined(ESP32)
        #define MAX_INPUTS 32  // ESP32
    #else
        #define MAX_INPUTS 8   // Default/Unknown
    #endif
#endif

// Global inputs array
extern Input inputs[MAX_INPUTS];
extern uint8_t numActiveInputs;

// ===== INITIALIZATION =====
void initInputManager();              // Initialize and load from EEPROM

// ===== CONFIGURATION =====
bool setInputApplication(uint8_t pin, Application app);
bool setInputSensor(uint8_t pin, Sensor sensor);
bool setInputName(uint8_t pin, const char* name);
bool setInputDisplayName(uint8_t pin, const char* displayName);
bool setInputUnits(uint8_t pin, Units units);
bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue);
bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length);
bool enableInput(uint8_t pin, bool enable);
bool enableInputAlarm(uint8_t pin, bool enable);
bool enableInputDisplay(uint8_t pin, bool enable);
bool clearInput(uint8_t pin);

// ===== CALIBRATION OVERRIDES =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c);
bool setInputCalibrationLookup(uint8_t pin, float bias);
bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax);
bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c);
bool clearInputCalibration(uint8_t pin);

// ===== PERSISTENCE =====
bool saveInputConfig();               // Save all inputs to EEPROM
bool loadInputConfig();               // Load all inputs from EEPROM
void resetInputConfig();              // Clear all inputs and EEPROM

// ===== RUNTIME =====
void readAllInputs();                 // Read all enabled inputs
Input* getInputByPin(uint8_t pin);   // Find input by pin number
Input* getInputByIndex(uint8_t index); // Get input by array index
uint8_t getInputIndex(uint8_t pin);  // Get array index for pin

// ===== INFO =====
void printInputInfo(uint8_t pin);    // Print detailed input information
void listAllInputs();                // List all active inputs
void listApplicationPresets();       // List available Applications
void listSensors();                  // List available Sensors

#endif // INPUT_MANAGER_H