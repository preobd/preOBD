/*
 * serial_config.cpp - Serial Command Interface
 *
 * With INCLUDE_CLI_EDITING, embedded-cli provides the line editor (history,
 * cursor movement). Without it (mega2560, #147), line_reader.h assembles the
 * line in CLI_CMD_BUFFER_SIZE bytes instead of embedded-cli's CLI_BUFFER_SIZE
 * arena. Both builds tokenize with embedded-cli's own embeddedCliTokenizeArgs,
 * so quoting and escapes are identical, and both call dispatchCommand().
 */

#include "../config.h"

#include "serial_config.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../core/message_router.h"
#include "../core/message_api.h"
#include "../storage/json_config.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if SUPPORTS_JSON_IMPORT_STREAM
#include <strings.h>  // strncasecmp (POSIX); not available on AVR toolchain
#endif

// embedded-cli requires EMBEDDED_CLI_IMPL in exactly one compilation unit.
// The line-reader build uses only its tokenizer; LTO drops the rest.
#define EMBEDDED_CLI_IMPL
#include <embedded_cli.h>

static const char CLI_PROMPT[] = "preOBD> ";

#if INCLUDE_CLI_EDITING
//=============================================================================
// CLI instance and configuration
//=============================================================================

// Configuration parameters sourced from per-board header (boards/<board>/board.h).
// embeddedCliRequiredSize() validates CLI_BUFFER_SIZE at runtime; increase in
// board.h if CLI init fails.
static CLI_UINT cli_buffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli* cli = nullptr;
#else
#include "line_reader.h"

static char lineBuf[CLI_CMD_BUFFER_SIZE];
static LineReader lineReader;
static bool promptShown = false;
#endif

#if SUPPORTS_JSON_IMPORT_STREAM
// Print a line, then the prompt again: embedded-cli's embeddedCliPrint().
// For output produced outside a command (the JSON import stream), after
// which nothing else would reprint the prompt.
static void cliPrintLine(const char* line) {
#if INCLUDE_CLI_EDITING
    if (cli != nullptr) embeddedCliPrint(cli, line);
#else
    msg.control.print(line);
    msg.control.print(F("\r\n"));
    msg.control.print(CLI_PROMPT);
#endif
}
#endif

#if SUPPORTS_JSON_IMPORT_STREAM
//=============================================================================
// JSON import streaming state
// Buffer is malloc'd on demand (serialConfig_beginJsonImport) and freed after
// dispatch, so 16 KB is only resident during an active import.
//
// Two completion modes share this one buffer: sentinel mode (interactive
// human at a terminal — can't know a paste's byte count in advance) scans
// each completed line for the literal text "END JSON"; counted mode
// (programmatic sender — already holds the full payload, so it knows the
// byte length for free) just counts up to jsonImportTarget. See
// dispatchJsonImport() for the shared completion tail.
//=============================================================================
static bool   jsonImportActive = false;
static bool   jsonImportCounted = false;    // true = counted mode, false = sentinel mode
static char*  jsonImportBuffer = nullptr;
static size_t jsonImportLen = 0;
static size_t jsonImportTarget = 0;         // counted mode: exact byte count expected
static size_t jsonImportLastLineStart = 0;  // sentinel mode only
// Sentinel mode only: set once the buffer overflows. Overflow used to call
// resetJsonImport() immediately, but any payload bytes already in flight
// (the rest of the oversized JSON, plus the sender's trailing "END JSON"
// line) would then fall through to embeddedCliReceiveChar() on the next
// call and get parsed as CLI command lines. Now we keep swallowing bytes
// until the idle timeout below cleans up, so nothing ever spills over.
static bool   jsonImportOverflowed = false;
// Updated on every byte received (both modes); serialConfig_checkJsonImportIdle()
// aborts an abandoned sentinel-mode import (human walked away, or a link
// dropped uncleanly mid-paste) so the malloc'd buffer isn't held forever.
static unsigned long jsonImportLastByteMs = 0;
static const unsigned long JSON_IMPORT_IDLE_TIMEOUT_MS = 45000;

