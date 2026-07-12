/*
 * control_json.h - Rule/Action <-> JSON leaf converters (control engine).
 *
 * The array-level iteration over rules[]/actions[] plus the
 * re-arming on import live in json_config.cpp. These per-record converters are
 * isolated here, depending only on ArduinoJson + control_types.h, so they link
 * (and round-trip-test) on the native host without dragging json_config.cpp's
 * heavy dependency set (message_api / registries / EEPROM / SD). See
 * test/test_control_json.
 *
 * Schema (additive — no JSON_SCHEMA_VERSION bump; absent arrays are ignored on
 * import, so pre-control-engine configs still load):
 *   rule   = { id, input, condition, severity, limitLo, limitHi,
 *              hysteresis, warmupMs, debounceMs, enabled }
 *   action = { id, type, target, rule?, invert, failsafe }   // rule omitted = manual
 */

#ifndef CONTROL_JSON_H
#define CONTROL_JSON_H

#include "../control/control_types.h"  // Rule/Action + ENABLE_CONTROL_ENGINE

#if SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE

#include <ArduinoJson.h>

// Serialize one record into a (caller-created) JSON object.
void controlExportRule(JsonObject obj, const Rule& r);
void controlExportAction(JsonObject obj, const Action& a);

// Deserialize one record. Returns false (leaving `out` untouched) when a
// required field is missing or an enum string doesn't map — the caller skips
// that record. Optional fields fall back to sane defaults.
bool controlImportRule(JsonObjectConst obj, Rule& out);
bool controlImportAction(JsonObjectConst obj, Action& out);

#endif // SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE
#endif // CONTROL_JSON_H
