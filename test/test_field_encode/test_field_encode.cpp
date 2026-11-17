// Native unit test: the shared scaled-field encoder (src/outputs/formats/field_encode.cpp, #612).
#include <unity.h>
#include <math.h>
#include <string.h>

#include "../../src/outputs/formats/field_encode.cpp"

void setUp(void) {}
void tearDown(void) {}

static uint32_t be(const uint8_t* p, uint8_t n) {
    uint32_t r = 0;
    for (uint8_t i = 0; i < n; i++) r = (r << 8) | p[i];
    return r;
}

static uint32_t enc(float v, float scale, float off, uint8_t len, uint8_t flags, bool res) {
    uint8_t o[4] = {0};
    TEST_ASSERT_EQUAL_UINT8(len, encodeScaledField(v, scale, off, len, flags, res, o));
    if (flags & FIELD_LITTLE_ENDIAN) {
        uint32_t r = 0;
        for (uint8_t i = len; i-- > 0;) r = (r << 8) | o[i];
        return r;
    }
    return be(o, len);
}

static void test_unsigned_basic_and_endian(void) {
    uint8_t o[4];
    TEST_ASSERT_EQUAL_UINT8(2, encodeScaledField(3000.0f, 0.25f, 0.0f, 2, 0, false, o));   // 12000 = 0x2EE0
    TEST_ASSERT_EQUAL_HEX8(0x2E, o[0]); TEST_ASSERT_EQUAL_HEX8(0xE0, o[1]);
    TEST_ASSERT_EQUAL_UINT8(2, encodeScaledField(3000.0f, 0.25f, 0.0f, 2, FIELD_LITTLE_ENDIAN, false, o));
    TEST_ASSERT_EQUAL_HEX8(0xE0, o[0]); TEST_ASSERT_EQUAL_HEX8(0x2E, o[1]);
    TEST_ASSERT_EQUAL_UINT8(4, encodeScaledField(305419896.0f, 1.0f, 0.0f, 4, 0, false, o));
    TEST_ASSERT_EQUAL_UINT32(305419896u & 0xFFFFFF00u, be(o, 4) & 0xFFFFFF00u);   // float: 24-bit mantissa
    TEST_ASSERT_EQUAL_UINT8(4, encodeScaledField(65536.0f, 1.0f, 0.0f, 4, FIELD_LITTLE_ENDIAN, false, o));
    TEST_ASSERT_EQUAL_HEX8(0, o[0]); TEST_ASSERT_EQUAL_HEX8(0, o[1]);
    TEST_ASSERT_EQUAL_HEX8(1, o[2]); TEST_ASSERT_EQUAL_HEX8(0, o[3]);
    TEST_ASSERT_EQUAL_UINT8(1, encodeScaledField(90.0f, 1.0f, -40.0f, 1, 0, false, o));    // A-40
    TEST_ASSERT_EQUAL_HEX8(130, o[0]);
}

static void test_unsigned_rounding_and_clamp(void) {
    TEST_ASSERT_EQUAL_UINT32(5, enc(4.5f, 1, 0, 1, 0, false));        // half up
    TEST_ASSERT_EQUAL_UINT32(4, enc(4.4999f, 1, 0, 1, 0, false));
    TEST_ASSERT_EQUAL_UINT32(0, enc(0.4999f, 1, 0, 1, 0, false));
    TEST_ASSERT_EQUAL_UINT32(1, enc(0.5f, 1, 0, 1, 0, false));
    TEST_ASSERT_EQUAL_UINT32(0, enc(-5.0f, 1, 0, 1, 0, false));       // negative clamps to 0
    TEST_ASSERT_EQUAL_UINT32(0, enc(-1e30f, 1, 0, 2, 0, false));
    TEST_ASSERT_EQUAL_UINT32(255, enc(255.0f, 1, 0, 1, 0, false));    // exact max
    TEST_ASSERT_EQUAL_UINT32(255, enc(254.5f, 1, 0, 1, 0, false));
    TEST_ASSERT_EQUAL_UINT32(255, enc(1e9f, 1, 0, 1, 0, false));
    TEST_ASSERT_EQUAL_UINT32(65535, enc(65535.0f, 1, 0, 2, 0, false));
    TEST_ASSERT_EQUAL_UINT32(65535, enc(1e30f, 1, 0, 2, 0, false));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, enc(1e30f, 1, 0, 4, 0, false));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, enc(4294967296.0f, 1, 0, 4, 0, false));
}

static void test_non_finite_and_bad_length(void) {
    uint8_t o[4] = {9, 9, 9, 9};
    TEST_ASSERT_EQUAL_UINT8(0, encodeScaledField(NAN, 1, 0, 2, 0, false, o));
    TEST_ASSERT_EQUAL_UINT8(0, encodeScaledField(INFINITY, 1, 0, 2, 0, true, o));
    TEST_ASSERT_EQUAL_UINT8(0, encodeScaledField(-INFINITY, 1, 0, 2, FIELD_SIGNED, false, o));
    TEST_ASSERT_EQUAL_UINT8(0, encodeScaledField(1.0f, 1, 0, 3, 0, false, o));
    TEST_ASSERT_EQUAL_UINT8(0, encodeScaledField(1.0f, 1, 0, 0, 0, false, o));
    TEST_ASSERT_EQUAL_HEX8(9, o[0]); TEST_ASSERT_EQUAL_HEX8(9, o[3]);   // untouched
    fieldAllOnes(3, o, 0);
    TEST_ASSERT_EQUAL_HEX8(9, o[0]);
}

