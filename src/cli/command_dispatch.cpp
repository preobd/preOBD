/*
 * command_dispatch.cpp - Table-driven command routing (mechanism only)
 *
 * Holds the two dispatch functions split out from command_table.cpp so the
 * routing mechanism can be unit-tested on the host (see test/test_dispatch).
 * Everything here is generic over whatever COMMANDS[] table and handlers are
 * linked in; the production table + handlers stay in command_table.cpp, and
 * the native test links a synthetic table with spy handlers.
 *
 * Dependency discipline: this TU intentionally pulls only lightweight headers
 * (command_table.h, command_dispatch.h, system_mode.h). User-facing error text
 * is emitted through the command_dispatch.h reporter seam rather than `msg`,
 * which keeps the heavy message_router/message_api chain out of the test
 * build. streq/streq_P are forward-declared here (their canonical declarations
 * live in command_helpers.h, definitions in command_helpers.cpp) for the same
 * reason — including command_helpers.h would drag in the message subsystem.
 */

#include <Arduino.h>   // pgm_read_ptr / pgm_read_byte (PROGMEM access)
#include <string.h>
#include <ctype.h>

#include "command_table.h"
#include "command_dispatch.h"
#include "../core/system_mode.h"
#include "../core/config_generation.h"

// Case-insensitive compares — see command_helpers.h/.cpp for the definitions.
// Forward-declared (not #included) to keep this TU lightweight; see header note.
bool streq(const char* a, const char* b);
bool streq_P(const char* a, const char* b_P);

// Subcommand dispatcher (see command_table.h).
//
// Subcommand tables are declared PROGMEM by callers (BUS_SUBCOMMANDS,
// SYSTEM_SUBCOMMANDS) so they live in flash on AVR. On AVR a direct
// `table[i].token` read returns a flash address dereferenced as RAM —
// garbage. Read both fields via pgm_read_ptr; on Teensy/ESP32 PROGMEM is
// a no-op and pgm_read_ptr is a plain dereference. Token strings are
// likewise PROGMEM-resident, so compare with streq_P.
int dispatchSubcommand(const Subcommand* table, uint8_t tableLen,
                       const char* token, const char* commandName,
                       int argc, const char* const* argv, int tokenIndex) {
    for (uint8_t i = 0; i < tableLen; i++) {
        const char* entryToken = (const char*)pgm_read_ptr(&table[i].token);
        if (streq_P(token, entryToken)) {
            // RUN-mode gate: per-subcommand. Top-level commands like SYSTEM and
            // LOG are reachable in RUN mode (configModeOnly=false) so users can
            // run read-only verbs (SYSTEM STATUS, LOG TAGS); the destructive
            // siblings (SYSTEM RESET, SYSTEM REBOOT, etc.) carry
            // runModeAllowed=false and are blocked here.
            bool runOk = (bool)pgm_read_byte(&table[i].runModeAllowed);
            if (!runOk && isInRunMode()) {
                dispatchReportSubcmdConfigRequired(commandName, token);
                return 1;
            }
            // Past the CONFIG gate = a mutation; bump regardless of the
            // handler's result (a spurious client refresh is cheap, a missed
            // partial mutation is not).
            if (!runOk) configGenerationBump();
            SubcommandHandler h = (SubcommandHandler)pgm_read_ptr(&table[i].handler);
            return h(argc, argv, tokenIndex);
        }
    }
    dispatchReportUnknownSubcommand(commandName, token);
    return 1;
}

// Main command dispatcher.
//
// Mode gating is now table-driven: COMMANDS[i].configModeOnly is the single
// source of truth. Commands that are wholly read-only (HELP, INFO, LIST, ...)
// or wholly mode-switching (CONFIG, RUN) carry configModeOnly=false. Commands
// that mutate state carry configModeOnly=true and are rejected in RUN mode.
//
// For commands that are *partly* mutating (SYSTEM, LOG), configModeOnly=false
// at this level lets the dispatcher reach the handler; the handler's own
// subcommand table — or, for cmd_log, inline isInRunMode() checks — gates the
// destructive verbs. This is what fixes the SYSTEM RESET CONFIRM safety bug:
// previously the wholesale "SYSTEM" allowlist let *any* SYSTEM subcommand run
// in RUN mode.
int dispatchCommand(int argc, const char* const* argv) {
    if (argc == 0) return 0;

    // Convert command name to uppercase for comparison
    char cmdUpper[32];
    strncpy(cmdUpper, argv[0], sizeof(cmdUpper) - 1);
    cmdUpper[sizeof(cmdUpper) - 1] = '\0';
    for (char* p = cmdUpper; *p; p++) *p = toupper(*p);

    // Look up command in table; gate before dispatch.
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        if (streq(cmdUpper, COMMANDS[i].name)) {
            if (COMMANDS[i].configModeOnly && isInRunMode()) {
                dispatchReportConfigRequired(cmdUpper);
                return 1;
            }
            if (COMMANDS[i].configModeOnly) configGenerationBump();  // see dispatchSubcommand
            return COMMANDS[i].handler(argc, argv);
        }
    }

    // Command not found
    dispatchReportUnknownCommand(argv[0]);
    return 1;
}
