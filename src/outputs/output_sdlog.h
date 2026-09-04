/*
 * output_sdlog.h - SD card data logging module
 *
 * The four output-module entry points were previously declared only as bare
 * externs in output_manager.cpp, and closeSDLog() was declared nowhere at all
 * (and consequently never called). They live here now, alongside the status
 * accessors SYSTEM DIAG HW uses to report whether logging is actually running.
 */

#ifndef OUTPUT_SDLOG_H
#define OUTPUT_SDLOG_H

#include <Arduino.h>

// Output-module lifecycle. There is deliberately no per-input send hook: a wide
// CSV row needs the whole sample at once, so updateSDLog() owns the cadence and
// walks inputs[] itself (see the comment in output_sdlog.cpp).
void initSDLog();
void updateSDLog();
void closeSDLog();

// Status, for SYSTEM DIAG HW. Valid on every build — they report "off" when
// INCLUDE_SD_LOGGING is unset.
bool        sdLogIsActive();      // file open and healthy
bool        sdLogIsFaulted();     // latched after an unrecoverable write error
bool        sdLogCardRemoved();   // that fault was a pulled card, not a bad write
const char* sdLogFileName();      // current 8.3 name, or "(none)"
uint32_t    sdLogRowsWritten();
uint16_t    sdLogWriteErrors();

#endif // OUTPUT_SDLOG_H