static void resetJsonImport() {
    free(jsonImportBuffer);
    jsonImportBuffer = nullptr;
    jsonImportActive = false;
    jsonImportCounted = false;
    jsonImportTarget = 0;
    jsonImportLen = 0;
    jsonImportLastLineStart = 0;
    jsonImportOverflowed = false;
#if INCLUDE_CLI_EDITING
    if (cli != nullptr) embeddedCliProcess(cli);
#endif
}

// Shared completion tail for both modes: parse, report, reset. Reports via
// embeddedCliPrint (not msg.control) so the invitation gets reprinted
// afterward — streaming mode bypasses embeddedCliReceiveChar entirely, so
// the library's own post-command invitation reprint (onControlInput,
// embedded_cli.c) never runs for anything received since 'JSON IMPORT' was
// entered. Without this, no further prompt is EVER printed and every client
// waiting on one (this app, the reference webapp) hangs until it times out.
static void dispatchJsonImport() {
    bool ok = loadConfigFromJSON(jsonImportBuffer);
    if (ok) {
        msg.control.println(F("Applied"));
        printSaveReminder();
    } else {
        msg.control.println(F("ERROR: Failed to apply JSON configuration"));
    }
    cliPrintLine("");
    resetJsonImport();
}

void serialConfig_checkJsonImportIdle() {
    if (!jsonImportActive) return;
    // Signed subtraction: safe across a millis() wraparound (~49 days).
    if ((int32_t)(millis() - jsonImportLastByteMs) < (int32_t)JSON_IMPORT_IDLE_TIMEOUT_MS) return;
    cliPrintLine("ERROR: JSON import timed out (no data received)");
    resetJsonImport();
}
#endif

//=============================================================================
// Callbacks
//=============================================================================

#if INCLUDE_CLI_EDITING

// Write character callback - embedded-cli calls this to output characters
static void cli_write_char(EmbeddedCli* embeddedCli, char c) {
    (void)embeddedCli;
    msg.control.write(c);
}

// Command handler - called for bound commands
// This is the bridge between embedded-cli's binding system and our dispatch system
static void cli_command_handler(EmbeddedCli* embeddedCli, char* args, void* context) {
    (void)embeddedCli;

    // Get command name from context (we store it there during binding)
    const char* cmdName = (const char*)context;

    // Parse args into argc/argv format for dispatchCommand
    // embedded-cli can tokenize for us, but we need to build argv array
    const char* argv[CLI_MAX_ARGS];
    int argc = 1;

    // First arg is always the command name
    argv[0] = cmdName;

    // Tokenize remaining args if present
    if (args != nullptr && *args != '\0') {
        // Use embedded-cli's tokenization
        uint16_t tokenCount = embeddedCliGetTokenCount(args);
        for (uint16_t i = 0; i < tokenCount && argc < CLI_MAX_ARGS; i++) {
            const char* token = embeddedCliGetToken(args, i + 1);  // 1-indexed
            if (token != nullptr) {
                argv[argc++] = token;
            }
        }
    }

    // Dispatch to our command table
    dispatchCommand(argc, argv);
}

