// Native unit test: shared CSV field formatting (src/outputs/output_format.h).
//
// These two helpers are the single definition of "how a reading looks in CSV"
// for both recording outputs (serial + SD). Before they existed the two paths
// had drifted: serial printed ERROR for a faulted sensor while SD dropped the
// row, and SD dereferenced the PROGMEM unit pointer as a plain const char*.
// The cases below pin both behaviours so they can't diverge again.
#include <unity.h>
#include <Arduino.h>
#include <math.h>

#include "../../src/outputs/output_format.h"

void setUp(void) {}
void tearDown(void) {}

// ===== formatValueField =====

static void test_value_two_decimals(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), 87.5f);
    TEST_ASSERT_EQUAL_STRING("87.50", buf);
}

static void test_value_rounds_to_two_decimals(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), 610.006f);
    TEST_ASSERT_EQUAL_STRING("610.01", buf);
}

static void test_value_negative(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), -38.5f);
    TEST_ASSERT_EQUAL_STRING("-38.50", buf);
}

static void test_value_zero(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), 0.0f);
    TEST_ASSERT_EQUAL_STRING("0.00", buf);
}

// A disconnected sensor reads NAN. It must be visible as a fault in the log,
// not dropped (the old SD path) and not rendered "nan".
static void test_value_nan_is_error(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), NAN);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);
}

// Gated on isfinite(), not !isnan() — INF must not ship as a plausible reading.
static void test_value_infinity_is_error(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), INFINITY);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);

    formatValueField(buf, sizeof(buf), -INFINITY);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);
}

// dtostrf() has no bounds check: a float near FLT_MAX needs 40+ characters and
// would run off the end of a VALUE_FIELD_LEN buffer. Guard before formatting.
// Reported as ERROR rather than a distinct token — an unusable reading is an
// unusable reading, and one sentinel keeps the value column simple to parse.
static void test_value_overflow_guarded(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), 3.4e38f);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);

    formatValueField(buf, sizeof(buf), -3.4e38f);
    TEST_ASSERT_EQUAL_STRING("ERROR", buf);
}

// The largest value that still formats normally must fit the documented buffer.
static void test_value_at_limit_fits_buffer(void) {
    char buf[VALUE_FIELD_LEN];
    formatValueField(buf, sizeof(buf), -99999998.0f);
    TEST_ASSERT_TRUE(strlen(buf) < VALUE_FIELD_LEN);
    TEST_ASSERT_NOT_EQUAL('\0', buf[0]);
}

static void test_value_null_buffer_is_safe(void) {
    formatValueField(nullptr, 0, 12.0f);   // must not crash
    char buf[VALUE_FIELD_LEN] = "untouched";
    formatValueField(buf, 0, 12.0f);
    TEST_ASSERT_EQUAL_STRING("untouched", buf);
}

// ===== formatUnitSymbol =====

static const char PSTR_TEST_C[] PROGMEM = "C";
static const char PSTR_TEST_KMH[] PROGMEM = "km/h";
static const char PSTR_TEST_EMPTY[] PROGMEM = "";
static const char PSTR_TEST_LONG[] PROGMEM = "abcdefghijklmnop";

static void test_unit_copies_symbol(void) {
    char buf[UNIT_FIELD_LEN];
    formatUnitSymbol(buf, sizeof(buf), PSTR_TEST_C);
    TEST_ASSERT_EQUAL_STRING("C", buf);
}

static void test_unit_copies_multichar_symbol(void) {
    char buf[UNIT_FIELD_LEN];
    formatUnitSymbol(buf, sizeof(buf), PSTR_TEST_KMH);
    TEST_ASSERT_EQUAL_STRING("km/h", buf);
}

