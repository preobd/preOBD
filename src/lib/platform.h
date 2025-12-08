/*
 * platform.h - Automatic platform detection and configuration
 * DO NOT EDIT - This file automatically detects your hardware
 */

#ifndef PLATFORM_H
#define PLATFORM_H

// ===== AUTOMATIC PLATFORM DETECTION =====
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
    // Arduino Uno - 5V system with 1.1V internal reference
    #define PLATFORM_NAME "Arduino Uno"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 5.0
    #define SYSTEM_VOLTAGE_MV 5000  // For preprocessor comparisons
    #define AREF_VOLTAGE 5.0    // Using VCC as reference
    #define ADC_RESOLUTION 10   // 10-bit ADC (0-1023)
    #define ADC_MAX_VALUE 1023
    #define MAX_INPUTS 6        // Arduino Uno analog inputs
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    // Arduino Mega - 5V system with 1.1V internal reference
    #define PLATFORM_NAME "Arduino Mega 2560"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 5.0
    #define SYSTEM_VOLTAGE_MV 5000
    #define AREF_VOLTAGE 5.0
    #define ADC_RESOLUTION 10
    #define ADC_MAX_VALUE 1023
    #define MAX_INPUTS 16       // Arduino Mega analog inputs
#elif defined(__MK20DX256__) || defined(__MK20DX128__)
    // Teensy 3.x - 3.3V system with 1.2V internal reference
    #define PLATFORM_NAME "Teensy 3.x"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
    #define MAX_INPUTS 24       // Teensy 3.x analog inputs
#elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
    // Teensy 3.5/3.6 - can be 3.3V or 5V tolerant
    #define PLATFORM_NAME "Teensy 3.5/3.6"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
    #define MAX_INPUTS 32       // Teensy 3.5/3.6 analog inputs
#elif defined(__IMXRT1062__)
    // Teensy 4.x - can be 3.3V or 5V tolerant
    #if defined(ARDUINO_TEENSY41)
        #define PLATFORM_NAME "Teensy 4.1"
    #elif defined(ARDUINO_TEENSY40)
        #define PLATFORM_NAME "Teensy 4.0"
    #else
        #define PLATFORM_NAME "Teensy 4.x"
    #endif
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
    #define MAX_INPUTS 40       // Teensy 4.x analog inputs
#elif defined(ARDUINO_SAM_DUE)
    // Arduino Due
    #define PLATFORM_NAME "Arduino Due"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 12
    #define ADC_MAX_VALUE 4095
    #define MAX_INPUTS 12       // Arduino Due analog inputs
#elif defined(ESP32)
    // ESP32
    #define PLATFORM_NAME "ESP32"
    #define I2C_CLOCK_SPEED "100kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 12
    #define ADC_MAX_VALUE 4095
    #define MAX_INPUTS 32       // ESP32 analog inputs
#else
    // Default safe values for unknown platforms
    #define PLATFORM_NAME "Unknown"
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 10
    #define ADC_MAX_VALUE 1023
    #define MAX_INPUTS 8        // Default/Unknown platform
#endif

// ===== VOLTAGE DIVIDER CONFIGURATION =====
// Automatically configured based on system voltage
#if SYSTEM_VOLTAGE_MV == 3300
    // 3.3V system configuration
    // 100kΩ from battery + → junction → 22kΩ to ground
    // Max input: 12V × (22/(100+22)) = 2.16V (safe for 3.3V ADC)
    #define VOLTAGE_DIVIDER_R1 100000.0
    #define VOLTAGE_DIVIDER_R2 22000.0
#else
    // 5V system configuration
    // 100kΩ from battery + → junction → 6.8kΩ to ground  
    // Max input: 12V × (6.8/(100+6.8)) = 0.76V
    #define VOLTAGE_DIVIDER_R1 100000.0
    #define VOLTAGE_DIVIDER_R2 6800.0
#endif

#define VOLTAGE_DIVIDER_RATIO ((VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2)

// ===== PLATFORM INITIALIZATION FUNCTIONS =====
void setupADC();  // Configure ADC settings for the current platform

#endif