// Fallback for unrecognized commands (typically a lowercase command name that
// embedded-cli's case-sensitive binding match missed). We uppercase only the
// command NAME so dispatchCommand can match it, then tokenize the args exactly
// like the bound-command path (cli_command_handler): quote-aware and
// case-PRESERVING. Subcommand keywords still match case-insensitively via
// streq_P and app/sensor names via case-insensitive hashing, so free-text
// values (e.g. an Action's NAME, "Cooling Fan") must keep their original case
// and spaces — the old hand-rolled space-splitter that uppercased every token
// corrupted them (dropped words at spaces, forced ALL-CAPS).
static void cli_on_command(EmbeddedCli* embeddedCli, CliCommand* command) {
    (void)embeddedCli;

    // embedded-cli is case-sensitive on command names; users expect otherwise.
    char cmdUpper[32];
    strncpy(cmdUpper, command->name, sizeof(cmdUpper) - 1);
    cmdUpper[sizeof(cmdUpper) - 1] = '\0';
    for (char* p = cmdUpper; *p; p++) *p = toupper(*p);

    const char* argv[CLI_MAX_ARGS];
    int argc = 1;
    argv[0] = cmdUpper;

    // command->args points into embedded-cli's cmdBuffer, which reserves two
    // trailing NULs — the writable slack embeddedCliTokenizeArgs requires — so
    // we tokenize in place, identical to the bound-command path (quotes and
    // escapes honored). Unbound commands arrive here with args NOT yet tokenized.
    if (command->args != nullptr && *command->args != '\0') {
        char* args = (char*)command->args;
        embeddedCliTokenizeArgs(args);
        uint16_t tokenCount = embeddedCliGetTokenCount(args);
        for (uint16_t i = 0; i < tokenCount && argc < CLI_MAX_ARGS; i++) {
            const char* token = embeddedCliGetToken(args, i + 1);  // 1-indexed
            if (token != nullptr) argv[argc++] = token;
        }
    }

    // Try to dispatch - dispatchCommand will handle unknown commands
    dispatchCommand(argc, argv);
}
#else
static void lineWriteChar(char c) {
    msg.control.write(c);
}

// One completed line, parsed as embedded-cli's parseCommand() + our two
// callbacks did it: the name is everything up to the first space, the args
// start at the next non-space. The name is upper-cased (embedded-cli's
// bindings were the upper-case COMMANDS[] names, and cli_on_command folded
// any other spelling), `H` is HELP's alias binding, and `<NAME> -h` /
// `--help` on a bound name prints that command's one-line help.
static void dispatchLine(char* line) {
    char* name = line;
    while (*name == ' ') name++;
    if (*name == '\0') return;                      // blank line: prompt only

    char* args = name;
    while (*args != '\0' && *args != ' ') args++;
    bool exact = true;                              // name already upper-case?
    if (*args == ' ') {
        *args++ = '\0';
        while (*args == ' ') args++;
    }
    if (*args == '\0') args = nullptr;

    char cmdUpper[32];
    strncpy(cmdUpper, name, sizeof(cmdUpper) - 1);
    cmdUpper[sizeof(cmdUpper) - 1] = '\0';
    for (char* p = cmdUpper; *p; p++) {
        if (islower(*p)) exact = false;
        *p = toupper(*p);
    }
    const char* cmdName = (strcmp(cmdUpper, "H") == 0) ? "HELP" : cmdUpper;

    const char* argv[CLI_MAX_ARGS];
    int argc = 1;
    argv[0] = cmdName;
    if (args != nullptr) {
        // lineBuf keeps two bytes of slack past the line (line_reader.h)
        embeddedCliTokenizeArgs(args);
        uint16_t tokenCount = embeddedCliGetTokenCount(args);
        for (uint16_t i = 0; i < tokenCount && argc < CLI_MAX_ARGS; i++) {
            const char* token = embeddedCliGetToken(args, i + 1);  // 1-indexed
            if (token != nullptr) argv[argc++] = token;
        }
    }

    // embedded-cli compared its tokenized args, so only the first token
    // counts: `SET -h x` and `SET "-h"` print the help line too
    if (exact && argc > 1 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
            if (strcmp(COMMANDS[i].name, cmdUpper) == 0) {
                if (COMMANDS[i].help != nullptr) {
                    msg.control.write('\t');
                    msg.control.print(COMMANDS[i].help);
                    msg.control.print(F("\r\n"));
                }
                return;
            }
        }
    }
    dispatchCommand(argc, argv);
}
#endif

//=============================================================================
// Public API implementation
//=============================================================================

