/*
 * cli_tokenize.cpp - see cli_tokenize.h
 */
#include "cli_tokenize.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// One pass, reading at `in` and writing at `out`. `out` never passes `in`,
// because the characters that are skipped (quotes, backslashes, extra spaces)
// only ever make it fall further behind, so the words fit in the line itself.
FLASHMEM uint8_t cliTokenize(char* line, char* argv[], uint8_t maxArgs) {
    if (line == nullptr) return 0;

    uint8_t argc = 0;
    bool quoted = false;    // inside "...": spaces are part of the word
    bool literal = false;   // the previous character was a backslash
    bool inWord = false;
    char* out = line;

    for (const char* in = line; *in != '\0'; in++) {
        const char c = *in;
        bool endsWord = false;

        if (literal) {
            literal = false;                  // kept as is, whatever it is
        } else if (c == '\\') {
            literal = true;
            continue;
        } else if (c == '"') {
            quoted = !quoted;
            endsWord = true;
        } else if (c == ' ' && !quoted) {
            endsWord = true;
        }

        if (endsWord) {
            if (inWord) {
                *out++ = '\0';
                inWord = false;
            }
            continue;
        }

        if (!inWord) {
            if (argc == maxArgs) break;       // the rest of the line is ignored
            argv[argc++] = out;
            inWord = true;
        }
        *out++ = c;
    }

    if (inWord) *out = '\0';
    return argc;
}
