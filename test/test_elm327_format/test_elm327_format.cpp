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

// Rules from the STN1155 capture below: with spaces every line ends in a
// space; with headers the full 8-byte frame shows, zero padding included.
static void test_single_frame(void) {
    expect(ECT, 3, false, false, "410583\r\r>");
    expect(ECT, 3, false, true,  "41 05 83 \r\r>");
    expect(ECT, 3, true,  true,  "7E8 03 41 05 83 00 00 00 00 \r\r>");
    expect(ECT, 3, true,  false, "7E80341058300000000\r\r>");
}

static void test_multi_frame(void) {
    expect(BOOST, 12, false, true,  "00C \r0: 41 70 02 00 00 17 \r1: 3E 00 00 00 00 00 00 \r\r>");
    expect(BOOST, 12, false, false, "00C\r0:417002000017\r1:3E000000000000\r\r>");
    expect(BOOST, 12, true,  true,  "7E8 10 0C 41 70 02 00 00 17 \r7E8 21 3E 00 00 00 00 00 00 \r\r>");
    expect(BOOST, 12, true,  false, "7E8100C417002000017\r7E8213E000000000000\r\r>");
}

// Verbatim from a genuine OBDLink LX (STN1155 v4.0.0) reading preOBD over CAN,
// 2026-10-02. Apps are written against real adapters, so the emulator must
// print exactly this. If one of these fails, the emulator drifted from the
// chip, not the other way round.
static void test_matches_stn1155_capture(void) {
    static const uint8_t PIDS00[] = {0x41, 0x00, 0x80, 0x00, 0x00, 0x01};
    static const uint8_t DTCS[]   = {0x43, 0x00};
    static const uint8_t BOOST38[] = {0x41, 0x70, 0x02, 0x00, 0x00, 0x17, 0x38, 0, 0, 0, 0, 0};
    static const uint8_t BOOST3A[] = {0x41, 0x70, 0x02, 0x00, 0x00, 0x17, 0x3A, 0, 0, 0, 0, 0};
    expect(PIDS00, 6, false, true, "41 00 80 00 00 01 \r\r>");                      // 0100
    expect(PIDS00, 6, true,  true, "7E8 06 41 00 80 00 00 01 00 \r\r>");            // ATH1 0100
    expect(DTCS,   2, false, true, "43 00 \r\r>");                                   // 03
    expect(DTCS,   2, true,  true, "7E8 02 43 00 00 00 00 00 00 \r\r>");            // ATH1 03
    expect(BOOST38, 12, false, true,
           "00C \r0: 41 70 02 00 00 17 \r1: 38 00 00 00 00 00 00 \r\r>");           // 0170
    expect(BOOST3A, 12, true, true,
           "7E8 10 0C 41 70 02 00 00 17 \r7E8 21 3A 00 00 00 00 00 00 \r\r>");      // ATH1 0170
    expect(BOOST38, 12, false, false, "00C\r0:417002000017\r1:38000000000000\r\r>"); // ATS0 0170
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_frame);
    RUN_TEST(test_matches_stn1155_capture);
    RUN_TEST(test_multi_frame);
    return UNITY_END();
}
