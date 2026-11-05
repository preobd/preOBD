/*
 * sd_fs.h - The SD card library for this platform: the global `SD` and `File`
 *
 * The Arduino SD library (and the Teensy and ESP32 cores' versions of it)
 * everywhere, except STM32, where the card slot is wired to the SDIO
 * peripheral, which that SPI-only library can't drive. There it is
 * STM32duino's STM32SD (SDIO + FatFs), which offers the same SD/File names
 * with two differences the callers have to respect:
 *  - FILE_WRITE is FatFs FA_WRITE: it writes from offset 0 and creates the
 *    file only if it doesn't exist, where Arduino SD appends. preOBD only
 *    ever opens a fresh file for writing (a new log name; SAVE FILE removes
 *    the old file first), so both behave the same here.
 *  - No capacity or media-present call: sd_manager.cpp asks FatFs directly.
 *  - It never reports a failed write: File::write() returns a short count but
 *    never sets Print's write error, and File::flush() drops f_sync()'s
 *    result. So a pulled card logged "0 errors" forever (#100 bench). Callers
 *    that need to know use sdWriteFailed() and sdFlush() below, which read
 *    FatFs directly on STM32 and keep the Arduino behaviour elsewhere.
 */

#ifndef SD_FS_H
#define SD_FS_H

#if defined(ARDUINO_ARCH_STM32)
#include <STM32SD.h>
#else
#include <SD.h>
#endif
#include <SPI.h>

// ---------------------------------------------------------------------------
// Mounting a card on an external chip-select (#381)
//
// The bus manager owns the SPI bus: acquireSPI() claims MOSI/MISO/SCK and
// starts the peripheral, and the card must be mounted on THAT SPIClass. Each
// SD library takes the port its own way, so this is the one place that knows
// how; sd_manager.cpp only ever calls sdBeginSpi().
//
// SD_LIB_SPI_ANY_PORT says whether the library can use any SPI instance. One
// that can't drives the global `SPI` only, and the caller must refuse another
// bus rather than mount a card on wires the bus manager never started.
// ---------------------------------------------------------------------------
#if defined(ARDUINO_ARCH_STM32)
// STM32SD drives the SDIO peripheral only; its begin() argument is a card-
// DETECT pin, so an external SD_PIN would silently mount the on-board slot
// instead. An SPI card on STM32 needs a different SD library: refuse the build.
#  if defined(SD_PIN) && SD_PIN != 254
#    error "SD_PIN: an SPI SD card isn't supported on STM32 (STM32SD is SDIO only). Use the board's built-in slot."
#  endif
#elif defined(CORE_TEENSY)
#  define SD_LIB_SPI_ANY_PORT 1
#elif defined(ESP32)
#  define SD_LIB_SPI_ANY_PORT 1
#else
#  define SD_LIB_SPI_ANY_PORT 0     // stock Arduino SD: always `SPI`
#endif

#if !defined(ARDUINO_ARCH_STM32)
// Mount the card behind `cs` on `spi`, clocked at up to `hz`. `spi` must
// already be started by the bus manager.
inline bool sdBeginSpi(uint8_t cs, SPIClass& spi, uint32_t hz) {
#if defined(CORE_TEENSY)
    // Teensy's SD wrapper has no port parameter, but its SdFat is public and
    // takes one. Going around SDClass::begin() skips two things it does:
    // the file timestamp callback (set here), and latching "card present",
    // which totalSize()/usedSize() and mediaPresent() depend on.
    // mediaPresent() latches it: on a card not yet seen it remounts once via
    // sdfs.restart(), which reuses this config, port included.
    FsDateTime::setCallback(SDClass::dateTime);
    if (!SD.sdfs.begin(SdSpiConfig(cs, SHARED_SPI, hz, &spi))) return false;
    return SD.mediaPresent();
#elif defined(ESP32)
    return SD.begin(cs, spi, hz);
#else
    if (&spi != &SPI) return false;   // see SD_LIB_SPI_ANY_PORT
    return SD.begin(hz, cs);
#endif
}
#endif

// Has a write to this file failed? On STM32, FatFs latches the first disk
// error on the file object and refuses further writes.
inline bool sdWriteFailed(File& f) {
#if defined(ARDUINO_ARCH_STM32)
    return f._fil && f._fil->err;
#else
    return f.getWriteError();
#endif
}

// flush(), saying whether it reached the card. Elsewhere a failed flush shows
// up through getWriteError() on the next write, so this reports success.
//
// On STM32 one retry: f_sync() first checks the card with CMD13, and the card
// can answer with a stale "previous command's CRC was bad" flag that it clears
// on that same read. FatFs reports it as FR_INVALID_OBJECT, though the card
// and file are fine and an immediate second f_sync() succeeds. This hit the
// first flush of every log on the Feather (#583). A pulled card fails both.
inline bool sdFlush(File& f) {
#if defined(ARDUINO_ARCH_STM32)
    if (!f._fil) return false;
    FRESULT r = f_sync(f._fil);
    if (r == FR_INVALID_OBJECT || r == FR_NOT_READY) r = f_sync(f._fil);
    return r == FR_OK;
#else
    f.flush();
    return true;
#endif
}

// Print adapter for a log file: write through it instead of the File, so a
// write that loses bytes is caught. FatFs f_write() runs the same CMD13 check
// as f_sync(), and when the card's stale flag lands there (#583) it writes
// nothing and records no error on the file, so sdWriteFailed() can't see it
// and a value, comma or newline would vanish from a row. On STM32 a refused
// write (0 bytes) is retried once; it was refused before anything was
// written, so the retry can't duplicate data. Any write still short is
// remembered for the caller.
class SdWriter : public Print {
public:
    explicit SdWriter(File& f) : f_(f) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override {
        size_t w = f_.write(buf, n);
#if defined(ARDUINO_ARCH_STM32)
        if (w == 0 && n) w = f_.write(buf, n);
#endif
        if (w != n) short_ = true;
        return w;
    }
    bool shortWrite() const { return short_; }
    void reset() { short_ = false; }
private:
    File& f_;
    bool short_ = false;
};

#endif // SD_FS_H
