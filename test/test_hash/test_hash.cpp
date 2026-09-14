// Native unit test: DJB2 registry hashing (src/core/hash.h).
//
// hash.h is header-only and case-insensitive. These golden vectors pin the
// algorithm so a refactor can't silently shift hashes (which would break
// EEPROM portability across firmware versions).
#include <unity.h>
#include <Arduino.h>

#include "../../src/core/hash.h"

void setUp(void) {}
void tearDown(void) {}

// Hand-computed: hash=5381; 'A'(65): 5381*33+65 = 177638; & 0xFFFF = 46566.
static void test_golden_single_char(void) {
    TEST_ASSERT_EQUAL_UINT16(46566, djb2_hash("A"));
}

// Empty string => seed value, masked to 16 bits.
static void test_golden_empty(void) {
    TEST_ASSERT_EQUAL_UINT16(5381, djb2_hash(""));
}

// Case-insensitive: lookups must not depend on user capitalization.
static void test_case_insensitive(void) {
    TEST_ASSERT_EQUAL_UINT16(djb2_hash("celsius"), djb2_hash("CELSIUS"));
    TEST_ASSERT_EQUAL_UINT16(djb2_hash("OilPress"), djb2_hash("OILPRESS"));
}

// Distinct strings should not collide on these common registry names.
static void test_distinct_strings_differ(void) {
    TEST_ASSERT_NOT_EQUAL(djb2_hash("CELSIUS"), djb2_hash("FAHRENHEIT"));
    TEST_ASSERT_NOT_EQUAL(djb2_hash("CHT"), djb2_hash("EGT"));
}

// Null is defined to hash to 0.
static void test_null_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, djb2_hash(nullptr));
}

// PROGMEM variant must agree with the RAM variant for the same bytes.
static void test_progmem_variant_matches(void) {
    static const char s[] = "COOLANT_TEMP";
    TEST_ASSERT_EQUAL_UINT16(djb2_hash(s), djb2_hash_P(s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_single_char);
    RUN_TEST(test_golden_empty);
    RUN_TEST(test_case_insensitive);
    RUN_TEST(test_distinct_strings_differ);
    RUN_TEST(test_null_is_zero);
    RUN_TEST(test_progmem_variant_matches);
    return UNITY_END();
}
