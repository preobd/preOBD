/*
 * line_reader.cpp - see line_reader.h
 */
#include "line_reader.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

FLASHMEM static void writeStr(LineReaderWriteFn write, const char* s) {
    while (*s) write(*s++);
}

FLASHMEM void lineReaderInit(LineReader& r, char* buf, uint16_t cap) {
    r.buf = buf;
    r.cap = cap;
    lineReaderReset(r);
}

FLASHMEM void lineReaderReset(LineReader& r) {
    r.lastChar = '\0';
    r.escape = false;
    lineReaderClear(r);
}

FLASHMEM void lineReaderClear(LineReader& r) {
    r.len = 0;
    if (r.cap > 0) r.buf[0] = '\0';
}

FLASHMEM bool lineReaderFeed(LineReader& r, char c, LineReaderWriteFn write) {
    const char last = r.lastChar;
    r.lastChar = c;

    if (r.escape) {
        if (c >= 64 && c <= 126) r.escape = false;   // final byte ends the sequence
        return false;
    }
    if (last == 0x1B && c == '[') {
        r.escape = true;
        return false;
    }

    if (c == '\r' || c == '\n') {
        if ((last == '\r' && c == '\n') || (last == '\n' && c == '\r')) return false;
        writeStr(write, "\r\n");
        r.buf[r.len] = '\0';
        return true;
    }
    if (c == '\b' || c == 0x7F) {
        if (r.len > 0) {
            writeStr(write, "\x1B[D\x1B[P");   // cursor left, delete char
            r.buf[--r.len] = '\0';
        }
        return false;
    }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        if (r.len + 1 >= r.cap) return false;           // room for the NUL
        r.buf[r.len++] = c;
        r.buf[r.len] = '\0';
        write(c);
    }
    // '\t', ESC itself and bytes >= 0x80 are dropped
    return false;
}
