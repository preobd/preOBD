/*
 * analog_pins.cpp - Canonical analog A-name <-> physical pin mapping.
 *
 * The table is just the board's own A* constants collected into an indexable
 * array — the Arduino core exposes A0..An only as individual compile-time
 * macros, with no runtime "give me A<n>" accessor, and its analogInputToDigital
 * Pin() macro is ambiguous on Teensy 4.x (returns its argument unchanged for
 * pins 14..27/38..41, so it disagrees with the A14..A17 constants). A static_
 * assert ties this table to the core values so it can't silently drift (#302).
 *
 * On AVR the A-names ARE contiguous (A0 + n), so the mapping is formula-based
 * with no lookup table — nothing to store on the SRAM-starved mega.
 */

#include "analog_pins.h"
#include <Arduino.h>

#if defined(__IMXRT1062__) || defined(__MK64FX512__) || defined(__MK66FX1M0__) || \
    defined(__MK20DX256__) || defined(__MK20DX128__)
    // Teensy: A-names are non-contiguous — resolve through the core constants.
    #define ANALOG_PINS_TABLE 1
    #if defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41)
        static constexpr uint8_t kAnalog[] = {
            A0, A1, A2, A3, A4, A5, A6, A7, A8,
            A9, A10, A11, A12, A13, A14, A15, A16, A17 };
        static_assert(kAnalog[14] == 38 && kAnalog[17] == 41,
                      "Teensy 4.1 A14..A17 must map to pins 38..41");
    #elif defined(__IMXRT1062__)  // Teensy 4.0
        static constexpr uint8_t kAnalog[] = {
            A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13 };
    #else  // Teensy 3.1/3.2/3.5/3.6 — conventional header set A0..A9
        static constexpr uint8_t kAnalog[] = {
            A0, A1, A2, A3, A4, A5, A6, A7, A8, A9 };
    #endif
    static constexpr uint8_t kCount = sizeof(kAnalog) / sizeof(kAnalog[0]);
#elif defined(__AVR_ATmega2560__)
    static constexpr uint8_t kCount = 16;  // A0..A15, contiguous from A0
#elif defined(__AVR_ATmega328P__)
    static constexpr uint8_t kCount = 6;   // A0..A5, contiguous from A0
#else
    // Generic / ESP32: assume contiguous A-names (true on ESP32-S3: A0==GPIO1).
    static constexpr uint8_t kCount = MAX_INPUTS;
#endif

uint8_t analogPinCount() {
    return kCount;
}

uint8_t analogPinForIndex(uint8_t index) {
    if (index >= kCount) return 0xFF;
#ifdef ANALOG_PINS_TABLE
    return kAnalog[index];
#else
    return (uint8_t)(A0 + index);
#endif
}

int8_t analogIndexForPin(uint8_t pin) {
#ifdef ANALOG_PINS_TABLE
    for (uint8_t i = 0; i < kCount; i++) {
        if (kAnalog[i] == pin) return (int8_t)i;
    }
    return -1;
#else
    if (pin >= A0 && pin < (uint8_t)(A0 + kCount)) return (int8_t)(pin - A0);
    return -1;
#endif
}
