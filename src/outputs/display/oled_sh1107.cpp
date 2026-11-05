/*
 * oled_sh1107.cpp - 128x64 SH1107 OLED driver (see oled_sh1107.h)
 */

#include "../../config.h"

#if INCLUDE_OLED

#include "oled_sh1107.h"
#include "oled_font5x7.h"
#include "lcd_layout.h"     // LCD_DEGREE_CHAR
#include "../../drivers/bus_manager.h"

// I2C control bytes: what follows is a command stream, or display RAM.
static const uint8_t CTRL_CMD  = 0x00;
static const uint8_t CTRL_DATA = 0x40;

// Native panel geometry: 64 columns per page, 16 pages of 8 rows.
static const uint8_t NATIVE_COLS  = OLED_HEIGHT;      // 64
static const uint8_t NATIVE_PAGES = OLED_WIDTH / 8;   // 16

// Data bytes per I2C transaction: the control byte plus 16 fits AVR's
// 32-byte Wire buffer with room to spare.
static const uint8_t DATA_PER_TX = 16;

// Power-up sequence for the FeatherWing's panel, as Adafruit_SH1107 sends
// it (64x128 native, display offset 0x60). DC-DC 0x8A is what lights the
// glass: the SSD1306's 0x8D/0x14 charge-pump command is ACKed by the SH1107
// and does nothing, so a panel set up that way answers but stays dark.
static const uint8_t INIT_SEQ[] PROGMEM = {
    0xAE,         // display off
    0xD5, 0x51,   // clock divide / oscillator
    0x20,         // page addressing
    0x81, 0x4F,   // contrast
    0xAD, 0x8A,   // DC-DC
    0xA0,         // segment remap: normal
    0xC0,         // COM scan: ascending
    0xDC, 0x00,   // display start line 0
    0xD3, 0x60,   // display offset
    0xD9, 0x22,   // pre-charge
    0xDB, 0x35,   // VCOM deselect
    0xA8, 0x3F,   // multiplex: 64
    0xA4,         // display follows RAM
    0xA6,         // normal (not inverted)
};

bool OledSh1107::command(const uint8_t* cmds, uint8_t n) {
    if (!_wire) return false;
    _wire->beginTransmission(_addr);
    _wire->write(CTRL_CMD);
    _wire->write(cmds, n);
    if (_wire->endTransmission() != 0) {
        fail();
        return false;
    }
    return true;
}

// A failed write. On the STM32 core it also leaves Wire refusing every later
// transaction, re-probes included, so the bus manager re-initializes the
// peripheral (#586); elsewhere that's a no-op.
void OledSh1107::fail() {
    _online = false;
    _lastTry = millis();
    clearI2CFault();
}

bool OledSh1107::init() {
    // One transaction, so a command's parameter can never be split from its
    // opcode (23 bytes with the control byte fits any Wire buffer).
    uint8_t buf[sizeof(INIT_SEQ)];
    memcpy_P(buf, INIT_SEQ, sizeof(buf));
    if (!command(buf, sizeof(buf))) return false;
    _online = true;
    // The panel's RAM is unknown: send a blank screen. Whatever was drawn
    // before is stale; the caller redraws after a (re)connect.
    clear();
    _dirty = 0xFFFF;
    flush();
    if (!_online) return false;
    _reset = true;                   // the caller has a blank panel to redraw
    if (!_wantOn) return true;       // DISPLAY DISABLE: stays off until enabled
    delay(100);                      // Adafruit: settle before display on
    const uint8_t on = 0xAF;
    command(&on, 1);
    return _online;
}

bool OledSh1107::begin(TwoWire* wire, uint8_t addr) {
    _wire = wire;
    _addr = addr;
    _lastTry = millis();
    if (!_wire) return false;
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) return false;   // offline; service() retries
    return init();
}

// Display-off flag in the status byte (read with no control byte).
static const uint8_t STATUS_DISPLAY_OFF = 0x40;

bool OledSh1107::service() {
    if (!_wire) return false;
    if ((uint32_t)(millis() - _lastTry) < OLED_RETRY_MS) return _online;
    _lastTry = millis();
    if (_online) {
        // A write that fails is the only other way a lost panel is noticed,
        // and nothing is written while the screen doesn't change. A panel
        // replugged in that window powers up off and unconfigured but ACKs
        // every write after it, so it would stay dark: read its status
        // instead, and set it up again if it says it's off when it should be on.
        if (_wire->requestFrom(_addr, (uint8_t)1) != 1) { fail(); return false; }
        const uint8_t status = (uint8_t)_wire->read();
        if (_wantOn && (status & STATUS_DISPLAY_OFF)) return init();
        return true;
    }
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) {
        clearI2CFault();             // as in LcdPcf8574::service() (#586)
        return false;
    }
    return init();
}

