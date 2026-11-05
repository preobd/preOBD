/*
 * lcd_pcf8574.cpp - HD44780 LCD on a PCF8574 backpack (see lcd_pcf8574.h)
 */

#include "../../config.h"

#if INCLUDE_LCD

#include "lcd_pcf8574.h"
#include "lcd_layout.h"      // LCD_COLS, LCD_ROWS
#include "../../drivers/bus_manager.h"

// PCF8574 output bits on the backpack.
static const uint8_t BIT_RS = 0x01;
static const uint8_t BIT_EN = 0x04;
static const uint8_t BIT_BACKLIGHT = 0x08;

// HD44780 commands.
static const uint8_t CMD_CLEAR        = 0x01;   // takes 1.52 ms
static const uint8_t CMD_ENTRY_MODE   = 0x06;   // increment, no shift
static const uint8_t CMD_DISPLAY_ON   = 0x0C;   // display on, no cursor, no blink
static const uint8_t CMD_FUNCTION_SET = 0x28;   // 4-bit, 2 lines (4 on a 20x4), 5x8
static const uint8_t CMD_SET_DDRAM    = 0x80;

// A 20x4 is two 40-character DDRAM lines folded in half: rows 0/1 start at
// 0x00/0x40, rows 2/3 one row's width on (0x14/0x54).

uint8_t LcdPcf8574::control(bool isData) const {
    return (uint8_t)((isData ? BIT_RS : 0) | (_backlight ? BIT_BACKLIGHT : 0));
}

bool LcdPcf8574::transmit(const uint8_t* bytes, uint8_t n) {
    if (!_online) return false;
    _wire->beginTransmission(_addr);
    _wire->write(bytes, n);
    if (_wire->endTransmission() != 0) {
        fail();
        return false;
    }
    return true;
}

// The high four bits of `nibble` on D4-D7: presented with EN high, latched
// when EN falls.
bool LcdPcf8574::sendNibble(uint8_t nibble) {
    const uint8_t b = (uint8_t)((nibble & 0xF0) | control(false));
    const uint8_t bytes[] = { (uint8_t)(b | BIT_EN), b };
    return transmit(bytes, sizeof(bytes));
}

bool LcdPcf8574::send(uint8_t value, bool isData) {
    const uint8_t hi = (uint8_t)((value & 0xF0) | control(isData));
    const uint8_t lo = (uint8_t)((uint8_t)(value << 4) | control(isData));
    const uint8_t bytes[] = { (uint8_t)(hi | BIT_EN), hi, (uint8_t)(lo | BIT_EN), lo };
    return transmit(bytes, sizeof(bytes));
}

// A failed write. On the STM32 core it also leaves Wire refusing every later
// transaction for 100 ms each, for every device on the bus (#586), so the bus
// manager re-initializes the peripheral; elsewhere that's a no-op.
void LcdPcf8574::fail() {
    _online = false;
    _lastTry = millis();
    clearI2CFault();
}

// The HD44780's reset-by-instruction into 4-bit mode (datasheet figure 24),
// then the same setup LiquidCrystal_PCF8574::begin() sent.
bool LcdPcf8574::init() {
    _online = true;
    const uint8_t idle = control(false);
    if (!transmit(&idle, 1)) return false;
    delay(50);                       // > 40 ms after power-up before the first command
    if (!sendNibble(0x30)) return false;
    delayMicroseconds(4500);
    if (!sendNibble(0x30)) return false;
    delayMicroseconds(200);
    if (!sendNibble(0x30)) return false;
    delayMicroseconds(200);
    if (!sendNibble(0x20)) return false;
    if (!send(CMD_FUNCTION_SET, false)) return false;
    if (!send(CMD_DISPLAY_ON, false)) return false;
    clear();
    send(CMD_ENTRY_MODE, false);
    return _online;
}

bool LcdPcf8574::begin(TwoWire* wire, uint8_t addr) {
    _wire = wire;
    _addr = addr;
    _online = false;
    _lastTry = millis();
    if (!_wire) return false;
    // A zero-byte probe first: writing to a panel that isn't there is what
    // wedges the STM32's Wire, and a bare address probe doesn't.
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) return false;
    return init();
}

bool LcdPcf8574::service() {
    if (_online) return true;
    if (!_wire) return false;
    if ((uint32_t)(millis() - _lastTry) < LCD_RETRY_MS) return false;
    _lastTry = millis();
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) {
        // Another client may have jammed the STM32 bus (#586), and a jammed
        // bus fails this probe without touching the wires: clear it, or the
        // panel could never come back.
        clearI2CFault();
        return false;
    }
    return init();
}

void LcdPcf8574::clear() {
    if (send(CMD_CLEAR, false)) delayMicroseconds(1600);
}

void LcdPcf8574::setCursor(uint8_t col, uint8_t row) {
    if (row >= LCD_ROWS) return;
    const uint8_t base = (uint8_t)(((row & 1) ? 0x40 : 0x00) + ((row & 2) ? LCD_COLS : 0));
    send((uint8_t)(CMD_SET_DDRAM | (base + col)), false);
}

void LcdPcf8574::setBacklight(bool on) {
    _backlight = on;
    const uint8_t b = control(false);
    transmit(&b, 1);
}

size_t LcdPcf8574::write(uint8_t c) {
    return send(c, true) ? 1 : 0;
}

#endif // INCLUDE_LCD
