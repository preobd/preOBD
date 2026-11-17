/*
 * cli_tokenize.h - Split a command line into words, in place (#624)
 *
 * The rules (unchanged from the embedded-cli tokenizer it replaces):
 *   - words are separated by spaces; runs of spaces count once, and leading
 *     or trailing spaces make no word
 *   - a double quote starts or ends a quoted part, where spaces are kept, and
 *     also ends the current word: `a"b c"d` is three words (a, "b c", d), and
 *     `""` is no word at all, never an empty one
 *   - a backslash makes the next character literal, inside quotes or out:
 *     `\"`, `\ ` (a space inside the word) and `\\`; a backslash at the very
 *     end is dropped
 *   - a quote left open runs to the end of the line
 *   - words past `maxArgs` are ignored
 *
 * The words are written over the line itself (each NUL-terminated) and
 * `argv` points into it, so the line must stay alive while argv is used.
 * Pure C++, no Arduino dependency: test/test_cli_tokenize runs it on the host.
 */
#ifndef CLI_TOKENIZE_H
#define CLI_TOKENIZE_H

#include <stdint.h>

/** Split `line` into at most `maxArgs` words; returns how many. */
uint8_t cliTokenize(char* line, char* argv[], uint8_t maxArgs);

#endif // CLI_TOKENIZE_H
