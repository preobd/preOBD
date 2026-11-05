/*
 * board_pins.cpp - The board's pins and name, from the Arduino core. See
 * board_pins.h for what comes from where (#570).
 */

#include "board_pins.h"
#include <Arduino.h>

#ifndef FLASHMEM
#define FLASHMEM
#endif

#if !defined(NUM_DIGITAL_PINS) || !defined(NUM_ANALOG_INPUTS)
    #error "board_pins: this core defines no NUM_DIGITAL_PINS / NUM_ANALOG_INPUTS"
#endif
#if defined(ESP32) && !defined(SOC_GPIO_VALID_GPIO_MASK)
    #error "board_pins: ESP32 core without SOC_GPIO_VALID_GPIO_MASK (would accept GPIOs the chip lacks)"
#endif

// The analog inputs the firmware offers: the core's count, unless board.h says
// fewer can be read (ESP32: ADC2 is unusable while the radio runs).
#ifdef BOARD_ANALOG_USABLE
    #define BOARD_ANALOG_COUNT BOARD_ANALOG_USABLE
    static_assert(BOARD_ANALOG_USABLE <= NUM_ANALOG_INPUTS,
                  "BOARD_ANALOG_USABLE exceeds the core's NUM_ANALOG_INPUTS");
#else
    #define BOARD_ANALOG_COUNT NUM_ANALOG_INPUTS
#endif
#if BOARD_ANALOG_COUNT > 27
    #error "board_pins: kAnalog lists A0..A26; extend it for this board"
#endif

// The core's own A-name constants, in PROGMEM (flash on AVR; nothing on the
// mega's 8 KB of RAM). A constant the core doesn't define fails the build.
static const uint8_t kAnalog[] PROGMEM = {
    A0,
#if BOARD_ANALOG_COUNT > 1
    A1,
#endif
#if BOARD_ANALOG_COUNT > 2
    A2,
#endif
#if BOARD_ANALOG_COUNT > 3
    A3,
#endif
#if BOARD_ANALOG_COUNT > 4
    A4,
#endif
#if BOARD_ANALOG_COUNT > 5
    A5,
#endif
#if BOARD_ANALOG_COUNT > 6
    A6,
#endif
#if BOARD_ANALOG_COUNT > 7
    A7,
#endif
#if BOARD_ANALOG_COUNT > 8
    A8,
#endif
#if BOARD_ANALOG_COUNT > 9
    A9,
#endif
#if BOARD_ANALOG_COUNT > 10
    A10,
#endif
#if BOARD_ANALOG_COUNT > 11
    A11,
#endif
#if BOARD_ANALOG_COUNT > 12
    A12,
#endif
#if BOARD_ANALOG_COUNT > 13
    A13,
#endif
#if BOARD_ANALOG_COUNT > 14
    A14,
#endif
#if BOARD_ANALOG_COUNT > 15
    A15,
#endif
#if BOARD_ANALOG_COUNT > 16
    A16,
#endif
#if BOARD_ANALOG_COUNT > 17
    A17,
#endif
#if BOARD_ANALOG_COUNT > 18
    A18,
#endif
#if BOARD_ANALOG_COUNT > 19
    A19,
#endif
#if BOARD_ANALOG_COUNT > 20
    A20,
#endif
#if BOARD_ANALOG_COUNT > 21
    A21,
#endif
#if BOARD_ANALOG_COUNT > 22
    A22,
#endif
#if BOARD_ANALOG_COUNT > 23
    A23,
#endif
#if BOARD_ANALOG_COUNT > 24
    A24,
#endif
#if BOARD_ANALOG_COUNT > 25
    A25,
#endif
#if BOARD_ANALOG_COUNT > 26
    A26,
#endif
};
static_assert(sizeof(kAnalog) == BOARD_ANALOG_COUNT, "kAnalog must list every A-name");

// All cold (CLI and JSON paths): FLASHMEM keeps them out of Teensy 4 RAM1.
FLASHMEM uint8_t analogPinCount() {
    return BOARD_ANALOG_COUNT;
}

FLASHMEM uint8_t analogPinForIndex(uint8_t index) {
    if (index >= BOARD_ANALOG_COUNT) return 0xFF;
    return pgm_read_byte(&kAnalog[index]);
}

FLASHMEM int8_t analogIndexForPin(uint8_t pin) {
    for (uint8_t i = 0; i < BOARD_ANALOG_COUNT; i++) {
        if (pgm_read_byte(&kAnalog[i]) == pin) return (int8_t)i;
    }
    return -1;
}

FLASHMEM bool boardHasPin(uint8_t pin) {
#if defined(SOC_GPIO_VALID_GPIO_MASK)
    // ESP32: the GPIO count includes numbers the chip lacks (S3: 22..25).
    if (pin < NUM_DIGITAL_PINS && ((SOC_GPIO_VALID_GPIO_MASK >> pin) & 1ULL)) return true;
#else
    if (pin < NUM_DIGITAL_PINS) return true;
#endif
    return analogIndexForPin(pin) >= 0;
}

FLASHMEM const char* boardName() {
#if defined(ARDUINO_TEENSY41)
    return "TEENSY41";
#elif defined(ARDUINO_TEENSY40)
    return "TEENSY40";
#elif defined(ARDUINO_TEENSY36)
    return "TEENSY36";
#elif defined(ARDUINO_TEENSY35)
    return "TEENSY35";
#elif defined(ARDUINO_TEENSY32)
    return "TEENSY32";
#elif defined(ARDUINO_TEENSY31)
    return "TEENSY31";
#elif defined(ARDUINO_AVR_MEGA2560)
    return "MEGA2560";
#elif defined(ARDUINO_AVR_UNO)
    return "UNO";
#elif defined(ARDUINO_ESP32S3_DEV)
    return "ESP32S3";
#else
    #error "boardName(): add this board's ARDUINO_<BOARD> define (from its PlatformIO board JSON)"
#endif
}
