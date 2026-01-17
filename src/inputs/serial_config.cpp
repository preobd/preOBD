/*
 * serial_config_microrl.cpp - microrl-based Serial Command Interface
 * Replaces manual command buffering with microrl library
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
#include "../lib/sensor_library.h"
#include "../lib/application_presets.h"
#include <string.h>

// microrl is a C library - need extern "C"
extern "C" {
    #include <microrl.h>
}

//=============================================================================
// microrl instance and callbacks
//=============================================================================

static microrl_t microrl_instance;

// Print callback - microrl calls this to output characters
static void microrl_print(const char* str) {
    msg.control.print(str);
}

// Execute callback - microrl calls this when user presses Enter
static int microrl_execute(int argc, const char* const* argv) {
    return dispatchCommand(argc, argv);
}

#ifdef _USE_COMPLETE
// Static array for completion results (must persist after function returns)
// Max 64 matches (commands, subcommands, or sensors/applications) + NULL terminator
#define MAX_COMPLETION_MATCHES 64
static char* completion_matches[MAX_COMPLETION_MATCHES + 1];

// Helper function: case-insensitive prefix match
static bool starts_with(const char* str, const char* prefix, size_t prefix_len) {
    for (size_t i = 0; i < prefix_len; i++) {
        if (toupper(prefix[i]) != toupper(str[i])) {
            return false;
        }
    }
    return true;
}

// Helper function: add match if not already added (prevent duplicates)
static void add_match(const char* match, int* match_count) {
    if (*match_count >= MAX_COMPLETION_MATCHES) return;

    // Check for duplicates
    for (int i = 0; i < *match_count; i++) {
        if (streq(completion_matches[i], match)) return;
    }

    completion_matches[(*match_count)++] = (char*)match;
}

// Completion callback - microrl calls this when user presses TAB
// Returns NULL-terminated array of matching command/subcommand names
// argc: number of tokens, argv: array of tokens (argv[argc-1] is the partial token)
static char** microrl_complete(int argc, const char* const* argv) {
    int match_count = 0;
    completion_matches[0] = NULL;

    const char* partial = (argc > 0) ? argv[argc - 1] : "";
    size_t partial_len = strlen(partial);

    // ===== Complete first token (top-level commands) =====
    if (argc <= 1) {
        for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
            const char* cmd_name = COMMANDS[i].name;
            if (starts_with(cmd_name, partial, partial_len)) {
                add_match(cmd_name, &match_count);
            }
        }
    }

    // ===== Complete subcommands based on first token =====
    else if (argc >= 2) {
        const char* cmd = argv[0];

        // LIST subcommands
        if (streq(cmd, "LIST")) {
            static const char* list_subcommands[] = {
                "INPUTS", "APPLICATIONS", "SENSORS", "OUTPUTS", "TRANSPORTS"
            };
            for (uint8_t i = 0; i < 5; i++) {
                if (starts_with(list_subcommands[i], partial, partial_len)) {
                    add_match(list_subcommands[i], &match_count);
                }
            }
        }

        // HELP subcommands
        else if (streq(cmd, "HELP")) {
            static const char* help_subcommands[] = {
                "LIST", "SET", "CALIBRATION", "CONTROL", "OUTPUT",
                "RELAY", "TEST", "DISPLAY", "TRANSPORT", "SYSTEM",
                "CONFIG", "EXAMPLES", "QUICK"
            };
            for (uint8_t i = 0; i < 13; i++) {
                if (starts_with(help_subcommands[i], partial, partial_len)) {
                    add_match(help_subcommands[i], &match_count);
                }
            }
        }

        // OUTPUT subcommands
        else if (streq(cmd, "OUTPUT")) {
            static const char* output_subcommands[] = {
                "STATUS", "CAN", "REALDASH", "SERIAL", "SD"
            };
            for (uint8_t i = 0; i < 5; i++) {
                if (starts_with(output_subcommands[i], partial, partial_len)) {
                    add_match(output_subcommands[i], &match_count);
                }
            }
        }

        // DISPLAY subcommands
        else if (streq(cmd, "DISPLAY")) {
            static const char* display_subcommands[] = {
                "STATUS", "ENABLE", "DISABLE", "TYPE", "ADDRESS", "INTERVAL"
            };
            for (uint8_t i = 0; i < 6; i++) {
                if (starts_with(display_subcommands[i], partial, partial_len)) {
                    add_match(display_subcommands[i], &match_count);
                }
            }
        }

        // TRANSPORT subcommands
        else if (streq(cmd, "TRANSPORT")) {
            static const char* transport_subcommands[] = {
                "STATUS", "LIST", "CONTROL", "DATA", "DEBUG"
            };
            for (uint8_t i = 0; i < 5; i++) {
                if (starts_with(transport_subcommands[i], partial, partial_len)) {
                    add_match(transport_subcommands[i], &match_count);
                }
            }
        }

        // SYSTEM subcommands
        else if (streq(cmd, "SYSTEM")) {
            static const char* system_subcommands[] = {
                "STATUS", "DUMP", "UNITS", "SEA_LEVEL", "INTERVAL",
                "REBOOT", "RESET"
            };
            for (uint8_t i = 0; i < 7; i++) {
                if (starts_with(system_subcommands[i], partial, partial_len)) {
                    add_match(system_subcommands[i], &match_count);
                }
            }
        }

        // RELAY subcommands (if enabled)
        #ifdef ENABLE_RELAY_OUTPUT
        else if (streq(cmd, "RELAY")) {
            if (argc == 2) {
                static const char* relay_subcommands[] = {"LIST", "0", "1"};
                for (uint8_t i = 0; i < 3; i++) {
                    if (starts_with(relay_subcommands[i], partial, partial_len)) {
                        add_match(relay_subcommands[i], &match_count);
                    }
                }
            } else if (argc == 3 && (streq(argv[1], "0") || streq(argv[1], "1"))) {
                static const char* relay_opts[] = {
                    "STATUS", "PIN", "INPUT", "THRESHOLD", "MODE"
                };
                for (uint8_t i = 0; i < 5; i++) {
                    if (starts_with(relay_opts[i], partial, partial_len)) {
                        add_match(relay_opts[i], &match_count);
                    }
                }
            }
        }
        #endif

        // TEST subcommands (if enabled)
        #ifdef ENABLE_TEST_MODE
        else if (streq(cmd, "TEST")) {
            static const char* test_subcommands[] = {
                "LIST", "STATUS", "STOP", "0", "1", "2", "3", "4"
            };
            for (uint8_t i = 0; i < 8; i++) {
                if (starts_with(test_subcommands[i], partial, partial_len)) {
                    add_match(test_subcommands[i], &match_count);
                }
            }
        }
        #endif

        // SET subcommands - complex hierarchical completion
        else if (streq(cmd, "SET")) {
            if (argc == 2) {
                // Complete pin names: A0-A15, I2C, I2C:0-9
                static const char* pin_prefixes[] = {
                    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
                    "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15",
                    "I2C"
                };
                for (uint8_t i = 0; i < 17; i++) {
                    if (starts_with(pin_prefixes[i], partial, partial_len)) {
                        add_match(pin_prefixes[i], &match_count);
                    }
                }
            } else if (argc == 3) {
                // SET <pin> <field> - complete field names only (not applications for cleaner UX)
                static const char* set_fields[] = {
                    "APPLICATION", "SENSOR", "NAME", "DISPLAY_NAME", "UNITS",
                    "ALARM", "RPM", "SPEED", "PRESSURE_LINEAR", "PRESSURE_POLY",
                    "STEINHART", "BETA", "BIAS", "CALIBRATION"
                };
                for (uint8_t i = 0; i < 14; i++) {
                    if (starts_with(set_fields[i], partial, partial_len)) {
                        add_match(set_fields[i], &match_count);
                    }
                }
            } else if (argc == 4) {
                // SET <pin> APPLICATION <app> - complete application names
                if (streq(argv[2], "APPLICATION")) {
                    for (uint8_t i = 0; i < NUM_APPLICATION_PRESETS; i++) {
                        const char* app_name = getApplicationNameByIndex(i);
                        if (app_name && starts_with(app_name, partial, partial_len)) {
                            add_match(app_name, &match_count);
                        }
                    }
                }
                // SET <pin> SENSOR <sensor> - complete sensor names
                else if (streq(argv[2], "SENSOR")) {
                    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
                        const char* sensor_name = getSensorNameByIndex(i);
                        if (sensor_name && starts_with(sensor_name, partial, partial_len)) {
                            add_match(sensor_name, &match_count);
                        }
                    }
                }
                // SET <pin> ALARM <subcommand> - complete alarm subcommands
                else if (streq(argv[2], "ALARM")) {
                    static const char* alarm_opts[] = {
                        "ENABLE", "DISABLE", "WARMUP", "PERSIST"
                    };
                    for (uint8_t i = 0; i < 4; i++) {
                        if (starts_with(alarm_opts[i], partial, partial_len)) {
                            add_match(alarm_opts[i], &match_count);
                        }
                    }
                }
                // SET <pin> CALIBRATION <subcommand>
                else if (streq(argv[2], "CALIBRATION")) {
                    static const char* cal_opts[] = {"PRESET", "CLEAR"};
                    for (uint8_t i = 0; i < 2; i++) {
                        if (starts_with(cal_opts[i], partial, partial_len)) {
                            add_match(cal_opts[i], &match_count);
                        }
                    }
                }
                // SET <pin> <app> <sensor> - complete sensor names for shorthand
                else {
                    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
                        const char* sensor_name = getSensorNameByIndex(i);
                        if (sensor_name && starts_with(sensor_name, partial, partial_len)) {
                            add_match(sensor_name, &match_count);
                        }
                    }
                }
            }
        }

        // INFO subcommands
        else if (streq(cmd, "INFO")) {
            if (argc == 2) {
                // Complete pin names
                static const char* pin_prefixes[] = {
                    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
                    "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15",
                    "I2C"
                };
                for (uint8_t i = 0; i < 17; i++) {
                    if (starts_with(pin_prefixes[i], partial, partial_len)) {
                        add_match(pin_prefixes[i], &match_count);
                    }
                }
            } else if (argc == 3) {
                // INFO <pin> <subcommand>
                static const char* info_opts[] = {"ALARM", "CALIBRATION"};
                for (uint8_t i = 0; i < 2; i++) {
                    if (starts_with(info_opts[i], partial, partial_len)) {
                        add_match(info_opts[i], &match_count);
                    }
                }
            }
        }
    }

    // NULL-terminate the array (required by microrl)
    completion_matches[match_count] = NULL;

    return completion_matches;
}
#endif

#ifdef _USE_CTLR_C
// Ctrl+C callback - microrl calls this when user presses Ctrl+C
static void microrl_sigint(void) {
    msg.control.println();
    msg.control.println(F("^C"));
}
#endif

//=============================================================================
// Public API implementation
//=============================================================================

void initSerialConfig() {
    // Initialize microrl
    microrl_init(&microrl_instance, microrl_print);

    // Set execute callback (required)
    microrl_set_execute_callback(&microrl_instance, microrl_execute);

#ifdef _USE_COMPLETE
    // Set completion callback (optional, Teensy/ESP32 only)
    microrl_set_complete_callback(&microrl_instance, microrl_complete);
#endif

#ifdef _USE_CTLR_C
    // Set Ctrl+C callback (optional)
    microrl_set_sigint_callback(&microrl_instance, microrl_sigint);
#endif

    // Print startup banner
    msg.control.println();
    msg.control.println(F("========================================"));
    msg.control.println(F("  openEMS Serial Configuration"));
    msg.control.println(F("  Type 'HELP' for commands"));
    msg.control.println(F("========================================"));
    msg.control.println();

    // microrl will print initial prompt automatically (via _ENABLE_INIT_PROMPT)
}

/**
 * Handle incoming character input (called by MessageRouter)
 * This function is called character-by-character from router.update()
 */
void handleCommandInput(char c) {
    // Feed character to microrl
    microrl_insert_char(&microrl_instance, c);
}

/**
 * Legacy function for backward compatibility
 * Now just a no-op since router.update() handles command polling
 */
void processSerialCommands() {
    // This function is deprecated - command input is now handled by
    // router.update() calling handleCommandInput()
    // Kept for backward compatibility, but does nothing
}

/**
 * Legacy function for backward compatibility
 * Commands are now dispatched through command_table.cpp
 */
void handleSerialCommand(char* cmd) {
    // This function is deprecated - commands are now handled by
    // microrl execute callback -> dispatchCommand()
    // Kept for backward compatibility, but does nothing
}

#endif // USE_STATIC_CONFIG
