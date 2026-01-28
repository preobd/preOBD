/*
 * serial_config.cpp - embedded-cli based Serial Command Interface
 * Uses embedded-cli library for command line interface with history and autocompletion
 *
 * NOTE: Only compiled in EEPROM/runtime configuration mode (not in static mode)
 */

#include "../config.h"

#ifndef USE_STATIC_CONFIG

#include "serial_config.h"
#include "command_table.h"
#include "command_helpers.h"
#include "../lib/message_router.h"
#include "../lib/message_api.h"
#include <string.h>
#include <ctype.h>

// embedded-cli requires EMBEDDED_CLI_IMPL in exactly one compilation unit
#define EMBEDDED_CLI_IMPL
#include <embedded_cli.h>

//=============================================================================
// CLI instance and configuration
//=============================================================================

// Configuration parameters
#define CLI_RX_BUFFER_SIZE 128
#define CLI_CMD_BUFFER_SIZE 128
#define CLI_HISTORY_BUFFER_SIZE 256
#define CLI_MAX_BINDINGS 32  // Enough for all commands + some headroom

// Static buffer for CLI (avoids dynamic allocation)
// Size calculated to fit the configuration above
#define CLI_BUFFER_SIZE 4096
static CLI_UINT cli_buffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli* cli = nullptr;

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
    const int MAX_ARGS = 16;
    const char* argv[MAX_ARGS];
    int argc = 1;

    // First arg is always the command name
    argv[0] = cmdName;

    // Tokenize remaining args if present
    if (args != nullptr && *args != '\0') {
        // Use embedded-cli's tokenization
        uint16_t tokenCount = embeddedCliGetTokenCount(args);
        for (uint16_t i = 0; i < tokenCount && argc < MAX_ARGS; i++) {
            const char* token = embeddedCliGetToken(args, i + 1);  // 1-indexed
            if (token != nullptr) {
                argv[argc++] = token;
            }
        }
    }

    // Dispatch to our command table
    dispatchCommand(argc, argv);
}

// Fallback for unrecognized commands - try case-insensitive match
static void cli_on_command(EmbeddedCli* embeddedCli, CliCommand* command) {
    (void)embeddedCli;

    // embedded-cli is case-sensitive, but users expect case-insensitive commands
    // Convert command name to uppercase
    char cmdUpper[32];
    strncpy(cmdUpper, command->name, sizeof(cmdUpper) - 1);
    cmdUpper[sizeof(cmdUpper) - 1] = '\0';
    for (char* p = cmdUpper; *p; p++) *p = toupper(*p);

    // Build argc/argv for dispatch
    const int MAX_ARGS = 16;
    const char* argv[MAX_ARGS];
    static char argBuffers[MAX_ARGS - 1][32];  // Static buffers for uppercase args
    int argc = 1;
    argv[0] = cmdUpper;

    // Tokenize args if present and convert to uppercase
    if (command->args != nullptr && *command->args != '\0') {
        // Manually tokenize the args string (space-delimited)
        char* argsCopy = (char*)command->args;

        // Parse space-separated tokens
        int bufIdx = 0;
        for (char* p = argsCopy; *p && argc < MAX_ARGS && bufIdx < (MAX_ARGS - 1); ) {
            // Skip leading spaces
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0') break;

            // Find end of token
            char* tokenStart = p;
            while (*p && *p != ' ' && *p != '\t') p++;

            // Copy token to buffer and uppercase
            size_t tokenLen = p - tokenStart;
            if (tokenLen >= sizeof(argBuffers[bufIdx])) {
                tokenLen = sizeof(argBuffers[bufIdx]) - 1;
            }
            strncpy(argBuffers[bufIdx], tokenStart, tokenLen);
            argBuffers[bufIdx][tokenLen] = '\0';
            for (char* q = argBuffers[bufIdx]; *q; q++) *q = toupper(*q);

            argv[argc++] = argBuffers[bufIdx];
            bufIdx++;
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
    config->invitation = "openEMS> ";
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

    // Print startup banner
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  openEMS Serial Configuration"));
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

#endif // USE_STATIC_CONFIG
