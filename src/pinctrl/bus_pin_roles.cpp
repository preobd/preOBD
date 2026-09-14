/*
 * bus_pin_roles.cpp - see bus_pin_roles.h
 */

#include "bus_pin_roles.h"
#include "../hal/bus_defaults.h"

// Labels live in flash, not RAM.
//
// F() is NOT equivalent across platforms: on AVR it expands to PSTR() and the
// literal goes to flash, but on Teensy 4.x it is a bare cast
// (cores/teensy4/WString.h), so an F() literal is an ordinary string that lands
// in RAM1. Measured: these ~21 labels as F() cost 1024 B of RAM1 on teensy41,
// the opposite of what the F() spelling implies. PROGMEM is a real flash
// section on Teensy 4 and its .progmem is memory-mapped, so Print reads these
// directly; casting a PROGMEM pointer to __FlashStringHelper* is exactly what
// AVR's own F() does, and printing stays correct on both.
#define ROLE_LABEL(sym, text) static const char sym[] PROGMEM = text
ROLE_LABEL(L_I2C0_SDA, "I2C0 SDA");  ROLE_LABEL(L_I2C0_SCL, "I2C0 SCL");
ROLE_LABEL(L_I2C1_SDA, "I2C1 SDA");  ROLE_LABEL(L_I2C1_SCL, "I2C1 SCL");
ROLE_LABEL(L_I2C2_SDA, "I2C2 SDA");  ROLE_LABEL(L_I2C2_SCL, "I2C2 SCL");
ROLE_LABEL(L_SPI0_MOSI, "SPI0 MOSI"); ROLE_LABEL(L_SPI0_MISO, "SPI0 MISO"); ROLE_LABEL(L_SPI0_SCK, "SPI0 SCK");
ROLE_LABEL(L_SPI1_MOSI, "SPI1 MOSI"); ROLE_LABEL(L_SPI1_MISO, "SPI1 MISO"); ROLE_LABEL(L_SPI1_SCK, "SPI1 SCK");
ROLE_LABEL(L_SPI2_MOSI, "SPI2 MOSI"); ROLE_LABEL(L_SPI2_MISO, "SPI2 MISO"); ROLE_LABEL(L_SPI2_SCK, "SPI2 SCK");
ROLE_LABEL(L_CAN0_TX, "CAN0 TX");    ROLE_LABEL(L_CAN0_RX, "CAN0 RX");
ROLE_LABEL(L_CAN1_TX, "CAN1 TX");    ROLE_LABEL(L_CAN1_RX, "CAN1 RX");
ROLE_LABEL(L_CAN2_TX, "CAN2 TX");    ROLE_LABEL(L_CAN2_RX, "CAN2 RX");
#undef ROLE_LABEL

static inline const __FlashStringHelper* asLabel(const char* p) {
    return reinterpret_cast<const __FlashStringHelper*>(p);
}

const __FlashStringHelper* busPinRole(uint8_t pin) {
    // 0xFF is getDefault*()'s "this platform has no such bus/pin" sentinel, so
    // it would otherwise match every absent bus. Reject it before comparing.
    if (pin == 0xFF) return nullptr;

    // getDefault*() returns 0xFF for a bus index this platform does not have,
    // and the guard above already excluded 0xFF, so every comparison below is a
    // harmless no-match on smaller platforms — no #if guards needed.
    if (pin == getDefaultI2CSDA(0)) return asLabel(L_I2C0_SDA);
    if (pin == getDefaultI2CSCL(0)) return asLabel(L_I2C0_SCL);
    if (pin == getDefaultI2CSDA(1)) return asLabel(L_I2C1_SDA);
    if (pin == getDefaultI2CSCL(1)) return asLabel(L_I2C1_SCL);
    if (pin == getDefaultI2CSDA(2)) return asLabel(L_I2C2_SDA);
    if (pin == getDefaultI2CSCL(2)) return asLabel(L_I2C2_SCL);

    if (pin == getDefaultSPIMOSI(0)) return asLabel(L_SPI0_MOSI);
    if (pin == getDefaultSPIMISO(0)) return asLabel(L_SPI0_MISO);
    if (pin == getDefaultSPISCK(0))  return asLabel(L_SPI0_SCK);
    if (pin == getDefaultSPIMOSI(1)) return asLabel(L_SPI1_MOSI);
    if (pin == getDefaultSPIMISO(1)) return asLabel(L_SPI1_MISO);
    if (pin == getDefaultSPISCK(1))  return asLabel(L_SPI1_SCK);
    if (pin == getDefaultSPIMOSI(2)) return asLabel(L_SPI2_MOSI);
    if (pin == getDefaultSPIMISO(2)) return asLabel(L_SPI2_MISO);
    if (pin == getDefaultSPISCK(2))  return asLabel(L_SPI2_SCK);

    if (pin == getDefaultCANTX(0)) return asLabel(L_CAN0_TX);
    if (pin == getDefaultCANRX(0)) return asLabel(L_CAN0_RX);
    if (pin == getDefaultCANTX(1)) return asLabel(L_CAN1_TX);
    if (pin == getDefaultCANRX(1)) return asLabel(L_CAN1_RX);
    if (pin == getDefaultCANTX(2)) return asLabel(L_CAN2_TX);
    if (pin == getDefaultCANRX(2)) return asLabel(L_CAN2_RX);

    return nullptr;
}
