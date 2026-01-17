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

// Helper: Check if command is read-only (allowed in RUN mode)
bool isReadOnlyCommand(const char* cmdName);

#endif // _COMMAND_TABLE_H_