void OledSh1107::flush() {
    if (!_online || !_dirty) return;
    for (uint8_t p = 0; p < NATIVE_PAGES; p++) {
        if (!(_dirty & (1u << p))) continue;
        const uint8_t addr[] = { (uint8_t)(0xB0 + p), 0x10, 0x00 };
        if (!command(addr, sizeof(addr))) return;
        const uint8_t* src = _buf + (uint16_t)p * NATIVE_COLS;
        for (uint8_t i = 0; i < NATIVE_COLS; i += DATA_PER_TX) {
            _wire->beginTransmission(_addr);
            _wire->write(CTRL_DATA);
            _wire->write(src + i, DATA_PER_TX);
            if (_wire->endTransmission() != 0) { fail(); return; }
        }
        _dirty &= (uint16_t)~(1u << p);
    }
}

// Landscape (x right, y down) to the panel's native layout, the same mapping
// as Adafruit_GFX rotation 1 on a 64x128 SH1107.
void OledSh1107::setPixel(uint8_t x, uint8_t y, bool on) {
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    const uint8_t nx = (uint8_t)(NATIVE_COLS - 1 - y);
    uint8_t& b = _buf[(uint16_t)(x >> 3) * NATIVE_COLS + nx];
    const uint8_t bit = (uint8_t)(1u << (x & 7));
    const uint8_t was = b;
    b = on ? (uint8_t)(b | bit) : (uint8_t)(b & ~bit);
    if (b != was) _dirty |= (uint16_t)(1u << (x >> 3));
}

void OledSh1107::drawGlyph(uint8_t x, uint8_t y, uint8_t c) {
    const uint8_t* glyph = nullptr;
    if (c >= OLED_FONT_FIRST && c <= OLED_FONT_LAST) {
        glyph = OLED_FONT5X7 + (c - OLED_FONT_FIRST) * OLED_FONT_WIDTH;
    } else if (c == LCD_DEGREE_CHAR) {
        glyph = OLED_GLYPH_DEGREE;
    }
    for (uint8_t col = 0; col < OLED_CHAR_WIDTH; col++) {
        const uint8_t bits = (glyph && col < OLED_FONT_WIDTH) ? pgm_read_byte(glyph + col) : 0;
        for (uint8_t row = 0; row < OLED_CHAR_HEIGHT; row++) {
            setPixel((uint8_t)(x + col), (uint8_t)(y + row), (bits >> row) & 1);
        }
    }
}

void OledSh1107::setTextGrid(uint8_t x0, uint8_t y0, uint8_t rowPitch) {
    _gridX = x0;
    _gridY = y0;
    _rowPitch = rowPitch;
}

void OledSh1107::setCursor(uint8_t col, uint8_t row) {
    _x = (uint8_t)(_gridX + col * OLED_CHAR_WIDTH);
    _y = (uint8_t)(_gridY + row * _rowPitch);
}

void OledSh1107::clear() {
    for (uint16_t i = 0; i < sizeof(_buf); i++) {
        if (_buf[i]) { _buf[i] = 0; _dirty |= (uint16_t)(1u << (i / NATIVE_COLS)); }
    }
    setCursor(0, 0);
}

void OledSh1107::setPower(bool on) {
    const bool wasOn = _wantOn;
    _wantOn = on;
    if (!_online) return;
    // Turning back on after DISPLAY DISABLE sets the panel up again: it may
    // have been replugged while off, and service() isn't run while disabled.
    if (on && !wasOn) { init(); return; }
    const uint8_t c = on ? 0xAF : 0xAE;
    command(&c, 1);
}

bool OledSh1107::takeReset() {
    const bool r = _reset;
    _reset = false;
    return r;
}

size_t OledSh1107::write(uint8_t c) {
    if (_x + OLED_CHAR_WIDTH > OLED_WIDTH || _y + OLED_CHAR_HEIGHT > OLED_HEIGHT) return 0;
    drawGlyph(_x, _y, c);
    _x = (uint8_t)(_x + OLED_CHAR_WIDTH);
    return 1;
}

#endif // INCLUDE_OLED
