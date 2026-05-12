/*
 * command_table.h - Table-driven command dispatch system
 * Replaces monolithic if/else command parsing with structured table
 */

#ifndef _COMMAND_TABLE_H_
#define _COMMAND_TABLE_H_

#include <stdint.h>

// Command handler function signature
// Returns 0 on success, non-zero on error
// argc: argument count (including command name at argv[0])
// argv: array of argument strings
typedef int (*CommandHandler)(int argc, const char* const* argv);

// Command registration structure
struct Command {
    const char* name;          // Command name (uppercase, stored in PROGMEM on AVR)
    CommandHandler handler;    // Function pointer to handler
    const char* help;          // Short help text (PROGMEM on AVR)
    bool configModeOnly;       // true = only available in CONFIG mode
};

// Command table (defined in command_table.cpp)
extern const Command COMMANDS[];
extern const uint8_t NUM_COMMANDS;

// Main command dispatcher
// Called by microrl execute callback
int dispatchCommand(int argc, const char* const* argv);

//-----------------------------------------------------------------------------
// Subcommand dispatch (used by handlers like SET / BUS / SYSTEM that themselves
// dispatch on a second token, e.g. "SET <pin> APPLICATION ..." or "BUS LIST").
//
// Leaf handler signature:
//   - argc/argv are forwarded verbatim from the top-level command (i.e. argv[0]
//     is still the top-level command name); the leaf decides what slice it
//     wants. `tokenIndex` tells the leaf which argv slot held the matched
//     subcommand token, so it can validate and reach into argv with no
//     bookkeeping at the call site.
//
// AVR / PROGMEM contract — read carefully:
//   - Subcommand tables MUST be declared `PROGMEM`. They live in flash on AVR;
//     mega2560 has very little RAM and a per-table cost of ~4 bytes/entry adds
//     up fast.
//   - Token strings (`Subcommand::token`) MUST also be PROGMEM-resident — i.e.
//     declared as `static const char PSTR_FOO[] PROGMEM = "FOO";` and stored in
//     the table as `PSTR_FOO`, not as a bare `"FOO"` literal. Bare literals end
//     up in `.data` (RAM-initialized from flash) and defeat the point.
//   - Always dispatch through `dispatchSubcommand()`. Do NOT open-code the
//     lookup loop — direct `table[i].token` / `table[i].handler` reads compile
//     fine but pull flash addresses through the RAM bus on AVR, silently
//     failing every match at runtime. The dispatcher uses `pgm_read_ptr` and
//     `streq_P` (case-insensitive RAM-vs-PROGMEM compare) to do this safely.
//   - On Teensy / ESP32, PROGMEM is a no-op and `pgm_read_ptr` is a plain
//     dereference, so the same code is portable.
//
// See SET_FIELDS / BUS_SUBCOMMANDS / SYSTEM_SUBCOMMANDS for canonical examples.
//-----------------------------------------------------------------------------
typedef int (*SubcommandHandler)(int argc, const char* const* argv, int tokenIndex);

struct Subcommand {
    const char* token;          // PROGMEM-resident subcommand keyword (uppercase)
    SubcommandHandler handler;  // Leaf handler
    bool runModeAllowed;        // true = also runnable in RUN mode (read-only verbs);
                                // false = CONFIG-only (mutates state or reboots)
};

// Look up `token` (case-insensitive) in `table` and call its handler.
// Returns the handler's return value, or 1 (and prints an error) if no match.
// `commandName` is used in the unknown-token error message (e.g. "SET", "BUS").
// `table` is expected to be PROGMEM-resident — see contract above.
int dispatchSubcommand(const Subcommand* table, uint8_t tableLen,
                       const char* token, const char* commandName,
                       int argc, const char* const* argv, int tokenIndex);

#endif // _COMMAND_TABLE_H_
