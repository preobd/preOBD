/*
 * rules_json.cpp - Rule/Action <-> JSON leaf converters. See rules_json.h.
 */

#include "rules_json.h"

#if SUPPORTS_JSON_CONFIG && INCLUDE_RULES_ENGINE

#include <string.h>
#include "rules_engine.h"  // rulesNormalizeWarn (warn tri-state import, #425)

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
    {"INFO",    RULE_SEV_INFO},
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

// Tri-state warn-side export (#425, slice B): SET -> the stored number,
// NONE -> explicit null, AUTO -> key absent entirely (so a pre-#425 export
// and an AUTO-mode export are byte-for-byte identical, and an absent key on
// import means exactly "AUTO", not "assume 0"). Callers pass whichever of
// "warnLo"/"warnHi" applies.
static void exportWarnSide(JsonObject obj, const char* key, const Rule& r, bool hiSide, float value) {
    switch (ruleWarnMode(r, hiSide)) {
        case RULE_WARN_SET:  obj[key] = value; break;
        case RULE_WARN_NONE: obj[key] = nullptr; break;
        case RULE_WARN_AUTO: default: /* key omitted */ break;
    }
}

void rulesExportRule(JsonObject obj, const Rule& r) {
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
    // Warn tri-state (#425): only meaningful for an ALARM rule; warnHi only for
    // OUTSIDE_BAND (ABOVE/BELOW have no hi side, so never emit it — a reader
    // must not infer a hi-side tier that can't exist for the shape).
    if (r.severity == RULE_SEV_ALARM) {
        exportWarnSide(obj, "warnLo", r, false, r.warnLo);
        if (r.condition == RULE_OUTSIDE_BAND) {
            exportWarnSide(obj, "warnHi", r, true, r.warnHi);
        }
    }
}

bool rulesImportRule(JsonObjectConst obj, Rule& out) {
    // Required: id, input, condition. Severity defaults to NONE; the numeric
    // fields default to the hand-entry fallbacks.
    if (obj["id"].isNull() || obj["input"].isNull()) return false;

    // RULES_ID_NONE (0xFF) is the empty-slot sentinel — a record carrying it
    // would silently vanish from later exports, so reject it outright.
    uint8_t id = obj["id"];
    if (id == RULES_ID_NONE) return false;

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

    // Warn tri-state (#425, slice B): absent key -> AUTO, JSON null -> NONE,
    // number -> SET. MUST use isUnbound(), not isNull() alone — isNull() is
    // true for BOTH "absent" and "present but null" in ArduinoJson 7, which
    // would collapse NONE into AUTO. A non-ALARM rule ignores warn keys
    // entirely (ARCHITECTURE §11.2: the approach tier only exists for ALARM
    // rules) and warnHi is ignored on ABOVE/BELOW (no hi side to hold it) —
    // both leave the mode bits AUTO (0) rather than rejecting the record, so
    // a hand-edited or stale dump never loses a trip point + relay links over
    // a warn key (#425 amendment A3). rulesNormalizeWarn() below degrades any
    // SET side that isn't legal for the final shape/limits back to AUTO — it
    // NEVER rejects the record.
    out.warnLo = 0.0f;
    out.warnHi = 0.0f;
    if (severity == RULE_SEV_ALARM) {
        if (!obj["warnLo"].isUnbound()) {
            if (obj["warnLo"].isNull()) {
                ruleSetWarnMode(out, false, RULE_WARN_NONE);
            } else {
                ruleSetWarnMode(out, false, RULE_WARN_SET);
                out.warnLo = obj["warnLo"];
            }
        }
        if (condition == RULE_OUTSIDE_BAND && !obj["warnHi"].isUnbound()) {
            if (obj["warnHi"].isNull()) {
                ruleSetWarnMode(out, true, RULE_WARN_NONE);
            } else {
                ruleSetWarnMode(out, true, RULE_WARN_SET);
                out.warnHi = obj["warnHi"];
            }
        }
    }
    rulesNormalizeWarn(out);  // degrade, never reject (A2/A3)
    return true;
}

void rulesExportAction(JsonObject obj, const Action& a,
                         const uint8_t* memberRules, uint8_t memberCount) {
    obj["id"]     = a.id;
    obj["type"]   = nameOf(ACTION_TYPES, a.type);
    obj["target"] = a.target;
    if (a.ruleRef != RULES_ID_NONE) obj["rule"] = a.ruleRef;  // omit = manual
    // Multi-member actions ALSO emit the full list (#343). "rule" stays = rules[0]
    // so a reader that predates this key still sees a working single link rather
    // than a manual action.
    if (memberRules && memberCount > 1) {
        JsonArray arr = obj["rules"].to<JsonArray>();
        for (uint8_t i = 0; i < memberCount; i++) arr.add(memberRules[i]);
    }
    if (a.name[0] != '\0') obj["name"] = a.name;                // omit = unnamed
    obj["invert"] = (a.behavior & ACT_BEHAVIOR_INVERT) != 0;
    if (a.type == ACT_RELAY)
        obj["failsafe"] = (a.failsafe == ACT_FAILSAFE_ON) ? "ON" : "OFF";
}

bool rulesImportAction(JsonObjectConst obj, Action& out,
                         uint8_t* extraRules, uint8_t maxExtra, uint8_t* nExtra) {
    if (nExtra) *nExtra = 0;
    // Required: id, type, target. A missing/null "rule"/"rules" means manual-only.
    if (obj["id"].isNull() || obj["target"].isNull()) return false;

    // Reject the empty-slot sentinel (see rulesImportRule).
    uint8_t id = obj["id"];
    if (id == RULES_ID_NONE) return false;

    uint8_t type;
    if (!valOf(ACTION_TYPES, obj["type"], type)) return false;

    out.id       = id;
    out.type     = type;
    out.target   = obj["target"];
    // "rules" (the full member list) wins over "rule" when both are present —
    // export writes them consistently, and a hand-edited config that disagrees
    // meant the list it spelled out in full.
    out.ruleRef = RULES_ID_NONE;
    JsonArrayConst members = obj["rules"];
    if (!members.isNull()) {
        for (JsonVariantConst v : members) {
            uint8_t rid = v.as<uint8_t>();
            if (rid == RULES_ID_NONE) continue;      // sentinel is never a member
            if (out.ruleRef == RULES_ID_NONE) {
                out.ruleRef = rid;                     // first member goes inline
            } else if (extraRules && nExtra && *nExtra < maxExtra) {
                extraRules[(*nExtra)++] = rid;
            }
        }
    } else if (!obj["rule"].isNull()) {
        out.ruleRef = (uint8_t)obj["rule"];
    }
    out.behavior = (obj["invert"] | false) ? ACT_BEHAVIOR_INVERT : 0;
    const char* fs = obj["failsafe"] | "OFF";
    out.failsafe = (strcasecmp(fs, "ON") == 0) ? ACT_FAILSAFE_ON : ACT_FAILSAFE_OFF;
    const char* nm = obj["name"] | "";  // missing/null "name" = unnamed
    strncpy(out.name, nm, sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    return true;
}

#endif // SUPPORTS_JSON_CONFIG && INCLUDE_RULES_ENGINE
