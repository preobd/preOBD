/*
 * platform.cpp - Platform-specific initialization functions
 */

#include <Arduino.h>
#include "platform.h"
#include "message_api.h"
#include "log_tags.h"

// Analog-pin probe table (see platform.h). Built from the core A-name constants
// so the A<i>->pin mapping is correct on non-contiguous platforms. Conventional
// on-header analog sets only — this drives the SYSTEM DIAG PINS discovery sweep,
// not SET validation.
const uint8_t ANALOG_PINS[] PROGMEM = {
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
    // Arduino Uno/Nano — A0..A5
    A0, A1, A2, A3, A4, A5
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    // Arduino Mega — A0..A15
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15
#elif defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41)
    // Teensy 4.1 — A0..A17 (A14..A17 are pins 38..41, hence the named constants)
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15, A16, A17
#elif defined(__IMXRT1062__)
    // Teensy 4.0 — A0..A13
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13
#elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
    // Teensy 3.5/3.6 — conventional header set A0..A9 (higher A-names are
    // non-contiguous/special-purpose and omitted from the discovery sweep)
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9
#elif defined(__MK20DX256__) || defined(__MK20DX128__)
    // Teensy 3.1/3.2 — A0..A9
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9
#elif defined(ESP32)
    // ESP32 / ESP32-S3 — ADC1 only (A0..A9 == GPIO1..10 on S3). ADC2 (A10+) is
    // unusable while the radio is active, and these boards run BLE.
    A0, A1, A2, A3, A4, A5, A6, A7, A8, A9
#else
    // Unknown platform — conservative A0..A5
    A0, A1, A2, A3, A4, A5
#endif
};
const uint8_t ANALOG_PIN_COUNT = sizeof(ANALOG_PINS) / sizeof(ANALOG_PINS[0]);

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
