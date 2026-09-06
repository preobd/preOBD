// EEPROM.h — host stub. system_config.h includes <EEPROM.h>.
//
// Backed by a real byte array so a test can lay down a config store and have the
// firmware's own readers walk it (test_bus_needs does exactly that with
// storedInputBusNeeds()). Zero-initialized, which is what an unwritten part has:
// magic reads back 0, so a store nobody wrote is correctly seen as absent.
#ifndef NATIVE_TEST_EEPROM_H
#define NATIVE_TEST_EEPROM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Real EEPROM.h (AVR / Teensy) defines E2END as the last EEPROM address;
// system_config.h derives EEPROM_TOTAL_BYTES from it. 4 KB-class part here.
#ifndef E2END
#define E2END 4095
#endif

class EEPROMClass {
public:
    static const int SIZE = E2END + 1;

    uint8_t read(int a)              { return inRange(a, 1) ? cells[a] : 0; }
    void    write(int a, uint8_t v)  { if (inRange(a, 1)) cells[a] = v; }
    void    update(int a, uint8_t v) { write(a, v); }
    uint16_t length()                { return SIZE; }

    template <typename T> T& get(int a, T& v) {
        if (inRange(a, sizeof(T))) memcpy(&v, &cells[a], sizeof(T));
        return v;
    }
    template <typename T> const T& put(int a, const T& v) {
        if (inRange(a, sizeof(T))) memcpy(&cells[a], &v, sizeof(T));
        return v;
    }

    // Test-only: back to a blank part.
    void clear() { memset(cells, 0, sizeof(cells)); }

private:
    static bool inRange(int a, size_t n) {
        return a >= 0 && (size_t)a + n <= (size_t)SIZE;
    }
    uint8_t cells[SIZE] = {0};
};

// C++17 inline variable: one shared instance, no separate definition needed.
inline EEPROMClass EEPROM;

#endif // NATIVE_TEST_EEPROM_H
