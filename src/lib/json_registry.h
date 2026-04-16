/*
 * json_registry.h - JSON export of firmware static catalogs
 *
 * Exports compile-time registries (applications, sensors, units, categories,
 * outputs, standard PIDs, enum types, firmware info) as JSON, for use by
 * external tools such as a web-based configuration UI.
 *
 * This is distinct from json_config.h, which exports the user's active
 * configuration (configured inputs, bus settings). This exports the catalog —
 * the compile-time options the user can choose from.
 *
 * Only available on platforms with SUPPORTS_JSON_EXPORT (Teensy 3.x/4.x, ESP32).
 */

#ifndef JSON_REGISTRY_H
#define JSON_REGISTRY_H

#include <Arduino.h>
#include "platform.h"

#if SUPPORTS_JSON_EXPORT && !defined(USE_STATIC_CONFIG)

// ===== GRANULAR EXPORTS =====
// Each function writes a bare JSON array (or object for firmware) to out.

void writeApplicationsJson(Print& out);
void writeSensorsJson(Print& out, const char* filter = nullptr);  // filter: category name or nullptr
void writeUnitsJson(Print& out);
void writeCategoriesJson(Print& out);
void writeOutputsJson(Print& out);
void writePidsJson(Print& out);
void writeMeasurementTypesJson(Print& out);
void writeCalibrationTypesJson(Print& out);

// ===== BUNDLED EXPORT =====
// Writes one top-level JSON object containing all catalogs plus firmware info.
void dumpRegistryToJson(Print& out);

#endif // SUPPORTS_JSON_EXPORT && !USE_STATIC_CONFIG
#endif // JSON_REGISTRY_H
