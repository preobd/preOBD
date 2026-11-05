/*
 * eeprom_store.h - The EEPROM object the config store reads and writes
 *
 * On AVR, Teensy and ESP32 this is the core's EEPROM library, unchanged.
 *
 * STM32 has no EEPROM. The STM32duino library emulates one in a flash
 * sector, but its EEPROM.put() erases and rewrites that whole sector for
 * every byte (a 128 KB sector on the F405, ~1-2 s each), and every byte read
 * copies the whole emulation buffer out of flash first. So on STM32 the
 * EEPROM object here is a RAM image of the store: loaded from flash on first
 * use, edited by get/put like the real thing, and written back to flash only
 * by commit(): one erase per save instead of one per byte.
 *
 * Writers wrap a save in an EepromBatch. The outermost batch to close
 * commits, so `SAVE` (inputs + system config) costs one erase, not two.
 * On every other platform a batch does nothing: their put() is durable.
 */

#ifndef EEPROM_STORE_H
#define EEPROM_STORE_H

#if defined(ARDUINO_ARCH_STM32)

#include <stdint.h>
#include <string.h>

#ifndef EEPROM_TOTAL_BYTES
#error "eeprom_store.h: STM32 boards must define EEPROM_TOTAL_BYTES in board.h"
#endif

class FlashEeprom {
public:
    template <typename T> T& get(int addr, T& t) {
        load();
        if (inRange(addr, sizeof(T))) memcpy(&t, image_ + addr, sizeof(T));
        return t;
    }
    template <typename T> const T& put(int addr, const T& t) {
        load();
        if (inRange(addr, sizeof(T)) && memcmp(image_ + addr, &t, sizeof(T)) != 0) {
            memcpy(image_ + addr, &t, sizeof(T));
            dirty_ = true;
        }
        return t;
    }
    uint8_t read(int addr) { load(); return inRange(addr, 1) ? image_[addr] : 0xFF; }
    void write(int addr, uint8_t v) { put(addr, v); }
    uint16_t length() const { return EEPROM_TOTAL_BYTES; }

    // Writes the image to flash if anything changed. False if the erase or a
    // program step failed, or the flash doesn't read back as written.
    bool commit();

private:
    static bool inRange(int addr, size_t n) {
        return addr >= 0 && (size_t)addr + n <= EEPROM_TOTAL_BYTES;
    }
    void load();

    uint8_t image_[EEPROM_TOTAL_BYTES] __attribute__((aligned(4)));
    bool loaded_ = false;
    bool dirty_ = false;
};

extern FlashEeprom EEPROM;

#else
#include <EEPROM.h>
#endif

// Groups EEPROM writes into one commit (see the top of this file).
// The commit happens when the outermost batch closes, after the save
// functions inside it have returned, so whoever opened that batch checks
// eepromCommitFailed() before reporting success.
class EepromBatch {
public:
#if defined(ARDUINO_ARCH_STM32)
    EepromBatch();
    ~EepromBatch();
#else
    EepromBatch() {}   // put() is already durable here
#endif
private:
    EepromBatch(const EepromBatch&);
    EepromBatch& operator=(const EepromBatch&);
};

// True if the last outermost EepromBatch failed to write flash. Always false
// where put() is durable.
#if defined(ARDUINO_ARCH_STM32)
bool eepromCommitFailed();
#else
inline bool eepromCommitFailed() { return false; }
#endif

#endif // EEPROM_STORE_H
