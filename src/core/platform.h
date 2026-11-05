/*
 * platform.h - Hardware-derived platform constants (ADC, voltage, divider).
 * Feature flags and sizing live in boards/<board>/board.h — one per board.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// ===== AUTOMATIC PLATFORM DETECTION =====
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
    // Arduino Uno - 5V system with 1.1V internal reference
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 5.0
    #define SYSTEM_VOLTAGE_MV 5000  // For preprocessor comparisons
    #define AREF_VOLTAGE 5.0    // Using VCC as reference
    #define ADC_RESOLUTION 10   // 10-bit ADC (0-1023)
    #define ADC_MAX_VALUE 1023
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    // Arduino Mega - 5V system with 1.1V internal reference
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 5.0
    #define SYSTEM_VOLTAGE_MV 5000
    #define AREF_VOLTAGE 5.0
    #define ADC_RESOLUTION 10
    #define ADC_MAX_VALUE 1023
#elif defined(__MK20DX256__) || defined(__MK20DX128__)
    // Teensy 3.x - 3.3V system with 1.2V internal reference
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
#elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
    // Teensy 3.5/3.6 - can be 3.3V or 5V tolerant
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
#elif defined(__IMXRT1062__)
    // Teensy 4.x - can be 3.3V or 5V tolerant
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3    // Using VCC as reference
    #define ADC_RESOLUTION 12   // 12-bit ADC (0-4095)
    #define ADC_MAX_VALUE 4095
#elif defined(ARDUINO_SAM_DUE)
    // Arduino Due
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 12
    #define ADC_MAX_VALUE 4095
#elif defined(ARDUINO_ARCH_STM32)
    // STM32 (Feather F405) - 3.3V system, 12-bit SAR ADC referenced to VDDA
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #undef ADC_RESOLUTION   // the core's is analogRead()'s 10-bit default; setupADC() selects 12
    #define ADC_RESOLUTION 12
    #define ADC_MAX_VALUE 4095
#elif defined(ESP32)
    // ESP32
    #define I2C_CLOCK_SPEED "100kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 12
    #define ADC_MAX_VALUE 4095
#else
    // Default safe values for unknown platforms
    #define I2C_CLOCK_SPEED "400kHz"
    #define SYSTEM_VOLTAGE 3.3
    #define SYSTEM_VOLTAGE_MV 3300
    #define AREF_VOLTAGE 3.3
    #define ADC_RESOLUTION 10
    #define ADC_MAX_VALUE 1023
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

// The analog pins themselves (A<n> -> pin) live in pinctrl/board_pins.h (#570).

// ===== PLATFORM INITIALIZATION FUNCTIONS =====
void setupADC();  // Configure ADC settings for the current platform

// Called every loop() pass. STM32: re-arms USB CDC reception (platform.cpp).
#if defined(ARDUINO_ARCH_STM32)
void platformLoopService();
#else
inline void platformLoopService() {}
#endif

// analogRead() for a preOBD pin number. Everywhere but STM32 that is the
// core's own number. The STM32 core names analog pins 0xC0 + n, which preOBD
// reserves for virtual CAN inputs, so there preOBD uses their digital numbers
// (pinctrl/board_pins.cpp) and this maps them back to the ADC channel.
#if defined(ARDUINO_ARCH_STM32)
int platformAnalogRead(uint8_t pin);
#else
#define platformAnalogRead(pin) analogRead(pin)
#endif

// noTone() that leaves the pin LOW. The STM32 core's noTone() only pauses the
// tone timer, so the pin stays wherever the last toggle left it: HIGH half
// the time, which keeps an active buzzer (or a transistor-driven one)
// sounding after the alarm is silenced (#100 bench).
#if defined(ARDUINO_ARCH_STM32)
#define platformNoTone(pin) do { noTone(pin); digitalWrite((pin), LOW); } while (0)
#else
#define platformNoTone(pin) noTone(pin)
#endif

// Reboot into the chip's ROM USB bootloader, so a new image can be uploaded
// without a BOOT0 jumper (SYSTEM REBOOT BOOTLOADER). STM32 only: Teensy has
// its own program button and protocol, AVR and ESP32 reset into theirs.
#if defined(ARDUINO_ARCH_STM32)
#define PLATFORM_HAS_BOOTLOADER_REBOOT 1
void platformRebootToBootloader();
#endif

#endif