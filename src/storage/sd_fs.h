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
inline bool sdFlush(File& f) {
#if defined(ARDUINO_ARCH_STM32)
    return f._fil && f_sync(f._fil) == FR_OK;
#else
    f.flush();
    return true;
#endif
}

#endif // SD_FS_H