// An invalid units index yields the registry's empty PROGMEM string; callers
// test buf[0] to decide whether to emit a separator, so it must terminate.
static void test_unit_empty_symbol(void) {
    char buf[UNIT_FIELD_LEN];
    formatUnitSymbol(buf, sizeof(buf), PSTR_TEST_EMPTY);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_unit_null_symbol_terminates(void) {
    char buf[UNIT_FIELD_LEN];
    formatUnitSymbol(buf, sizeof(buf), nullptr);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// strncpy does not terminate when the source fills the buffer.
static void test_unit_oversized_symbol_truncates_safely(void) {
    char buf[UNIT_FIELD_LEN];
    formatUnitSymbol(buf, sizeof(buf), PSTR_TEST_LONG);
    TEST_ASSERT_EQUAL('\0', buf[UNIT_FIELD_LEN - 1]);
    TEST_ASSERT_EQUAL_UINT(UNIT_FIELD_LEN - 1, strlen(buf));
}

static void test_unit_null_buffer_is_safe(void) {
    formatUnitSymbol(nullptr, 0, PSTR_TEST_C);   // must not crash
}

// ===== Column signature =====
//
// Mirrors computeColumnSignature() in output_sdlog.cpp. That function reads the
// firmware's global inputs[] table, which does not link on the host, so the
// mixing step is reproduced here over plain values. The contract under test is
// the one the logger depends on: any change to which slots are logged, or to
// their names or units, must change the signature — that is what triggers a
// file roll instead of silently shifting columns under an existing header.

#include "../../src/core/hash.h"

struct FakeColumn {
    uint8_t     slot;
    uint8_t     unitsIndex;
    const char* abbrName;
    bool        logged;
};

static uint32_t signatureOf(const FakeColumn* cols, uint8_t count) {
    uint32_t sig = 5381;
    for (uint8_t i = 0; i < count; i++) {
        if (!cols[i].logged) continue;
        sig = sig * 31u + cols[i].slot;
        sig = sig * 31u + cols[i].unitsIndex;
        sig = sig * 31u + djb2_hash(cols[i].abbrName);
    }
    return sig;
}

static void baseline(FakeColumn* out) {
    out[0] = {0, 0, "CLT",  true};
    out[1] = {1, 3, "OILP", true};
    out[2] = {2, 0, "EGT",  true};
}

static void test_signature_stable_for_identical_set(void) {
    FakeColumn a[3], b[3];
    baseline(a);
    baseline(b);
    TEST_ASSERT_EQUAL_UINT32(signatureOf(a, 3), signatureOf(b, 3));
}

static void test_signature_changes_on_units_change(void) {
    FakeColumn a[3], b[3];
    baseline(a);
    baseline(b);
    b[0].unitsIndex = 1;   // C -> F
    TEST_ASSERT_NOT_EQUAL(signatureOf(a, 3), signatureOf(b, 3));
}

static void test_signature_changes_on_rename(void) {
    FakeColumn a[3], b[3];
    baseline(a);
    baseline(b);
    b[2].abbrName = "EGT2";
    TEST_ASSERT_NOT_EQUAL(signatureOf(a, 3), signatureOf(b, 3));
}

// SET <pin> OUTPUT SD_LOG DISABLE drops a column mid-log.
static void test_signature_changes_on_routing_toggle(void) {
    FakeColumn a[3], b[3];
    baseline(a);
    baseline(b);
    b[1].logged = false;
    TEST_ASSERT_NOT_EQUAL(signatureOf(a, 3), signatureOf(b, 3));
}

// Same names and units, different slots: the column order differs, so the
// header would no longer describe the rows.
static void test_signature_changes_on_slot_move(void) {
    FakeColumn a[3], b[3];
    baseline(a);
    baseline(b);
    b[2].slot = 7;
    TEST_ASSERT_NOT_EQUAL(signatureOf(a, 3), signatureOf(b, 3));
}

// Unlogged slots must not contribute — an input that is not routed to SD can be
// added or reconfigured without forcing a pointless file roll.
static void test_signature_ignores_unlogged_slots(void) {
    FakeColumn a[4], b[4];
    baseline(a);
    a[3] = {9, 0, "AUX", false};
    baseline(b);
    b[3] = {9, 2, "OTHER", false};
    TEST_ASSERT_EQUAL_UINT32(signatureOf(a, 4), signatureOf(b, 4));
}

// ===== Sequential log filenames =====
//
// Mirrors claimNextFileName() in output_sdlog.cpp against an injected exists()
// predicate, since SD.exists() needs a card. The 8.3 short-name limit is the
// point: the old log_<millis>.csv scheme produced a 10-character basename once
// millis() reached 100000, which SD.open() rejects outright.

#define TEST_MAX_FILE_INDEX 9999

typedef bool (*ExistsFn)(const char*);

static bool claimNext(char* name, size_t len, uint16_t* lastIndex, ExistsFn exists) {
    for (uint16_t i = *lastIndex + 1; i <= TEST_MAX_FILE_INDEX; i++) {
        snprintf(name, len, "LOG%04u.CSV", (unsigned)i);
        if (!exists(name)) {
            *lastIndex = i;
            return true;
        }
    }
    return false;
}

static bool existsNone(const char*) { return false; }
static bool existsAll(const char*)  { return true; }
static bool existsFirstThree(const char* n) {
    return strcmp(n, "LOG0001.CSV") == 0 ||
           strcmp(n, "LOG0002.CSV") == 0 ||
           strcmp(n, "LOG0003.CSV") == 0;
}

static void test_filename_first_free_on_empty_card(void) {
    char name[13];
    uint16_t last = 0;
    TEST_ASSERT_TRUE(claimNext(name, sizeof(name), &last, existsNone));
    TEST_ASSERT_EQUAL_STRING("LOG0001.CSV", name);
    TEST_ASSERT_EQUAL_UINT16(1, last);
}

static void test_filename_skips_existing(void) {
    char name[13];
    uint16_t last = 0;
    TEST_ASSERT_TRUE(claimNext(name, sizeof(name), &last, existsFirstThree));
    TEST_ASSERT_EQUAL_STRING("LOG0004.CSV", name);
}

// A power cycle must not reuse the previous session's name — that is what made
// the old millis()-derived scheme append two drives into one file.
static void test_filename_advances_across_rotations(void) {
    char name[13];
    uint16_t last = 0;
    claimNext(name, sizeof(name), &last, existsNone);
    TEST_ASSERT_EQUAL_STRING("LOG0001.CSV", name);
    claimNext(name, sizeof(name), &last, existsNone);
    TEST_ASSERT_EQUAL_STRING("LOG0002.CSV", name);
    claimNext(name, sizeof(name), &last, existsNone);
    TEST_ASSERT_EQUAL_STRING("LOG0003.CSV", name);
}

static void test_filename_exhaustion_reports_failure(void) {
    char name[13];
    uint16_t last = 0;
    TEST_ASSERT_FALSE(claimNext(name, sizeof(name), &last, existsAll));
}

// Every reachable index must fit 8.3 — at most an 8-char base, '.', and at most
// a 3-char extension. This is the constraint the old log_<millis>.csv scheme
// violated once millis() reached 100000 ("log_123456" is 10 chars), which made
// SD.open() fail and logging silently never start. "LOG0001" is 7 chars, so
// there is one character of headroom; the assertion is the limit, not the exact
// width, so widening the prefix later still gets caught.
static void test_filename_is_8_3_for_every_index(void) {
    char name[13];
    for (uint16_t i = 1; i <= TEST_MAX_FILE_INDEX; i++) {
        snprintf(name, sizeof(name), "LOG%04u.CSV", (unsigned)i);

        const char* dot = strchr(name, '.');
        if (dot == nullptr) {
            TEST_ASSERT_NOT_NULL(dot);
            return;                     // one report, not 9999
        }
        size_t base = (size_t)(dot - name);
        size_t ext  = strlen(dot + 1);
        if (base == 0 || base > 8 || ext > 3 || strlen(name) >= sizeof(name)) {
            printf("  offending name at index %u: \"%s\"\n", (unsigned)i, name);
            TEST_ASSERT_TRUE(base > 0 && base <= 8 && ext <= 3);
            return;
        }
    }
    TEST_ASSERT_TRUE(true);   // all 9999 names are 8.3-clean
}

// ===== SD log format selection =====

static void test_parse_format_wide(void) {
    SdLogFormat f = SDLOG_FORMAT_LONG;
    TEST_ASSERT_TRUE(parseSdLogFormat("WIDE", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_WIDE, f);
}

static void test_parse_format_long(void) {
    SdLogFormat f = SDLOG_FORMAT_WIDE;
    TEST_ASSERT_TRUE(parseSdLogFormat("LONG", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);
}

// The CLI accepts commands in any case (see cli_on_command's fallback), so the
// format token must behave the same way.
static void test_parse_format_is_case_insensitive(void) {
    SdLogFormat f = SDLOG_FORMAT_WIDE;
    TEST_ASSERT_TRUE(parseSdLogFormat("long", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);

    TEST_ASSERT_TRUE(parseSdLogFormat("WiDe", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_WIDE, f);
}

// A rejected token must leave the caller's current setting untouched — the CLI
// passes the live config value in, so writing on failure would corrupt it.
static void test_parse_format_rejects_unknown_without_writing(void) {
    SdLogFormat f = SDLOG_FORMAT_LONG;
    TEST_ASSERT_FALSE(parseSdLogFormat("TALL", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);

    TEST_ASSERT_FALSE(parseSdLogFormat("", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);

    TEST_ASSERT_FALSE(parseSdLogFormat(nullptr, f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);
}

// Partial matches must not be accepted — "LON" is a typo, not an abbreviation.
static void test_parse_format_rejects_partial_match(void) {
    SdLogFormat f = SDLOG_FORMAT_WIDE;
    TEST_ASSERT_FALSE(parseSdLogFormat("LON", f));
    TEST_ASSERT_FALSE(parseSdLogFormat("WIDER", f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_WIDE, f);
}

static void test_format_name_round_trips(void) {
    TEST_ASSERT_EQUAL_STRING("WIDE", sdLogFormatName(SDLOG_FORMAT_WIDE));
    TEST_ASSERT_EQUAL_STRING("LONG", sdLogFormatName(SDLOG_FORMAT_LONG));

    SdLogFormat f;
    TEST_ASSERT_TRUE(parseSdLogFormat(sdLogFormatName(SDLOG_FORMAT_LONG), f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_LONG, f);
    TEST_ASSERT_TRUE(parseSdLogFormat(sdLogFormatName(SDLOG_FORMAT_WIDE), f));
    TEST_ASSERT_EQUAL(SDLOG_FORMAT_WIDE, f);
}

// The byte comes from EEPROM, which a firmware downgrade or a corrupt image
// could leave holding anything; naming must not index off the end.
static void test_format_name_tolerates_garbage_byte(void) {
    TEST_ASSERT_EQUAL_STRING("WIDE", sdLogFormatName(200));
    TEST_ASSERT_EQUAL_STRING("WIDE", sdLogFormatName(255));
}

// WIDE must be 0: the config field reuses a formerly-reserved byte, so every
// pre-existing EEPROM image reads back zero and has to land on the default.
static void test_wide_is_zero_for_legacy_eeprom(void) {
    TEST_ASSERT_EQUAL(0, (int)SDLOG_FORMAT_WIDE);
}

// The compatibility contract. A downstream parser binds to these column names,
// so this string must not drift — that is the entire reason LONG still exists.
static void test_long_header_is_byte_identical_to_legacy(void) {
    TEST_ASSERT_EQUAL_STRING("Time,Sensor,Value,Units", SDLOG_LONG_HEADER);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_value_two_decimals);
    RUN_TEST(test_value_rounds_to_two_decimals);
    RUN_TEST(test_value_negative);
    RUN_TEST(test_value_zero);
    RUN_TEST(test_value_nan_is_error);
    RUN_TEST(test_value_infinity_is_error);
    RUN_TEST(test_value_overflow_guarded);
    RUN_TEST(test_value_at_limit_fits_buffer);
    RUN_TEST(test_value_null_buffer_is_safe);

    RUN_TEST(test_unit_copies_symbol);
    RUN_TEST(test_unit_copies_multichar_symbol);
    RUN_TEST(test_unit_empty_symbol);
    RUN_TEST(test_unit_null_symbol_terminates);
    RUN_TEST(test_unit_oversized_symbol_truncates_safely);
    RUN_TEST(test_unit_null_buffer_is_safe);

    RUN_TEST(test_signature_stable_for_identical_set);
    RUN_TEST(test_signature_changes_on_units_change);
    RUN_TEST(test_signature_changes_on_rename);
    RUN_TEST(test_signature_changes_on_routing_toggle);
    RUN_TEST(test_signature_changes_on_slot_move);
    RUN_TEST(test_signature_ignores_unlogged_slots);

    RUN_TEST(test_filename_first_free_on_empty_card);
    RUN_TEST(test_filename_skips_existing);
    RUN_TEST(test_filename_advances_across_rotations);
    RUN_TEST(test_filename_exhaustion_reports_failure);
    RUN_TEST(test_filename_is_8_3_for_every_index);

    RUN_TEST(test_parse_format_wide);
    RUN_TEST(test_parse_format_long);
    RUN_TEST(test_parse_format_is_case_insensitive);
    RUN_TEST(test_parse_format_rejects_unknown_without_writing);
    RUN_TEST(test_parse_format_rejects_partial_match);
    RUN_TEST(test_format_name_round_trips);
    RUN_TEST(test_format_name_tolerates_garbage_byte);
    RUN_TEST(test_wide_is_zero_for_legacy_eeprom);
    RUN_TEST(test_long_header_is_byte_identical_to_legacy);

    return UNITY_END();
}
