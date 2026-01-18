/*
 * command_helpers.h - Helper functions and help system for commands
 */

#ifndef _COMMAND_HELPERS_H_
#define _COMMAND_HELPERS_H_

#include <stdint.h>
#include "../lib/message_router.h"

// Forward declarations for enums
enum MessagePlane;
enum TransportID;

// Helper functions (from old serial_config.cpp)
void trim(char* str);
void toUpper(char* str);
bool streq(const char* a, const char* b);

// Pin parsing
uint8_t parsePin(const char* pinStr, bool* isValid);

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
void printHelpCalibration();
void printHelpControl();
void printHelpOutput();
void printHelpBus();
void printHelpDisplay();
void printHelpTransport();
void printHelpSystem();
void printHelpConfig();
void printHelpExamples();

#ifdef ENABLE_RELAY_OUTPUT
void printHelpRelay();
#endif

#ifdef ENABLE_TEST_MODE
void printHelpTest();
#endif

#endif // _COMMAND_HELPERS_H_
