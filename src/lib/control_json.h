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
 *   action = { id, type, target, rule?, rules?, invert, failsafe }
 *            // rule    = FIRST member Rule id; omitted = manual-only
 *            // rules   = the full member list, emitted only when there is more
 *            //           than one (§11.6 / #343). `rule` stays present and is
 *            //           rules[0], so a pre-#343 reader still sees a valid link.
 */

#ifndef CONTROL_JSON_H
#define CONTROL_JSON_H

#include "../control/control_types.h"  // Rule/Action + ENABLE_CONTROL_ENGINE

#if SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE

#include <ArduinoJson.h>

// Serialize one record into a (caller-created) JSON object.
void controlExportRule(JsonObject obj, const Rule& r);

// `memberRules`/`memberCount` are the action's FULL Rule membership (member 0
// must equal a.ruleRef). Pass nullptr/0 to fall back to a.ruleRef alone.
void controlExportAction(JsonObject obj, const Action& a,
                         const uint8_t* memberRules = nullptr, uint8_t memberCount = 0);

// Deserialize one record. Returns false (leaving `out` untouched) when a
// required field is missing or an enum string doesn't map — the caller skips
// that record. Optional fields fall back to sane defaults.
bool controlImportRule(JsonObjectConst obj, Rule& out);

// `out.ruleRef` receives the first member. Any ADDITIONAL members (from a
// "rules" array) are written to extraRules[0..*nExtra), for the caller to feed
// to controlActionAddRule() once the action is in a table — this converter is
// deliberately table-free so it still links on the host (see test/test_control_json).
// Pass extraRules = nullptr to ignore them.
bool controlImportAction(JsonObjectConst obj, Action& out,
                         uint8_t* extraRules = nullptr, uint8_t maxExtra = 0,
                         uint8_t* nExtra = nullptr);

#endif // SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE
#endif // CONTROL_JSON_H
