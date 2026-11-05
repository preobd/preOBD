/*
 * oled_sh1107.h - Driver for the 128x64 SH1107 I2C OLED (Adafruit #4650)
 *
 * Built for the Adafruit FeatherWing OLED 128x64 (SH1107, fixed address
 * 0x3C). The SH1107 is natively 64 columns x 128 rows; the wing mounts it
 * sideways, so the driver draws in landscape (128 wide x 64 tall, (0,0)
 * top-left with the Feather's USB port on the left) and rotates on the way
 * into a 1 KB framebuffer. flush() sends only the 64-byte panel pages that
 * changed.
 *
 * Text uses a 6x8 cell (5x7 glyph + 1 blank column): 21 columns x 8 rows.
 * setTextGrid() places a character grid anywhere on the screen, so the 20x4
 * LCD layout (lcd_layout.h) draws on it unchanged with room between rows.
 *
 * Like the LCD, the OLED is a client of the I2C bus: begin() takes the
 * TwoWire the bus manager handed over and never calls Wire.begin() or
 * setClock() itself (#351).
 *
 * A failed I2C write takes the panel offline instead of being retried on
 * every page, so a pulled connector costs one failed transaction, not a
 * screenful (STM32's Wire can wait 100 ms per failure, past the RUN-mode
 * watchdog). While offline, service() re-probes at most every OLED_RETRY_MS
 * and re-initializes a panel that comes back.
 */

#ifndef OLED_SH1107_H
#define OLED_SH1107_H

#include <Arduino.h>
#include <Wire.h>

#define OLED_I2C_ADDRESS   0x3C
#define OLED_WIDTH         128      // landscape
#define OLED_HEIGHT        64
#define OLED_CHAR_WIDTH    6        // 5-column glyph + 1 blank column
#define OLED_CHAR_HEIGHT   8
#define OLED_RETRY_MS      5000

class OledSh1107 : public Print {
public:
    // Probe the panel and initialize it. False (and offline) when nothing
    // answers at `addr`; service() retries.
    bool begin(TwoWire* wire, uint8_t addr = OLED_I2C_ADDRESS);

    // True while the panel is answering.
    bool online() const { return _online; }

    // At most once per OLED_RETRY_MS: re-probe and re-initialize an offline
    // panel, or check that an online one is still on and set up (it may have
    // been replugged without a write failing). Returns online().
    bool service();

    // Push changed framebuffer pages to the panel. No-op while offline.
    void flush();

    // Character grid: cell (col,row) has its top-left pixel at
    // (x0 + col*6, y0 + row*rowPitch). Default: 21x8 from (0,0), pitch 8.
    void setTextGrid(uint8_t x0, uint8_t y0, uint8_t rowPitch);
    void setCursor(uint8_t col, uint8_t row);

    void clear();                    // framebuffer only; flush() shows it
    void setPower(bool on);          // panel on/off (its RAM is kept)

    // True once after the panel was (re)initialized, blanking it: the caller
    // must redraw everything, not only what changed.
    bool takeReset();

    // Print: printable ASCII and LCD_DEGREE_CHAR (0xDF); anything else draws
    // a blank cell. Characters past the right edge are dropped.
    size_t write(uint8_t c) override;
    using Print::write;

private:
    void setPixel(uint8_t x, uint8_t y, bool on);
    void drawGlyph(uint8_t x, uint8_t y, uint8_t c);
    bool command(const uint8_t* cmds, uint8_t n);
    bool init();
    void fail();

    uint8_t  _buf[OLED_WIDTH * OLED_HEIGHT / 8];   // native layout: 16 pages x 64
    uint16_t _dirty = 0xFFFF;        // one bit per native page
    TwoWire* _wire = nullptr;
    uint8_t  _addr = OLED_I2C_ADDRESS;
    uint8_t  _gridX = 0, _gridY = 0, _rowPitch = OLED_CHAR_HEIGHT;
    uint8_t  _x = 0, _y = 0;         // pixel position of the next character
    bool     _online = false;
    bool     _wantOn = true;         // what setPower() last asked for
    bool     _reset = false;         // see takeReset()
    uint32_t _lastTry = 0;
};

#endif // OLED_SH1107_H
