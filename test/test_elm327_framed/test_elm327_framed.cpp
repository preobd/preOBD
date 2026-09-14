// Native unit test: ELM327 routing over framed type 0x30
// (src/comms/elm327_framed.cpp + the real src/outputs/output_elm327.cpp parser).
//
// Feeds ELM327 command bytes as if they arrived in a 0x30 frame and captures
// the bytes the module hands back to MuxTransport::sendFrame -- the same
// protocol-correctness approach as test_broadcaster, end to end through the
// real AT/OBD parser. The bridge Stream (feed->read, write->capture, multi-
// call accumulation, drain) is exercised transitively by every case.
//
// What we fake:
//   - obdQuery_resolve: canned PID data, so obd_query.cpp (and its inputs/
//     calibration deps) need not be compiled. The parser path is real.
//   - MuxTransport::sendFrame: captures the emitted 0x30 frame bytes. The real
//     transport_mux.cpp is not compiled.
// Unused free functions in output_elm327.cpp (initELM327 etc., which pull
// msg/systemConfig) are dropped by --gc-sections in [env:native].

#include <unity.h>
#include <Arduino.h>
#include <string.h>

// ---- canned OBD layer (real obd_query.cpp deliberately not compiled) ----
#include "../../src/outputs/obd_query.h"
bool obdQuery_resolve(uint8_t mode, uint8_t pid, uint8_t* out, uint8_t* outLen) {
    if (mode != 0x01) return false;
    if (pid == 0x00) {  // supported PIDs 01-20 bitmap
        static const uint8_t b[] = {0x41, 0x00, 0x18, 0x00, 0x00, 0x01};
        memcpy(out, b, sizeof(b)); *outLen = sizeof(b); return true;
    }
    if (pid == 0x0C) {  // engine RPM: ((0x1A*256)+0xF8)/4 = 1726 rpm
        static const uint8_t b[] = {0x41, 0x0C, 0x1A, 0xF8};
        memcpy(out, b, sizeof(b)); *outLen = sizeof(b); return true;
    }
    return false;
}

// ---- modules under test (output_elm327 first: defines ELM327Output) ----
#include "../../src/outputs/output_elm327.cpp"
#include "../../src/comms/elm327_framed.cpp"

// ---- globals the parser TU references ----
// Only the reachable parser path needs a backing symbol: the ATRV branch reads
// inputs[]. output_elm327.cpp's free functions (initELM327 etc.) reference
// msg/systemConfig/getSerialPort, but they're unreferenced here and dropped by
// the linker's dead-strip (see [env:native]), so no stubs for those are needed.
Input inputs[MAX_INPUTS];

// ---- capturing transport ----
static uint8_t  g_cap[1024];   // concatenated payloads of all 0x30 frames
static size_t   g_capLen   = 0;
static int      g_frameCnt  = 0;
static uint8_t  g_lastType  = 0;
static bool     g_sendOk    = true;  // toggle to simulate back-pressure

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (!g_sendOk) return false;
    g_lastType = type;
    if (g_capLen + len <= sizeof(g_cap)) {
        memcpy(g_cap + g_capLen, payload, len);
        g_capLen += len;
    }
    ++g_frameCnt;
    return true;
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}

static MuxTransport g_out(nullptr, "TEST", 115200);

void setUp(void) {
    g_capLen = 0;
    g_frameCnt = 0;
    g_lastType = 0;
    g_sendOk = true;
    memset(inputs, 0, sizeof(inputs));
    Elm327Framed::init();  // re-bind + reset parser session (echo on, etc.)
}
void tearDown(void) {}

// Feed a NUL-terminated command string as one 0x30 frame's payload.
static void feed(const char* cmd) {
    Elm327Framed::handleInbound(reinterpret_cast<const uint8_t*>(cmd),
                                static_cast<uint16_t>(strlen(cmd)), g_out);
}
static bool capContains(const char* needle) {
    g_cap[g_capLen < sizeof(g_cap) ? g_capLen : sizeof(g_cap) - 1] = '\0';
    return strstr(reinterpret_cast<char*>(g_cap), needle) != nullptr;
}

// ATZ returns the version banner, on a 0x30 frame.
static void test_atz_returns_version(void) {
    feed("ATZ\r");
    TEST_ASSERT_TRUE(g_frameCnt >= 1);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_ELM327, g_lastType);
    TEST_ASSERT_TRUE(capContains("ELM327 v1.5"));
}

// ATE0 acks and disables echo; a later command's text is not echoed back
// (verifies session state persists in the dedicated parser instance).
static void test_ate0_ok_and_suppresses_echo(void) {
    feed("ATE0\r");
    TEST_ASSERT_TRUE(capContains("OK"));

    g_capLen = 0; g_cap[0] = '\0';   // isolate the next command's output
    feed("0100\r");
    TEST_ASSERT_FALSE(capContains("0100"));     // echo suppressed
    TEST_ASSERT_TRUE(capContains("410018000001"));
}

// Mode 01 PID 00 -> supported-PID bitmap, hex-encoded (echo on by default).
static void test_obd_pid00_supported_bitmap(void) {
    feed("0100\r");
    TEST_ASSERT_TRUE(capContains("410018000001"));
}

// Mode 01 PID 0C -> RPM data bytes hex-encoded.
static void test_obd_pid0c_data(void) {
    feed("010C\r");
    TEST_ASSERT_TRUE(capContains("410C1AF8"));
}

// Unsupported PID -> "NO DATA".
static void test_unsupported_pid_no_data(void) {
    feed("0199\r");
    TEST_ASSERT_TRUE(capContains("NO DATA"));
}

// A command split across two 0x30 frames is accumulated by the parser (it acts
// only on CR) -- exercises the bridge feeding the same parser instance twice.
static void test_command_split_across_frames(void) {
    feed("01");      // no CR yet -> no response
    TEST_ASSERT_EQUAL_INT(0, g_frameCnt);
    feed("0C\r");    // completes "010C"
    TEST_ASSERT_TRUE(capContains("410C1AF8"));
}

// Back-pressure: if sendFrame refuses, the response is held and flushed on the
// next inbound call rather than lost.
static void test_backpressure_retries(void) {
    g_sendOk = false;
    feed("ATI\r");                       // response generated but cannot send
    TEST_ASSERT_EQUAL_INT(0, g_frameCnt);
    g_sendOk = true;
    feed("\r");                          // any later frame flushes the residual
    TEST_ASSERT_TRUE(capContains("ELM327 v1.5"));
}

// poll() drains a back-pressured response without waiting for the client's
// next request (#263: the request may have been pumped mid-frame during a
// bulk text response, with emission deferred to the next frame boundary).
static void test_poll_drains_backpressured_response(void) {
    g_sendOk = false;
    feed("ATI\r");                       // response generated but cannot send
    TEST_ASSERT_EQUAL_INT(0, g_frameCnt);
    g_sendOk = true;
    Elm327Framed::poll(g_out);           // no inbound traffic needed
    TEST_ASSERT_TRUE(g_frameCnt >= 1);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_ELM327, g_lastType);
    TEST_ASSERT_TRUE(capContains("ELM327 v1.5"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_atz_returns_version);
    RUN_TEST(test_ate0_ok_and_suppresses_echo);
    RUN_TEST(test_obd_pid00_supported_bitmap);
    RUN_TEST(test_obd_pid0c_data);
    RUN_TEST(test_unsupported_pid_no_data);
    RUN_TEST(test_command_split_across_frames);
    RUN_TEST(test_backpressure_retries);
    RUN_TEST(test_poll_drains_backpressured_response);
    return UNITY_END();
}
