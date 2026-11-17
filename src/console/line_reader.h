/*
 * line_reader.h - Minimal CLI line editor for boards without INCLUDE_CLI_EDITING (#147)
 *
 * Assembles typed bytes into a command line the way embedded-cli does, without
 * its history, cursor editing, completion or 820 B buffer. (embedded-cli
 * completes a unique command prefix on Enter even with autocomplete off, so
 * `VERS` ran VERSION and echoed a trailing space. Here `VERS` is an unknown
 * command, and the echo is exactly what was typed.)
 *
 *   - printable ASCII (32-126) is stored and echoed; any other byte is dropped
 *     (embedded-cli's isDisplayableChar)
 *   - '\r' or '\n' ends the line and echoes "\r\n"; a CRLF or LFCR pair counts
 *     once
 *   - '\b' / 0x7F deletes the last char, echoing the same escape sequence as
 *     embedded-cli
 *   - an `ESC [ ... <final 0x40-0x7E>` sequence (arrow keys, Home, Del, ...)
 *     is ignored
 *   - a line keeps two bytes of slack, because embeddedCliTokenizeArgs()
 *     writes one byte past the NUL when it tokenizes in place, so cap 128
 *     holds 126 chars; further chars are dropped silently, as embedded-cli does
 *
 * Pure (no Arduino dependency), so test/test_line_reader runs it on the host.
 */
#ifndef LINE_READER_H
#define LINE_READER_H

#include <stdint.h>

typedef void (*LineReaderWriteFn)(char c);

struct LineReader {
    char*    buf;       // caller-owned, `cap` bytes
    uint16_t cap;
    uint16_t len;
    char     lastChar;
    bool     escape;    // inside an ESC [ ... sequence
};

void lineReaderInit(LineReader& r, char* buf, uint16_t cap);

/**
 * Feed one received byte; echo goes through `write`. Returns true when the
 * byte ended a line: `r.buf` then holds it, NUL-terminated, with `r.len`
 * chars (0 for an empty Enter). Handle it, then call lineReaderClear().
 */
bool lineReaderFeed(LineReader& r, char c, LineReaderWriteFn write);

void lineReaderClear(LineReader& r);

#endif // LINE_READER_H
