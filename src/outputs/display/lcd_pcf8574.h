/*
 * lcd_pcf8574.h - Driver for an HD44780 character LCD on a PCF8574 I2C backpack
 *
 * The common blue/green "I2C LCD" module (address 0x27): the PCF8574's eight
 * outputs drive the panel in 4-bit mode, P0 = RS, P1 = RW, P2 = EN,
 * P3 = backlight, P4-P7 = D4-D7. Every character or command is one I2C
 * transaction of four bytes (each nibble presented with EN high, then latched
 * with EN low), the same bytes mathertel/LiquidCrystal_PCF8574 sent.
 *
 * It replaces that library because the library ignores endTransmission(), so
 * a missing panel was written to forever. On the STM32 core one failed write
 * leaves Wire waiting 100 ms on every later transaction, and a 20x4 redraw is
 * about 90 of them: past the watchdog in RUN and CONFIG alike (#586). Here,
 * like the OLED (oled_sh1107.h), a failed write takes the panel offline and
 * every later call is a no-op. While offline, service() re-probes at most every
 * LCD_RETRY_MS and re-initializes a panel that comes back.
 *
 * Like the OLED, the LCD is a client of the I2C bus: begin() takes the TwoWire
 * the bus manager handed over and never calls Wire.begin() or setClock()
 * itself (#351).
 */

#ifndef LCD_PCF8574_H
#define LCD_PCF8574_H

#include <Arduino.h>
#include <Wire.h>

#define LCD_I2C_ADDRESS    0x27
#define LCD_RETRY_MS       5000

class LcdPcf8574 : public Print {
public:
    // Probe the panel and initialize it. False (and offline) when nothing
    // answers at `addr`; service() retries.
    bool begin(TwoWire* wire, uint8_t addr = LCD_I2C_ADDRESS);

    // True while the panel is answering.
    bool online() const { return _online; }

    // Re-probe and re-initialize an offline panel, at most once per
    // LCD_RETRY_MS. Returns online(). Cheap when already online.
    bool service();

    void clear();
    void setCursor(uint8_t col, uint8_t row);
    void setBacklight(bool on);      // remembered while offline, applied on return

    size_t write(uint8_t c) override;
    using Print::write;

private:
    bool transmit(const uint8_t* bytes, uint8_t n);
    bool sendNibble(uint8_t nibble);
    bool send(uint8_t value, bool isData);
    uint8_t control(bool isData) const;
    bool init();
    void fail();

    TwoWire* _wire = nullptr;
    uint8_t  _addr = LCD_I2C_ADDRESS;
    bool     _online = false;
    bool     _backlight = true;
    uint32_t _lastTry = 0;
};

#endif // LCD_PCF8574_H