void initSerialConfig() {
#if !INCLUDE_CLI_EDITING
    lineReaderInit(lineReader, lineBuf, sizeof(lineBuf));
#else
    // Configure CLI
    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->rxBufferSize = CLI_RX_BUFFER_SIZE;
    config->cmdBufferSize = CLI_CMD_BUFFER_SIZE;
    config->historyBufferSize = CLI_HISTORY_BUFFER_SIZE;
    config->maxBindingCount = CLI_MAX_BINDINGS;
    config->invitation = CLI_PROMPT;
    config->enableAutoComplete = false;  // Disable live autocomplete (too noisy)

    // Check required size
    uint16_t requiredSize = embeddedCliRequiredSize(config);
    if (requiredSize > CLI_BUFFER_SIZE) {
        msg.control.print(F("ERROR: CLI buffer too small. Need "));
        msg.control.print(requiredSize);
        msg.control.print(F(" bytes, have "));
        msg.control.println(CLI_BUFFER_SIZE);
        return;
    }

    // Set buffer after size check
    config->cliBuffer = cli_buffer;
    config->cliBufferSize = CLI_BUFFER_SIZE;

    // Create CLI instance
    cli = embeddedCliNew(config);

    if (cli == nullptr) {
        msg.control.println(F("ERROR: Failed to initialize CLI (embeddedCliNew returned NULL)"));
        return;
    }

    // Set callbacks
    cli->writeChar = cli_write_char;
    cli->onCommand = cli_on_command;

    // Register all commands from our command table
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        CliCommandBinding binding = {
            .name = COMMANDS[i].name,
            .help = COMMANDS[i].help,
            .tokenizeArgs = true,
            .context = (void*)COMMANDS[i].name,  // Store name for handler to retrieve
            .binding = cli_command_handler
        };

        if (!embeddedCliAddBinding(cli, binding)) {
            msg.control.print(F("WARNING: Failed to bind command: "));
            msg.control.println(COMMANDS[i].name);
        }
    }

    // Add lowercase aliases to override embedded-cli's built-in help
    // and provide case-insensitive experience for common commands
    static const char* HELP_NAME = "HELP";  // Points to our HELP command
    CliCommandBinding helpBinding = {
        .name = "help",
        .help = nullptr,  // Hide from help listing
        .tokenizeArgs = true,
        .context = (void*)HELP_NAME,
        .binding = cli_command_handler
    };
    embeddedCliAddBinding(cli, helpBinding);

    // `H` shortcut — common CLI convention. Case-insensitive `cli_on_command`
    // fallback handles `h` lowercase, so a single uppercase binding covers both.
    CliCommandBinding hBinding = {
        .name = "H",
        .help = nullptr,
        .tokenizeArgs = true,
        .context = (void*)HELP_NAME,
        .binding = cli_command_handler
    };
    embeddedCliAddBinding(cli, hBinding);
#endif

    // Print startup banner
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  preOBD Serial Configuration"));
    msg.control.println(F("  Type 'HELP' for commands"));
    msg.control.println(F("========================================"));
    msg.control.println();

    // Print initial prompt
    processSerialCommands();
}

/**
 * Handle incoming character input (called by MessageRouter)
 * This function is called character-by-character from router.update()
 */
