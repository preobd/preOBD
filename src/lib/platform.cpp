/*
 * platform.cpp - Platform-specific initialization functions
 */

#include <Arduino.h>
#include "platform.h"
#include "message_api.h"
#include "log_tags.h"

void setupADC() {
    // Configure analog reference based on platform
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        // Arduino Uno/Nano - 5V system
        analogReference(DEFAULT);  // 5V reference
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        // Arduino Mega - 5V system
        analogReference(DEFAULT);  // 5V reference
    #elif defined(__MK20DX256__) || defined(__MK20DX128__)
        // Teensy 3.1/3.2 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);  // Average 4 samples for stability
    #elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
        // Teensy 3.5/3.6 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
    #elif defined(__IMXRT1062__)
        // Teensy 4.0/4.1 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
    #elif defined(ARDUINO_SAM_DUE)
        // Arduino Due - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
    #elif defined(ESP32)
        // ESP32 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        // Set attenuation for full 0-3.3V range
        // ADC_11db = 0-3.3V (default but less accurate)
        // ADC_6db = 0-2.2V (more accurate, use if voltage divider allows)
        analogSetAttenuation(ADC_11db);
        msg.debug.info(TAG_ADC, "Attenuation set to 11db (0-3.3V range)");
        msg.debug.info(TAG_ADC, "ESP32 ADC is non-linear, consider calibration");
    #else
        // Unknown platform - use default
        msg.debug.warn(TAG_SYSTEM, "Unknown platform, using default ADC settings");
        #ifdef ADC_RESOLUTION
        analogReadResolution(ADC_RESOLUTION);
        #endif
    #endif
}
