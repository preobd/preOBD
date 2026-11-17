// Native unit test: RealDash over the nRF's BLE data channel (#612 phase 2c).
// What reaches MuxTransport::sendFrame is checked byte for byte:
//   - frame type 0x31 (FRAME_TYPE_HM10_STREAM), never 0x30 or text;
//   - payloads of whole 16-byte RealDash frames, at most 3 per frame (the
//     64-byte Teensy 4 UART ring, #528), NUL bytes intact;
//   - the frames concatenate to exactly what the encoder wrote;
//   - a refused sendFrame sends no partial frame and resumes next tick;
//   - a pass started while the last one is still leaving is skipped, so
//     nothing stale queues behind a slow phone.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/outputs/formats/realdash_framed.cpp"

static uint8_t  g_cap[2048];
static size_t   g_capLen = 0;
static int      g_frames = 0;
static bool    g_onlyStream = true;   // every frame was type 0x31
static uint16_t g_maxLen = 0;
static bool     g_badUnit = false;
static int      g_sendOkFor = 1 << 30;   // frames accepted before refusing

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (g_sendOkFor <= 0) return false;
    g_sendOkFor--;
    if (type != FRAME_TYPE_HM10_STREAM) g_onlyStream = false;
    if (len > g_maxLen) g_maxLen = len;
    if (len % 16 != 0) g_badUnit = true;
    memcpy(g_cap + g_capLen, payload, len);
    g_capLen += len;
    g_frames++;
    return true;
}
void MuxTransport::setFramedEnabled(bool e) { framedEnabled = e; }
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

// n RealDash-shaped units; unit i is 44 33 22 11, then i and NULs
static void writeUnits(Print& p, int n, uint8_t* expect) {
    for (int i = 0; i < n; i++) {
        uint8_t u[16] = {0x44, 0x33, 0x22, 0x11, 0x80, 0x0C, 0x00, 0x00, (uint8_t)i, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        p.write(u, 16);
        memcpy(expect + i * 16, u, 16);
    }
}

void setUp(void) {
    g_capLen = 0; g_frames = 0; g_onlyStream = true; g_maxLen = 0; g_badUnit = false; g_sendOkFor = 1 << 30;
    RealdashFramed::stop();
    RealdashFramed::start();
}
void tearDown(void) {}

static void test_a_pass_goes_out_as_0x31_frames_of_whole_units(void) {
    static uint8_t expect[7 * 16];
    Print* p = RealdashFramed::beginPass();
    TEST_ASSERT_NOT_NULL(p);
    writeUnits(*p, 7, expect);
    RealdashFramed::tick();
    TEST_ASSERT_TRUE(g_onlyStream);                              // every frame 0x31, never 0x30 or text
    TEST_ASSERT_EQUAL_INT(3, g_frames);                          // 3 + 3 + 1 units
    TEST_ASSERT_EQUAL_UINT16(48, g_maxLen);
    TEST_ASSERT_FALSE(g_badUnit);
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), g_capLen);
    TEST_ASSERT_EQUAL_MEMORY(expect, g_cap, sizeof(expect));     // NULs intact, byte for byte
}

static void test_a_refused_send_resumes_next_tick_without_splitting_a_frame(void) {
    static uint8_t expect[5 * 16];
    Print* p = RealdashFramed::beginPass();
    writeUnits(*p, 5, expect);
    g_sendOkFor = 1;                       // the second frame is refused (flow control)
    RealdashFramed::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frames);
    TEST_ASSERT_EQUAL_size_t(48, g_capLen);
    TEST_ASSERT_NULL(RealdashFramed::beginPass());   // still leaving: the next pass is skipped
    TEST_ASSERT_EQUAL_UINT32(1, RealdashFramed::skippedPasses());
    g_sendOkFor = 1 << 30;
    RealdashFramed::tick();
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), g_capLen);
    TEST_ASSERT_FALSE(g_badUnit);
    TEST_ASSERT_EQUAL_MEMORY(expect, g_cap, sizeof(expect));
    TEST_ASSERT_NOT_NULL(RealdashFramed::beginPass());   // gone: a new pass may start
}

static void test_a_short_tail_is_never_sent(void) {
    Print* p = RealdashFramed::beginPass();
    static uint8_t expect[16];
    writeUnits(*p, 1, expect);
    p->write((const uint8_t*)"abc", 3);    // not a whole unit
    RealdashFramed::tick();
    TEST_ASSERT_EQUAL_size_t(16, g_capLen);
    TEST_ASSERT_NOT_NULL(RealdashFramed::beginPass());
}

static void test_stopped_it_sends_nothing(void) {
    RealdashFramed::stop();
    TEST_ASSERT_NULL(RealdashFramed::beginPass());
    RealdashFramed::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frames);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_pass_goes_out_as_0x31_frames_of_whole_units);
    RUN_TEST(test_a_refused_send_resumes_next_tick_without_splitting_a_frame);
    RUN_TEST(test_a_short_tail_is_never_sent);
    RUN_TEST(test_stopped_it_sends_nothing);
    return UNITY_END();
}
