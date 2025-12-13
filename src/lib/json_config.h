/*
 * json_config.h - JSON Configuration Export/Import
 *
 * Provides JSON serialization and deserialization for system configuration.
 * Used for:
 * - DUMP JSON command (export to serial)
 * - CONFIG SAVE <filename> (export to SD card)
 * - CONFIG LOAD <filename> (import from SD card)
 */

#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H

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

// SD card backup/restore
#ifdef ENABLE_SD_LOGGING
bool saveConfigToSD(const char* filename = nullptr);
bool loadConfigFromSD(const char* filename);
#endif

#endif // JSON_CONFIG_H
