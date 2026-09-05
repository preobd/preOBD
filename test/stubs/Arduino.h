// Arduino.h — host stub for native unit tests.
//
// Provides just enough of the Arduino surface that the project's pure logic
// modules compile unchanged on the host toolchain. NOT a faithful emulation:
// hardware calls are no-ops, PROGMEM is a no-op, and the clock is a plain
// test-settable global. Resolved ahead of the real <Arduino.h> via
// `-I test/stubs` in [env:native].
#ifndef NATIVE_TEST_ARDUINO_H
#define NATIVE_TEST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

// ----- Arduino scalar aliases -----
typedef uint8_t byte;
typedef bool    boolean;

// ----- Digital/analog pin constants (no hardware behind them) -----
#define HIGH         1
#define LOW          0
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2
#define A0 0

// ----- PROGMEM is meaningless on the host: flash == RAM here -----
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr)  (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_word(addr)  (*reinterpret_cast<const uint16_t*>(addr))
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#define pgm_read_float(addr) (*reinterpret_cast<const float*>(addr))
// const-preserving: real tables (Subcommand::token, ::handler) are read from
// `const`-qualified members, so the pointee const must survive the cast.
// Callers C-cast the const void* to the concrete pointer type they need.
#define pgm_read_ptr(addr)   (*reinterpret_cast<const void* const*>(addr))

// _P string ops map straight to their RAM equivalents on the host.
#define strlen_P     strlen
#define strcmp_P     strcmp
#define strncmp_P    strncmp
#define strcasecmp_P strcasecmp
#define strcpy_P     strcpy
#define strncpy_P    strncpy
#define memcpy_P     memcpy
#define snprintf_P   snprintf
#define sprintf_P    sprintf

// ----- F()/__FlashStringHelper -----
class __FlashStringHelper;
#define F(str) (reinterpret_cast<const __FlashStringHelper*>(str))

// ----- Test-controlled clock -----
// C++17 inline variable: one shared definition across all test TUs. Advance it
// directly from a test (e.g. `g_test_now = 1500;`) to drive time-based logic.
inline uint32_t g_test_now = 0;
inline uint32_t millis() { return g_test_now; }
inline uint32_t micros() { return g_test_now * 1000UL; }

// ----- No-op hardware -----
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return LOW; }
inline int  analogRead(uint8_t) { return 0; }
inline void analogWrite(uint8_t, int) {}
inline void yield() {}

// ----- Print base class (Arduino's Print) -----
// Concrete transports and watchdog.h's WatchdogKickingPrint derive from this.
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    size_t print(const char* s) {
        size_t n = 0;
        if (s) while (*s) n += write(static_cast<uint8_t>(*s++));
        return n;
    }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t println(const char* s) { return print(s) + print('\n'); }
    size_t println() { return print('\n'); }
};

// ----- Stream -----
// Minimal concrete interface: the three pure virtuals real code reads through.
// A forward decl suffices for the common `Stream*` member case; the full
// definition is needed when a test subclasses Stream (e.g. an in-memory bridge
// feeding the ELM327 parser).
class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
};

// ----- Number formatting used by TransportInterface's inline print() helpers.
// Parsed wherever transport_interface.h is included, so they must exist.
// (sprintf is the simplest faithful impl; the callers use fixed local buffers.)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
inline char* itoa(int value, char* str, int base) {
    if (base == 16)      sprintf(str, "%x", value);
    else if (base == 8)  sprintf(str, "%o", value);
    else                 sprintf(str, "%d", value);
    return str;
}
inline char* dtostrf(double val, signed char /*width*/, unsigned char prec,
                     char* s) {
    sprintf(s, "%.*f", static_cast<int>(prec), val);
    return s;
}
inline char* ltoa(long value, char* str, int base) {
    if (base == 16)      sprintf(str, "%lx", value);
    else if (base == 8)  sprintf(str, "%lo", value);
    else                 sprintf(str, "%ld", value);
    return str;
}
inline char* utoa(unsigned int value, char* str, int base) {
    if (base == 16)      sprintf(str, "%x", value);
    else if (base == 8)  sprintf(str, "%o", value);
    else if (base == 2) {
        char tmp[33]; int i = 0;
        if (value == 0) tmp[i++] = '0';
        while (value) { tmp[i++] = static_cast<char>('0' + (value & 1)); value >>= 1; }
        int j = 0;
        while (i) str[j++] = tmp[--i];
        str[j] = '\0';
    } else               sprintf(str, "%u", value);
    return str;
}
inline char* ultoa(unsigned long value, char* str, int base) {
    if (base == 16)      sprintf(str, "%lx", value);
    else if (base == 8)  sprintf(str, "%lo", value);
    else if (base == 2) {
        char tmp[33]; int i = 0;
        if (value == 0) tmp[i++] = '0';
        while (value) { tmp[i++] = static_cast<char>('0' + (value & 1)); value >>= 1; }
        int j = 0;
        while (i) str[j++] = tmp[--i];
        str[j] = '\0';
    } else               sprintf(str, "%lu", value);
    return str;
}
#pragma clang diagnostic pop

// Print-format base selectors (Arduino constants).
#ifndef HEX
#define HEX 16
#define DEC 10
#define OCT 8
#define BIN 2
#endif

#endif // NATIVE_TEST_ARDUINO_H
