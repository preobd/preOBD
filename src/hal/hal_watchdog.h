/*
 * hal_watchdog.h - Hardware Abstraction Layer for Watchdog Timer
 * Part of the preOBD Hardware Abstraction Layer
 *
 * Provides a unified watchdog interface across all supported platforms:
 * - AVR (Arduino Uno, Mega)
 * - Teensy 3.x (Kinetis K20/K66)
 * - Teensy 4.x (IMXRT1062)
 * - ESP32
 * - Arduino Due (SAM3X)
 *
 * Usage:
 *   #include "hal/hal_watchdog.h"
 *   hal::watchdogEnable(2000);  // 2 second timeout
 *   hal::watchdogReset();       // Call in main loop
 */

#ifndef HAL_WATCHDOG_H
#define HAL_WATCHDOG_H

#include <stdint.h>

// Platform detection and include appropriate implementation
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    // AVR: Arduino Uno, Mega
    #include "platforms/watchdog_avr.h"

#elif defined(__MK20DX256__) || defined(__MK20DX128__) || \
      defined(__MK64FX512__) || defined(__MK66FX1M0__)
    // Teensy 3.x (Kinetis K20/K66)
    #include "platforms/watchdog_teensy3.h"

#elif defined(__IMXRT1062__)
    // Teensy 4.x (IMXRT1062)
    #include "platforms/watchdog_teensy4.h"

#elif defined(ARDUINO_SAM_DUE)
    // Arduino Due (SAM3X)
    #include "platforms/watchdog_due.h"

#elif defined(ESP32)
    // ESP32 variants (ESP32, ESP32-S3, ESP32-C3, etc.)
    #include "platforms/watchdog_esp32.h"

#else
    // Unknown platform - use stub (no-op)
    #include "platforms/watchdog_stub.h"

#endif

#endif // HAL_WATCHDOG_H
