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
