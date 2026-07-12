/*
 * serial_config.cpp - embedded-cli based Serial Command Interface
 * Uses embedded-cli library for command line interface with history and autocompletion
 */

#include "../config.h"

#include "serial_config.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include "../lib/json_config.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if SUPPORTS_JSON_IMPORT_STREAM
#include <strings.h>  // strncasecmp (POSIX); not available on AVR toolchain
#endif

// embedded-cli requires EMBEDDED_CLI_IMPL in exactly one compilation unit
#define EMBEDDED_CLI_IMPL
#include <embedded_cli.h>

//=============================================================================
// CLI instance and configuration
//=============================================================================

// Configuration parameters sourced from per-board header (boards/<board>/board.h).
// embeddedCliRequiredSize() validates CLI_BUFFER_SIZE at runtime; increase in
// board.h if CLI init fails.
static CLI_UINT cli_buffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli* cli = nullptr;

#if SUPPORTS_JSON_IMPORT_STREAM
//=============================================================================
// JSON import streaming state
// Buffer is malloc'd on demand (serialConfig_beginJsonImport) and freed after
// dispatch, so 16 KB is only resident during an active import.
//=============================================================================
static bool   jsonImportActive = false;
static char*  jsonImportBuffer = nullptr;
static size_t jsonImportLen = 0;
static size_t jsonImportLastLineStart = 0;

static void resetJsonImport() {
    free(jsonImportBuffer);
    jsonImportBuffer = nullptr;
    jsonImportActive = false;
    jsonImportLen = 0;
    jsonImportLastLineStart = 0;
    if (cli != nullptr) embeddedCliProcess(cli);
}
#endif

//=============================================================================
// Callbacks
//=============================================================================

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

//=============================================================================
// Public API implementation
//=============================================================================

void initSerialConfig() {
    // Configure CLI
    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->rxBufferSize = CLI_RX_BUFFER_SIZE;
    config->cmdBufferSize = CLI_CMD_BUFFER_SIZE;
    config->historyBufferSize = CLI_HISTORY_BUFFER_SIZE;
    config->maxBindingCount = CLI_MAX_BINDINGS;
    config->invitation = "preOBD> ";
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

    // Print startup banner
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  preOBD Serial Configuration"));
    msg.control.println(F("  Type 'HELP' for commands"));
    msg.control.println(F("========================================"));
    msg.control.println();

    // Print initial prompt
    embeddedCliProcess(cli);
}

/**
 * Handle incoming character input (called by MessageRouter)
 * This function is called character-by-character from router.update()
 */
void handleCommandInput(char c) {
#if SUPPORTS_JSON_IMPORT_STREAM
    if (jsonImportActive) {
        // Echo char back; emit \r before \n so terminals return cursor to column 0
        if (c == '\n') msg.control.print('\r');
        msg.control.print(c);

        // Check for buffer overflow before appending
        if (jsonImportLen >= JSON_IMPORT_MAX_BYTES - 1) {
            msg.control.println(F("\r\nERROR: JSON exceeds buffer limit"));
            msg.control.print(F("  Max: "));
            msg.control.print(JSON_IMPORT_MAX_BYTES);
            msg.control.println(F(" bytes"));
            resetJsonImport();
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

                bool ok = loadConfigFromJSON(jsonImportBuffer);
                if (ok) {
                    msg.control.println(F("Applied"));
                    printSaveReminder();
                } else {
                    msg.control.println(F("ERROR: Failed to apply JSON configuration"));
                }

                resetJsonImport();
            } else {
                jsonImportLastLineStart = jsonImportLen;
            }
        }
        return;
    }
#endif
    if (cli != nullptr) {
        embeddedCliReceiveChar(cli, c);
    }
}

/**
 * Process CLI - should be called from main loop
 * This processes received characters and executes commands
 */
void processSerialCommands() {
    if (cli != nullptr) {
        embeddedCliProcess(cli);
    }
}

#if SUPPORTS_JSON_IMPORT_STREAM
/**
 * Begin JSON import streaming mode.
 * Returns false if already active; caller should surface an error.
 */
bool serialConfig_beginJsonImport() {
    if (jsonImportActive) return false;
    jsonImportBuffer = (char*)malloc(JSON_IMPORT_MAX_BYTES);
    if (jsonImportBuffer == nullptr) {
        msg.control.println(F("ERROR: Not enough heap for JSON import buffer"));
        return false;
    }
    jsonImportActive = true;
    jsonImportLen = 0;
    jsonImportLastLineStart = 0;
    msg.control.println(F("Paste JSON, then type 'END JSON' on its own line:"));
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
