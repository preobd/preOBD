/*
 * command_helpers.h - Helper functions and help system for commands
 */

#ifndef _COMMAND_HELPERS_H_
#define _COMMAND_HELPERS_H_

#include <stdint.h>
#include <Arduino.h>  // for __FlashStringHelper
#include "../lib/message_router.h"
#include "../lib/sensor_types.h"  // for MeasurementType

// Forward declarations for enums
enum MessagePlane;
enum TransportID;

// Forward declaration to avoid pulling input.h into every command_helpers consumer.
struct Input;

// Look up an Input by pin; if absent, print the standard
// "ERROR: Input not configured" message on the control plane and return nullptr.
// Replaces the recurring getInputByPin + null-check + error-print boilerplate
// at every pin-scoped command leaf.
Input* requireInput(uint8_t pin);

// Standard success-message printers for pin-scoped SET / BUS leaves. Each
// emits "Input <pinArg> <field> set to <value>" (or the enable/disable shape
// for printInputFlag) followed by the canonical "(use SAVE to persist)"
// reminder on the control plane. Use these instead of hand-built print
// sequences so success output and the SAVE reminder stay consistent.
void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        const char* value);
void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        const __FlashStringHelper* value);
void printInputFieldSet(const char* pinArg, const __FlashStringHelper* field,
                        float value, uint8_t decimals = 3);
void printInputFlag(const char* pinArg, const __FlashStringHelper* facet,
                    bool enabled);

// Standard "(use SAVE to persist)" reminder. Call this at the end of any
// command leaf that mutates persisted configuration but cannot use the
// printInputFieldSet/printInputFlag helpers (e.g. multi-line success output).
void printSaveReminder();

// Walk a PROGMEM-resident Subcommand table and print each entry's help block
// verbatim. Each help string is expected to be a self-contained usage block
// (the parent prefix is baked in, sub-forms span multiple lines). Skips
// entries whose help field is null. Used by HELP BUS / HELP SYSTEM printers
// so they stay in sync with the dispatch tables automatically.
struct Subcommand;
void printSubcommandTable(const Subcommand* table, uint8_t n);

// MeasurementType -> printable PROGMEM name (e.g. "TEMPERATURE", "PRESSURE").
// Returns nullptr for unknown values; callers should null-check or print a
// fallback when displaying user-facing errors.
const __FlashStringHelper* measurementTypeName(MeasurementType type);

// Validate one dispatch-table entry. Caller has already read `token_P` and
// `handler` via pgm_read_ptr (the same path the production dispatcher uses)
// — so a regression that breaks PROGMEM access surfaces here as bad-char
// failures. Returns 1 if invalid (and prints diagnostic), 0 if OK.
//
// `token_P` may be either RAM-resident (Command::name) or PROGMEM-resident
// (Subcommand::token, SetField::token); pass `tokenInProgmem` accordingly.
int validateDispatchEntry(const __FlashStringHelper* group, uint8_t index,
                          const char* token_P, const void* handler,
                          bool tokenInProgmem);

// Helper functions (from old serial_config.cpp)
void trim(char* str);
void toUpper(char* str);
bool streq(const char* a, const char* b);

// Case-insensitive compare: RAM string `a` vs PROGMEM string `b_P`.
// Used for matching user input against PROGMEM-resident keyword tables on AVR.
bool streq_P(const char* a, const char* b_P);

// Pin parsing
uint8_t parsePin(const char* pinStr, bool* isValid);
void resetVirtualPinCounters();  // Reset CAN/I2C virtual pin allocation counters

// Transport parsing (returns valid enum + sets isValid flag)
MessagePlane parsePlane(const char* str, bool* isValid);
TransportID parseTransport(const char* str, bool* isValid);

// File path parsing for SAVE/LOAD commands
struct FilePathComponents {
    char destination[8];    // "SD", "USB", "EEPROM", etc.
    char filename[32];      // Filename portion
    bool isValid;           // Parse success flag
};

// Parse destination:filename format
// Examples: "SD:config.json" → {dest:"SD", file:"config.json"}
//           "config.json"    → {dest:"SD", file:"config.json"} (default)
//           "USB:backup.json" → {dest:"USB", file:"backup.json"}
FilePathComponents parseFilePath(const char* pathStr);

// System status printers
void printDisplayConfig();

// Help system
void printHelpOverview();
void printHelpQuick();
void printHelpCategory(const char* category);

// Individual help printer functions
void printHelpList();
void printHelpSet();
void printHelpControl();
void printHelpOutput();
void printHelpBus();
void printHelpDisplay();
void printHelpTransport();
void printHelpSystem();
void printHelpConfig();
void printHelpExamples();

#if ENABLE_RELAY_OUTPUT
void printHelpRelay();
#endif

#if ENABLE_TEST_MODE
void printHelpTest();
#endif

#endif // _COMMAND_HELPERS_H_
