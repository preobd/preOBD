// EEPROM.h — host stub. system_config.h includes <EEPROM.h>; the native tests
// never persist, so this just needs to compile. get/put are no-op identities.
#ifndef NATIVE_TEST_EEPROM_H
#define NATIVE_TEST_EEPROM_H

#include <stdint.h>
#include <stddef.h>

// Real EEPROM.h (AVR / Teensy) defines E2END as the last EEPROM address;
// system_config.h derives EEPROM_TOTAL_BYTES from it. 4 KB-class part here.
#ifndef E2END
#define E2END 4095
#endif

class EEPROMClass {
public:
    uint8_t read(int) { return 0; }
    void    write(int, uint8_t) {}
    void    update(int, uint8_t) {}
    uint16_t length() { return 4096; }
    template <typename T> T&       get(int, T& v) { return v; }
    template <typename T> const T& put(int, const T& v) { return v; }
};

// C++17 inline variable: one shared instance, no separate definition needed.
inline EEPROMClass EEPROM;

#endif // NATIVE_TEST_EEPROM_H
