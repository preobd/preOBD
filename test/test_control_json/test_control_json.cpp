// Native unit test: control-engine Rule/Action <-> JSON leaf converters
// (src/lib/control_json.cpp). These are pure (no global state), so the full
// export -> serialize -> parse -> import round-trip runs deterministically on
// the host. Covers field fidelity, enum string mapping, the manual-Action
// "rule" omission, default fill-in, and validation rejection.
#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include "../../src/lib/control_json.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- helpers ------------------------------------------------------------

static Rule makeRule(uint8_t cond, float lo, float hi, uint8_t sev) {
    Rule r{};
    r.id = 3;
    r.sourceInput = 14;
    r.condition = cond;
    r.severity = sev;
    r.flags = RULE_FLAG_ENABLED;
    r.limitLo = lo;
    r.limitHi = hi;
    r.hysteresis = 2.5f;
    r.warmupMs = 60000;
    r.debounceMs = 5000;
    return r;
}

static Action makeAction(uint8_t type, uint8_t target, uint8_t ruleRef) {
    Action a{};
    a.id = 1;
    a.type = type;
    a.target = target;
    a.ruleRef = ruleRef;
    a.behavior = ACT_BEHAVIOR_INVERT;
    a.failsafe = ACT_FAILSAFE_ON;
    return a;
}

// Full round-trip through a serialized string (catches anything export emits
// that import can't read back, and vice versa).
static Rule roundTripRule(const Rule& in, bool* ok = nullptr) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    controlExportRule(o, in);
    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    JsonDocument doc2;
    deserializeJson(doc2, buf);
    Rule out{};
    bool r = controlImportRule(doc2.as<JsonObjectConst>(), out);
    if (ok) *ok = r;
    return out;
}

static Action roundTripAction(const Action& in, bool* ok = nullptr) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    controlExportAction(o, in);
    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    JsonDocument doc2;
    deserializeJson(doc2, buf);
    Action out{};
    bool r = controlImportAction(doc2.as<JsonObjectConst>(), out);
    if (ok) *ok = r;
    return out;
}

// --- Rule round-trips ---------------------------------------------------

static void test_rule_outside_band_roundtrip(void) {
    Rule in = makeRule(RULE_OUTSIDE_BAND, -1.0f, 100.0f, RULE_SEV_ALARM);
    bool ok = false;
    Rule out = roundTripRule(in, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(in.id, out.id);
    TEST_ASSERT_EQUAL_UINT8(in.sourceInput, out.sourceInput);
    TEST_ASSERT_EQUAL_UINT8(RULE_OUTSIDE_BAND, out.condition);
    TEST_ASSERT_EQUAL_UINT8(RULE_SEV_ALARM, out.severity);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, out.limitLo);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, out.limitHi);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, out.hysteresis);
    TEST_ASSERT_EQUAL_UINT16(60000, out.warmupMs);
    TEST_ASSERT_EQUAL_UINT16(5000, out.debounceMs);
    TEST_ASSERT_EQUAL_UINT8(RULE_FLAG_ENABLED, out.flags & RULE_FLAG_ENABLED);
}

