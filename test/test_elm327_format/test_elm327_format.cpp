// Native unit test: ELM327 text of a Mode 01 answer (src/outputs/elm327_format.cpp, #516).
//
// 0x70 boost and 0x78 EGT answers are longer than one CAN frame; an ELM327
// client (Torque over BLE) expects them printed the way a real ELM327 prints
// an ISO-TP reply. Single-frame answers must print exactly as before.
#include <unity.h>
#include <string.h>

#include "../../src/outputs/elm327_format.cpp"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t ECT[] = {0x41, 0x05, 0x83};
static const uint8_t BOOST[] = {0x41, 0x70, 0x02, 0x00, 0x00, 0x17, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00};

static void expect(const uint8_t* b, uint8_t n, bool headers, bool spaces, const char* want) {
    char out[96];
    elmFormatAnswer(b, n, headers, spaces, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(want, out);
}

static void test_single_frame_unchanged(void) {
    expect(ECT, 3, false, false, "410583\r\r>");
    expect(ECT, 3, false, true,  "41 05 83\r\r>");
    expect(ECT, 3, true,  true,  "7E8 03 41 05 83\r\r>");
    expect(ECT, 3, true,  false, "7E803410583\r\r>");
}

static void test_multi_frame(void) {
    expect(BOOST, 12, false, true,  "00C\r0: 41 70 02 00 00 17\r1: 3E 00 00 00 00 00 00\r\r>");
    expect(BOOST, 12, false, false, "00C\r0:417002000017\r1:3E000000000000\r\r>");
    expect(BOOST, 12, true,  true,  "7E8 10 0C 41 70 02 00 00 17\r7E8 21 3E 00 00 00 00 00 00\r\r>");
    expect(BOOST, 12, true,  false, "7E8100C417002000017\r7E8213E000000000000\r\r>");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_frame_unchanged);
    RUN_TEST(test_multi_frame);
    return UNITY_END();
}
