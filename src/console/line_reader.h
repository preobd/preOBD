/*
 * line_reader.h - The console's line reader: one per console source (#147, #624)
 *
 * Assembles received bytes into a command line. There is no history, cursor
 * editing or completion: a terminal that wants them edits the line on the
 * host and sends it whole. The echo is exactly what was typed, and a command
 * name must be typed in full (`VERS` is an unknown command).
 *
 *   - printable ASCII (32-126) is stored and echoed; any other byte is dropped
 *   - '\r' or '\n' ends the line and echoes "\r\n"; a CRLF or LFCR pair counts
 *     once
 *   - '\b' / 0x7F deletes the last char, echoing ESC [ D, ESC [ P (cursor
 *     left, delete char)
 *   - an `ESC [ ... <final 0x40-0x7E>` sequence (arrow keys, Home, Del, ...)
 *     is ignored
 *   - a line holds cap - 1 chars plus its NUL (127 in 128 bytes); further
 *     chars are dropped silently. cliTokenize() splits it in place without
 *     writing past the NUL, so no slack is kept
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

/** Drop the line, ready for the next one; a CR/LF pair split across it still counts once. */
void lineReaderClear(LineReader& r);

/** Back to the state lineReaderInit() left it in: no line, no pending CR or ESC sequence. */
void lineReaderReset(LineReader& r);

#endif // LINE_READER_H