static void test_reserve_all_ones(void) {
    // Asked: real values stop one short of the N/A pattern
    TEST_ASSERT_EQUAL_UINT32(254, enc(255.0f, 1, 0, 1, 0, true));
    TEST_ASSERT_EQUAL_UINT32(254, enc(1e9f, 1, 0, 1, 0, true));
    TEST_ASSERT_EQUAL_UINT32(254, enc(254.0f, 1, 0, 1, 0, true));
    TEST_ASSERT_EQUAL_UINT32(253, enc(253.0f, 1, 0, 1, 0, true));
    TEST_ASSERT_EQUAL_UINT32(254, enc(253.5f, 1, 0, 1, 0, true));
    TEST_ASSERT_EQUAL_UINT32(65534, enc(65535.0f, 1, 0, 2, 0, true));
    TEST_ASSERT_EQUAL_UINT32(65534, enc(1e30f, 1, 0, 2, 0, true));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFEu, enc(1e30f, 1, 0, 4, 0, true));
    TEST_ASSERT_EQUAL_UINT32(0, enc(-3.0f, 1, 0, 2, 0, true));
    // Not asked: all-ones is reachable
    TEST_ASSERT_EQUAL_UINT32(65535, enc(65535.0f, 1, 0, 2, 0, false));
    // No finite input yields all-ones when asked
    for (int i = -10; i < 70000; i += 7) {
        TEST_ASSERT_TRUE(enc((float)i, 1, 0, 2, 0, true) != 0xFFFFu);
    }
    uint8_t o[4];
    fieldAllOnes(1, o, 0);                         TEST_ASSERT_EQUAL_HEX8(0xFF, o[0]);
    fieldAllOnes(2, o, 0);                         TEST_ASSERT_EQUAL_HEX8(0xFF, o[0]); TEST_ASSERT_EQUAL_HEX8(0xFF, o[1]);
    fieldAllOnes(4, o, FIELD_LITTLE_ENDIAN);       TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, be(o, 4));
}

static void test_signed(void) {
    uint8_t o[4];
    TEST_ASSERT_EQUAL_UINT8(2, encodeScaledField(-1.0f, 1, 0, 2, FIELD_SIGNED, false, o));
    TEST_ASSERT_EQUAL_HEX8(0xFF, o[0]); TEST_ASSERT_EQUAL_HEX8(0xFF, o[1]);
    TEST_ASSERT_EQUAL_UINT8(2, encodeScaledField(-2.0f, 0.5f, 0, 2, FIELD_SIGNED | FIELD_LITTLE_ENDIAN, false, o));   // -4 = 0xFFFC
    TEST_ASSERT_EQUAL_HEX8(0xFC, o[0]); TEST_ASSERT_EQUAL_HEX8(0xFF, o[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7F, enc(127.0f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x7F, enc(1e9f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x80, enc(-128.0f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x80, enc(-1e9f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x8000, enc(-1e30f, 1, 0, 2, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x7FFF, enc(1e30f, 1, 0, 2, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, enc(-1e30f, 1, 0, 4, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFu, enc(1e30f, 1, 0, 4, FIELD_SIGNED, false));
    // rounding: half up, also for negatives (-1.5 -> -1, -1.6 -> -2)
    TEST_ASSERT_EQUAL_UINT32(0xFE, enc(-1.6f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0xFF, enc(-1.5f, 1, 0, 1, FIELD_SIGNED, false));
    TEST_ASSERT_EQUAL_UINT32(0xFFFF, enc(-0.5f, 1, 1, 2, FIELD_SIGNED | FIELD_LITTLE_ENDIAN, false));   // offset: -1.5 -> -1
    TEST_ASSERT_EQUAL_UINT32(0x00, enc(-0.4999f, 1, 0, 1, FIELD_SIGNED, false));
    // reserve: the N/A pattern is the max positive, so +max real values stop at max-1;
    // -1 (all ones) stays a legal reading
    TEST_ASSERT_EQUAL_UINT32(0x7E, enc(127.0f, 1, 0, 1, FIELD_SIGNED, true));
    TEST_ASSERT_EQUAL_UINT32(0x7FFE, enc(1e30f, 1, 0, 2, FIELD_SIGNED, true));
    TEST_ASSERT_EQUAL_UINT32(0xFFFF, enc(-1.0f, 1, 0, 2, FIELD_SIGNED, true));
    TEST_ASSERT_EQUAL_UINT32(0x8000, enc(-1e30f, 1, 0, 2, FIELD_SIGNED, true));
    for (int i = -40000; i < 40000; i += 3) {
        TEST_ASSERT_TRUE(enc((float)i, 1, 0, 2, FIELD_SIGNED, true) != 0x7FFFu);
    }
    fieldAllOnes(1, o, FIELD_SIGNED);                  TEST_ASSERT_EQUAL_HEX8(0x7F, o[0]);
    fieldAllOnes(2, o, FIELD_SIGNED);                  TEST_ASSERT_EQUAL_HEX8(0x7F, o[0]); TEST_ASSERT_EQUAL_HEX8(0xFF, o[1]);
    fieldAllOnes(2, o, FIELD_SIGNED | FIELD_LITTLE_ENDIAN);
    TEST_ASSERT_EQUAL_HEX8(0xFF, o[0]); TEST_ASSERT_EQUAL_HEX8(0x7F, o[1]);
    fieldAllOnes(4, o, FIELD_SIGNED);                  TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFu, be(o, 4));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unsigned_basic_and_endian);
    RUN_TEST(test_unsigned_rounding_and_clamp);
    RUN_TEST(test_non_finite_and_bad_length);
    RUN_TEST(test_reserve_all_ones);
    RUN_TEST(test_signed);
    return UNITY_END();
}
