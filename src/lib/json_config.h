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

// JSON features only available in EEPROM mode (not static config)
#ifndef USE_STATIC_CONFIG

#include <Arduino.h>
#include <ArduinoJson.h>

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

// SD card backup/restore (always available, independent of ENABLE_SD_LOGGING)
bool saveConfigToSD(const char* filename = nullptr);
bool loadConfigFromSD(const char* filename);

// URI-style file path dispatchers
bool saveConfigToFile(const char* destination, const char* filename);
bool loadConfigFromFile(const char* destination, const char* filename);

#endif // USE_STATIC_CONFIG
#endif // JSON_CONFIG_H