void handleCommandInput(char c) {
#if SUPPORTS_JSON_IMPORT_STREAM
    if (jsonImportActive) {
        jsonImportLastByteMs = millis();

        if (jsonImportOverflowed) {
            // Draining after a sentinel-mode overflow — swallow everything
            // until the idle timeout resets cleanly. See the field comment
            // on jsonImportOverflowed for why an immediate reset here would
            // let the rest of the oversized payload spill into the CLI.
            return;
        }

        // Echo char back for the interactive terminal path (\r before \n so
        // terminals return cursor to column 0). A counted-mode programmatic
        // sender discards this, so skip it there — otherwise a 16 KB payload
        // over transparent BLE would double its own airtime for nothing
        // (Opus review finding).
        if (!jsonImportCounted) {
            if (c == '\n') msg.control.print('\r');
            msg.control.print(c);
        }

        if (jsonImportCounted) {
            jsonImportBuffer[jsonImportLen++] = c;
            if (jsonImportLen >= jsonImportTarget) {
                // Capacity always has +1 room to spare: cmd_json only accepts
                // target <= JSON_IMPORT_MAX_BYTES - 1.
                jsonImportBuffer[jsonImportLen] = '\0';
                dispatchJsonImport();
            }
            return;
        }

        // Sentinel mode: check for buffer overflow before appending.
        if (jsonImportLen >= JSON_IMPORT_MAX_BYTES - 1) {
            msg.control.println(F("\r\nERROR: JSON exceeds buffer limit"));
            msg.control.print(F("  Max: "));
            msg.control.print(JSON_IMPORT_MAX_BYTES);
            msg.control.println(F(" bytes"));
            jsonImportOverflowed = true;
            return;
        }

        jsonImportBuffer[jsonImportLen++] = c;

        if (c == '\n') {
            // Check if the just-completed line equals "END JSON" (trim \r\n, case-insensitive)
            const char* line = jsonImportBuffer + jsonImportLastLineStart;
            size_t tlen = jsonImportLen - jsonImportLastLineStart;
            while (tlen > 0 && (line[tlen - 1] == '\r' || line[tlen - 1] == '\n')) tlen--;

            const size_t slen = 8;  // strlen("END JSON")
            if (tlen == slen && strncasecmp(line, "END JSON", slen) == 0) {
                // Null-terminate the buffer at the sentinel's start, then dispatch
                jsonImportBuffer[jsonImportLastLineStart] = '\0';
                dispatchJsonImport();
            } else {
                jsonImportLastLineStart = jsonImportLen;
            }
        }
        return;
    }
#endif
#if INCLUDE_CLI_EDITING
    if (cli != nullptr) {
        embeddedCliReceiveChar(cli, c);
    }
#else
    // Dispatched here, not deferred to processSerialCommands(): the router
    // feeds this transport's bytes with it already set as the reply target,
    // and there is no rx FIFO to hold the bytes that follow the Enter.
    if (lineReaderFeed(lineReader, c, lineWriteChar)) {
        dispatchLine(lineReader.buf);
        lineReaderClear(lineReader);
        msg.control.print(CLI_PROMPT);
    }
#endif
}

/**
 * Process CLI - should be called from main loop
 * This processes received characters and executes commands
 */
void processSerialCommands() {
#if INCLUDE_CLI_EDITING
    if (cli != nullptr) {
        embeddedCliProcess(cli);
    }
#else
    // The first call prints the prompt, as embeddedCliProcess() does
    if (!promptShown) {
        promptShown = true;
        msg.control.print(CLI_PROMPT);
    }
#endif
}

#if SUPPORTS_JSON_IMPORT_STREAM
/**
 * Begin JSON import streaming mode.
 * Returns false if already active; caller should surface an error.
 * target == 0: sentinel mode. target > 0: counted mode, exactly target bytes.
 */
bool serialConfig_beginJsonImport(size_t target) {
    if (jsonImportActive) return false;
    jsonImportBuffer = (char*)malloc(JSON_IMPORT_MAX_BYTES);
    if (jsonImportBuffer == nullptr) {
        msg.control.println(F("ERROR: Not enough heap for JSON import buffer"));
        return false;
    }
    jsonImportActive = true;
    jsonImportCounted = (target > 0);
    jsonImportTarget = target;
    jsonImportLen = 0;
    jsonImportLastLineStart = 0;
    jsonImportOverflowed = false;
    jsonImportLastByteMs = millis();
    if (jsonImportCounted) {
        msg.control.print(F("Ready for "));
        msg.control.print(target);
        msg.control.println(F(" bytes"));
    } else {
        msg.control.println(F("Paste JSON, then type 'END JSON' on its own line:"));
    }
    return true;
}

bool serialConfig_isJsonImportActive() {
    return jsonImportActive;
}
#endif

/**
 * Legacy function for backward compatibility
 * Commands are now dispatched through embedded-cli bindings
 */
void handleSerialCommand(char* cmd) {
    // This function is deprecated - commands are now handled by
    // embedded-cli bindings -> cli_command_handler -> dispatchCommand()
    // Kept for backward compatibility, but does nothing
    (void)cmd;
}
