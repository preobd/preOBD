// Wire.h — host stub for native unit tests.
//
// bus_manager.h unconditionally `#include <Wire.h>` (it forward-declares
// TwoWire for its own API, but needs the real type where it holds
// `static TwoWire* active_i2c`). Real hardware calls are no-ops here — nothing
// on the host actually clocks I2C — this exists purely so bus_manager.cpp (and
// anything that includes bus_manager.h) compiles and links on the host.
#ifndef NATIVE_TEST_WIRE_H
#define NATIVE_TEST_WIRE_H

#include <stdint.h>

class TwoWire {
public:
    void begin() {}
    void setClock(uint32_t) {}
};

// One shared instance per name, like the real library's globals. Only `Wire`
// is ever referenced by the native build (bus_defaults.h's "generic Arduino"
// branch — no platform macro is defined on the host — has exactly one I2C
// bus), but Wire1/Wire2 are declared for parity with the real header in case a
// future stub or test references them.
inline TwoWire Wire;
inline TwoWire Wire1;
inline TwoWire Wire2;

#endif // NATIVE_TEST_WIRE_H
