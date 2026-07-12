/*
 * control_json.cpp - Rule/Action <-> JSON leaf converters. See control_json.h.
 */

#include "control_json.h"

#if SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE

#include <string.h>

namespace {

struct NameMap { const char* name; uint8_t val; };

// JSON enum tokens. Kept clean ("NONE", "OUTSIDE") rather than the CLI's
// display variants ("--", "OUTSIDE_BAND" isn't used either) so the dump is
// self-describing. Append-only — values are the stable persisted ints.
const NameMap CONDITIONS[] = {
    {"ABOVE",   RULE_ABOVE},
    {"BELOW",   RULE_BELOW},
    {"OUTSIDE", RULE_OUTSIDE_BAND},
    {"INSIDE",  RULE_INSIDE_BAND},
};
const NameMap SEVERITIES[] = {
    {"NONE",    RULE_SEV_NONE},
    {"WARNING", RULE_SEV_WARNING},
    {"ALARM",   RULE_SEV_ALARM},
};
const NameMap ACTION_TYPES[] = {
    {"RELAY",   ACT_RELAY},
    {"BEEPER",  ACT_BEEPER},
    {"LED",     ACT_LED},
    {"DISPLAY", ACT_DISPLAY},
};

template <size_t N>
const char* nameOf(const NameMap (&m)[N], uint8_t v) {
    for (size_t i = 0; i < N; i++) if (m[i].val == v) return m[i].name;
    return "?";
}

template <size_t N>
bool valOf(const NameMap (&m)[N], const char* s, uint8_t& out) {
    if (!s) return false;
    for (size_t i = 0; i < N; i++)
        if (strcasecmp(s, m[i].name) == 0) { out = m[i].val; return true; }
    return false;
}

} // namespace

void controlExportRule(JsonObject obj, const Rule& r) {
    obj["id"]         = r.id;
    obj["input"]      = r.sourceInput;
    obj["condition"]  = nameOf(CONDITIONS, r.condition);
    obj["severity"]   = nameOf(SEVERITIES, r.severity);
    obj["limitLo"]    = r.limitLo;
    obj["limitHi"]    = r.limitHi;
    obj["hysteresis"] = r.hysteresis;
    obj["warmupMs"]   = r.warmupMs;
    obj["debounceMs"] = r.debounceMs;
    obj["enabled"]    = (r.flags & RULE_FLAG_ENABLED) != 0;
}

bool controlImportRule(JsonObjectConst obj, Rule& out) {
    // Required: id, input, condition. Severity defaults to NONE; the numeric
    // fields default to the hand-entry fallbacks.
    if (obj["id"].isNull() || obj["input"].isNull()) return false;

    // CONTROL_ID_NONE (0xFF) is the empty-slot sentinel — a record carrying it
    // would silently vanish from later exports, so reject it outright.
    uint8_t id = obj["id"];
    if (id == CONTROL_ID_NONE) return false;

    uint8_t condition;
    if (!valOf(CONDITIONS, obj["condition"], condition)) return false;

    uint8_t severity;
    if (!valOf(SEVERITIES, obj["severity"] | "NONE", severity)) return false;

    float limitLo = obj["limitLo"] | 0.0f;
    float limitHi = obj["limitHi"] | 0.0f;
    // Band conditions need a real interval (mirrors the RULE ADD CLI check). A
    // missing/zero-width/inverted band is nonsense, not a safe default.
    if ((condition == RULE_OUTSIDE_BAND || condition == RULE_INSIDE_BAND) &&
        !(limitLo < limitHi)) {
        return false;
    }

    out.id          = id;
    out.sourceInput = obj["input"];
    out.condition   = condition;
    out.severity    = severity;
    out.limitLo     = limitLo;
    out.limitHi     = limitHi;
    out.hysteresis  = obj["hysteresis"] | 0.0f;
    out.warmupMs    = obj["warmupMs"]   | (uint16_t)RULE_DEFAULT_WARMUP_MS;
    out.debounceMs  = obj["debounceMs"] | (uint16_t)RULE_DEFAULT_DEBOUNCE_MS;
    out.flags       = (obj["enabled"] | true) ? RULE_FLAG_ENABLED : 0;
    return true;
}

void controlExportAction(JsonObject obj, const Action& a) {
    obj["id"]     = a.id;
    obj["type"]   = nameOf(ACTION_TYPES, a.type);
    obj["target"] = a.target;
    if (a.ruleRef != CONTROL_ID_NONE) obj["rule"] = a.ruleRef;  // omit = manual
    obj["invert"] = (a.behavior & ACT_BEHAVIOR_INVERT) != 0;
    if (a.type == ACT_RELAY)
        obj["failsafe"] = (a.failsafe == ACT_FAILSAFE_ON) ? "ON" : "OFF";
}

bool controlImportAction(JsonObjectConst obj, Action& out) {
    // Required: id, type, target. A missing/null "rule" means manual-only.
    if (obj["id"].isNull() || obj["target"].isNull()) return false;

    // Reject the empty-slot sentinel (see controlImportRule).
    uint8_t id = obj["id"];
    if (id == CONTROL_ID_NONE) return false;

    uint8_t type;
    if (!valOf(ACTION_TYPES, obj["type"], type)) return false;

    out.id       = id;
    out.type     = type;
    out.target   = obj["target"];
    out.ruleRef  = obj["rule"].isNull() ? CONTROL_ID_NONE : (uint8_t)obj["rule"];
    out.behavior = (obj["invert"] | false) ? ACT_BEHAVIOR_INVERT : 0;
    const char* fs = obj["failsafe"] | "OFF";
    out.failsafe = (strcasecmp(fs, "ON") == 0) ? ACT_FAILSAFE_ON : ACT_FAILSAFE_OFF;
    return true;
}

#endif // SUPPORTS_JSON_CONFIG && ENABLE_CONTROL_ENGINE
