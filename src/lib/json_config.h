/*
 * json_config.h - JSON Configuration Export/Import
 *
 * Provides JSON serialization and deserialization for system configuration.
 * Used for:
 * - SYSTEM DUMP JSON command (export to serial)
 * - CONFIG SAVE <filename> (export to SD card)
 * - CONFIG LOAD <filename> (import from SD card)
 *
 * SCHEMA VERSIONING:
 * The JSON schema is versioned independently of firmware version.
 * Schema version is included in exported JSON: {"schemaVersion": 1, ...}
 * See docs/JSON_MIGRATION_GUIDE.md for details on maintaining compatibility.
 *
 * Current Schema Version: 1
 *   - Initial release (v0.4.1-alpha)
 *
 * NOTE: JSON features are only available in EEPROM mode (runtime config).
 *       Static builds do not include JSON to save memory.
 */

#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H

#include "platform.h"

// JSON features only available on platforms with enough RAM (not Mega/Uno) and in runtime config mode
#if SUPPORTS_JSON_CONFIG

#include <Arduino.h>
#include <ArduinoJson.h>
#include "sd_manager.h"

// Forward declarations
struct Input;
struct SystemConfig;

// JSON export functions
void dumpConfigToJSON(Print& output);
void exportSystemConfigToJSON(JsonObject& systemObj);
void exportInputsToJSON(JsonArray& inputsArray);
void exportInputToJSON(JsonObject& inputObj, const Input* input);

// JSON import functions
bool loadConfigFromJSON(const char* jsonString);
bool importSystemConfigFromJSON(JsonObject& systemObj);
bool importInputsFromJSON(JsonArray& inputsArray);
bool importInputFromJSON(JsonObject& inputObj, uint8_t index);

#if SUPPORTS_SD
// SD card backup/restore (requires SUPPORTS_SD in board profile)
bool saveConfigToSD(const char* filename = nullptr);
bool loadConfigFromSD(const char* filename);

// URI-style file path dispatchers
bool saveConfigToFile(const char* destination, const char* filename);
bool loadConfigFromFile(const char* destination, const char* filename);
#endif // SUPPORTS_SD

#endif // SUPPORTS_JSON_CONFIG
#endif // JSON_CONFIG_H
