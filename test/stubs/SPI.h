// SPI.h — host stub for native unit tests. See Wire.h for why this exists:
// bus_manager.h unconditionally `#include <SPI.h>`.
#ifndef NATIVE_TEST_SPI_H
#define NATIVE_TEST_SPI_H

class SPIClass {
public:
    void begin() {}
};

// Only `SPI` is ever referenced by the native build (see Wire.h); SPI1/SPI2
// are declared for parity with the real header.
inline SPIClass SPI;
inline SPIClass SPI1;
inline SPIClass SPI2;

#endif // NATIVE_TEST_SPI_H
