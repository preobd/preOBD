/*
 * json_import_stream.cpp - see json_import_stream.h
 */
#include "json_import_stream.h"

#if SUPPORTS_JSON_IMPORT_STREAM
#include "command_helpers.h"          // printSaveReminder
#include "../config/json_config.h"    // loadConfigFromJSON
#include "../core/message_api.h"
#include <stdlib.h>
#include <strings.h>                  // strncasecmp (POSIX); not on the AVR toolchain

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static const unsigned long IDLE_TIMEOUT_MS = 45000;

// One import at a time
static struct {
    bool          active;
    bool          counted;       // counted mode, else sentinel mode
    uint8_t       source;        // TransportID it belongs to
    char*         buf;
    size_t        len;
    size_t        target;        // counted mode: bytes expected
    size_t        lineStart;     // sentinel mode: start of the current line
    // Sentinel mode: the buffer overflowed. The rest of the paste and its END
    // JSON line are still coming; ending the import now would run them as
    // commands, so they are swallowed until the idle timeout ends it.
    bool          overflowed;
    unsigned long lastByteMs;
} s_import = {};

FLASHMEM void jsonImportCancel() {
    free(s_import.buf);
    s_import.buf = nullptr;
    s_import.active = false;
    s_import.counted = false;
    s_import.len = 0;
    s_import.target = 0;
    s_import.lineStart = 0;
    s_import.overflowed = false;
}

FLASHMEM bool jsonImportBegin(uint8_t source, size_t target) {
    if (s_import.active) return false;
    s_import.buf = (char*)malloc(JSON_IMPORT_MAX_BYTES);
    if (s_import.buf == nullptr) {
        msg.control.println(F("ERROR: Not enough heap for JSON import buffer"));
        return false;
    }
    s_import.active = true;
    s_import.counted = (target > 0);
    s_import.source = source;
    s_import.target = target;
    s_import.len = 0;
    s_import.lineStart = 0;
    s_import.overflowed = false;
    s_import.lastByteMs = millis();
    if (s_import.counted) {
        msg.control.print(F("Ready for "));
        msg.control.print(target);
        msg.control.println(F(" bytes"));
    } else {
        msg.control.println(F("Paste JSON, then type 'END JSON' on its own line:"));
    }
    return true;
}

FLASHMEM bool jsonImportActive() { return s_import.active; }

FLASHMEM uint8_t jsonImportSource() { return s_import.source; }

FLASHMEM bool jsonImportIdleExpired() {
    // Signed subtraction: safe across a millis() wraparound (~49 days)
    return s_import.active && (int32_t)(millis() - s_import.lastByteMs) >= (int32_t)IDLE_TIMEOUT_MS;
}

// Both modes end here: apply, report, free
FLASHMEM static void finish() {
    if (loadConfigFromJSON(s_import.buf)) {
        msg.control.println(F("Applied"));
        printSaveReminder();
    } else {
        msg.control.println(F("ERROR: Failed to apply JSON configuration"));
    }
    jsonImportCancel();
}

FLASHMEM void jsonImportFeed(char c) {
    if (!s_import.active) return;
    s_import.lastByteMs = millis();
    if (s_import.overflowed) return;

    if (s_import.counted) {
        // Capacity has one byte to spare: cmd_json accepts at most
        // JSON_IMPORT_MAX_BYTES - 1
        s_import.buf[s_import.len++] = c;
        if (s_import.len >= s_import.target) {
            s_import.buf[s_import.len] = '\0';
            finish();
        }
        return;
    }

    // Sentinel mode echoes, for the human typing or pasting (\r before \n so
    // the terminal returns to column 0). Counted mode doesn't: its sender
    // discards it, and over BLE it would double the payload's airtime.
    if (c == '\n') msg.control.print('\r');
    msg.control.print(c);

    if (s_import.len >= JSON_IMPORT_MAX_BYTES - 1) {
        msg.control.println(F("\r\nERROR: JSON exceeds buffer limit"));
        msg.control.print(F("  Max: "));
        msg.control.print(JSON_IMPORT_MAX_BYTES);
        msg.control.println(F(" bytes"));
        s_import.overflowed = true;
        return;
    }
    s_import.buf[s_import.len++] = c;

    if (c == '\n') {
        // Is the line just completed "END JSON" (trimmed, any case)?
        const char* line = s_import.buf + s_import.lineStart;
        size_t n = s_import.len - s_import.lineStart;
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
        if (n == 8 && strncasecmp(line, "END JSON", 8) == 0) {
            s_import.buf[s_import.lineStart] = '\0';     // the JSON ends where END JSON starts
            finish();
        } else {
            s_import.lineStart = s_import.len;
        }
    }
}
#endif