static void test_rule_above_roundtrip(void) {
    Rule in = makeRule(RULE_ABOVE, 240.0f, 0.0f, RULE_SEV_WARNING);
    bool ok = false;
    Rule out = roundTripRule(in, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(RULE_ABOVE, out.condition);
    TEST_ASSERT_EQUAL_UINT8(RULE_SEV_WARNING, out.severity);
    TEST_ASSERT_EQUAL_FLOAT(240.0f, out.limitLo);
}

static void test_rule_disabled_flag_roundtrip(void) {
    Rule in = makeRule(RULE_BELOW, 5.0f, 0.0f, RULE_SEV_NONE);
    in.flags = 0;  // disabled
    bool ok = false;
    Rule out = roundTripRule(in, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(0, out.flags & RULE_FLAG_ENABLED);
    TEST_ASSERT_EQUAL_UINT8(RULE_SEV_NONE, out.severity);
}

static void test_rule_export_enum_strings(void) {
    Rule in = makeRule(RULE_OUTSIDE_BAND, -1.0f, 100.0f, RULE_SEV_ALARM);
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    controlExportRule(o, in);
    TEST_ASSERT_EQUAL_STRING("OUTSIDE", o["condition"]);
    TEST_ASSERT_EQUAL_STRING("ALARM", o["severity"]);
    TEST_ASSERT_TRUE(o["enabled"].as<bool>());
}

// --- Rule validation / defaults ----------------------------------------

static void test_rule_import_missing_id_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["input"] = 14;
    o["condition"] = "ABOVE";
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_missing_condition_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["input"] = 14;
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_bad_condition_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["input"] = 14;
    o["condition"] = "SIDEWAYS";
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_sentinel_id_rejected(void) {
    // CONTROL_ID_NONE (0xFF) is the empty-slot sentinel; a record carrying it
    // would silently vanish from later exports.
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = CONTROL_ID_NONE;
    o["input"] = 14;
    o["condition"] = "ABOVE";
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_outside_missing_hi_rejected(void) {
    // OUTSIDE with no limitHi -> zero/inverted band -> reject (not silent 0.0).
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["input"] = 14;
    o["condition"] = "OUTSIDE";
    o["limitLo"] = 10.0f;
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_outside_inverted_band_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["input"] = 14;
    o["condition"] = "OUTSIDE";
    o["limitLo"] = 100.0f;
    o["limitHi"] = 10.0f;   // hi < lo
    Rule out{};
    TEST_ASSERT_FALSE(controlImportRule(doc.as<JsonObjectConst>(), out));
}

static void test_action_import_sentinel_id_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = CONTROL_ID_NONE;
    o["type"] = "RELAY";
    o["target"] = 13;
    Action out{};
    TEST_ASSERT_FALSE(controlImportAction(doc.as<JsonObjectConst>(), out));
}

static void test_rule_import_defaults(void) {
    // Minimal record: severity, hysteresis, timings, enabled all defaulted.
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 7;
    o["input"] = 20;
    o["condition"] = "below";  // case-insensitive
    o["limitLo"] = 9.5f;
    Rule out{};
    TEST_ASSERT_TRUE(controlImportRule(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_UINT8(RULE_BELOW, out.condition);
    TEST_ASSERT_EQUAL_UINT8(RULE_SEV_NONE, out.severity);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.hysteresis);
    TEST_ASSERT_EQUAL_UINT16(RULE_DEFAULT_WARMUP_MS, out.warmupMs);
    TEST_ASSERT_EQUAL_UINT16(RULE_DEFAULT_DEBOUNCE_MS, out.debounceMs);
    TEST_ASSERT_EQUAL_UINT8(RULE_FLAG_ENABLED, out.flags & RULE_FLAG_ENABLED);
}

// --- Action round-trips -------------------------------------------------

static void test_action_linked_roundtrip(void) {
    Action in = makeAction(ACT_RELAY, 13, 3);  // linked to rule #3
    bool ok = false;
    Action out = roundTripAction(in, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(in.id, out.id);
    TEST_ASSERT_EQUAL_UINT8(ACT_RELAY, out.type);
    TEST_ASSERT_EQUAL_UINT8(13, out.target);
    TEST_ASSERT_EQUAL_UINT8(3, out.ruleRef);
    TEST_ASSERT_EQUAL_UINT8(ACT_BEHAVIOR_INVERT, out.behavior & ACT_BEHAVIOR_INVERT);
    TEST_ASSERT_EQUAL_UINT8(ACT_FAILSAFE_ON, out.failsafe);
}

static void test_action_manual_omits_rule(void) {
    Action in = makeAction(ACT_RELAY, 5, CONTROL_ID_NONE);  // manual
    in.behavior = 0;
    in.failsafe = ACT_FAILSAFE_OFF;
    // Export must NOT emit a "rule" key for a manual action.
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    controlExportAction(o, in);
    TEST_ASSERT_TRUE(o["rule"].isNull());
    // ...and import maps the absence back to manual.
    bool ok = false;
    Action out = roundTripAction(in, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, out.ruleRef);
    TEST_ASSERT_EQUAL_UINT8(0, out.behavior & ACT_BEHAVIOR_INVERT);
    TEST_ASSERT_EQUAL_UINT8(ACT_FAILSAFE_OFF, out.failsafe);
}

static void test_action_import_missing_type_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["target"] = 13;
    Action out{};
    TEST_ASSERT_FALSE(controlImportAction(doc.as<JsonObjectConst>(), out));
}

static void test_action_import_bad_type_rejected(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 1;
    o["type"] = "SERVO";
    o["target"] = 13;
    Action out{};
    TEST_ASSERT_FALSE(controlImportAction(doc.as<JsonObjectConst>(), out));
}

static void test_action_failsafe_defaults_off(void) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["id"] = 2;
    o["type"] = "relay";   // case-insensitive
    o["target"] = 8;
    Action out{};
    TEST_ASSERT_TRUE(controlImportAction(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_UINT8(ACT_FAILSAFE_OFF, out.failsafe);
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, out.ruleRef);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rule_outside_band_roundtrip);
    RUN_TEST(test_rule_above_roundtrip);
    RUN_TEST(test_rule_disabled_flag_roundtrip);
    RUN_TEST(test_rule_export_enum_strings);
    RUN_TEST(test_rule_import_missing_id_rejected);
    RUN_TEST(test_rule_import_missing_condition_rejected);
    RUN_TEST(test_rule_import_bad_condition_rejected);
    RUN_TEST(test_rule_import_sentinel_id_rejected);
    RUN_TEST(test_rule_import_outside_missing_hi_rejected);
    RUN_TEST(test_rule_import_outside_inverted_band_rejected);
    RUN_TEST(test_action_import_sentinel_id_rejected);
    RUN_TEST(test_rule_import_defaults);
    RUN_TEST(test_action_linked_roundtrip);
    RUN_TEST(test_action_manual_omits_rule);
    RUN_TEST(test_action_import_missing_type_rejected);
    RUN_TEST(test_action_import_bad_type_rejected);
    RUN_TEST(test_action_failsafe_defaults_off);
    return UNITY_END();
}
