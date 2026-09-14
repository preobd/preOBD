// Native unit test: framed-mode wire codec (src/comms/frame_codec.cpp).
//
// frame_codec is fully pure (stdint/stddef/string.h only), so we compile the
// implementation straight into this translation unit — no stubs required.
// This suite is also the harness proof: if `pio test -e native -f
// test_frame_codec` is green, the native toolchain + Unity wiring works.
#include <unity.h>
#include <stdint.h>
#include <string.h>

#include "../../src/comms/frame_codec.cpp"

void setUp(void) {}
void tearDown(void) {}

// Encoder lays out [SYNC][TYPE][LEN_lo][LEN_hi][payload][CRC8], and the final
// byte must equal a fresh frameCrc8() over exactly TYPE+LEN+PAYLOAD.
static void test_encode_structure_and_crc(void) {
    const uint8_t payload[] = {0x10, 0x20, 0x30};
    uint8_t out[FRAME_MAX_BYTES];
    size_t n = frameEncode(FRAME_TYPE_STATUS, payload, sizeof(payload), out);

    TEST_ASSERT_EQUAL_UINT(FRAME_HEADER_BYTES + sizeof(payload) + 1, n);
    TEST_ASSERT_EQUAL_HEX8(FRAME_SYNC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_STATUS, out[1]);
    TEST_ASSERT_EQUAL_HEX8(sizeof(payload) & 0xFF, out[2]);  // LEN lo
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);                    // LEN hi
    uint8_t crc = frameCrc8(&out[1], n - 2);                 // TYPE+LEN+PAYLOAD
    TEST_ASSERT_EQUAL_HEX8(crc, out[n - 1]);
}

// Encode then stream every byte back through the decoder: type/len/payload
// must survive the round trip exactly.
static void test_round_trip(void) {
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t out[FRAME_MAX_BYTES];
    size_t n = frameEncode(FRAME_TYPE_ALARM, payload, sizeof(payload), out);

    FrameDecoderState st{};  // value-init: explicit known start state (the
    Frame f{};               // struct already default-inits phase=0, but be explicit)
    FrameFeedResult r = FRAME_NEED_MORE;
    for (size_t i = 0; i < n; ++i) r = frameFeedByte(st, out[i], f);

    TEST_ASSERT_EQUAL(FRAME_READY, r);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_ALARM, f.type);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), f.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof(payload));
}

// Zero-length payload (e.g. PING) is a valid frame.
static void test_empty_payload_round_trip(void) {
    uint8_t out[FRAME_MAX_BYTES];
    size_t n = frameEncode(FRAME_TYPE_PING, nullptr, 0, out);
    TEST_ASSERT_EQUAL_UINT(FRAME_HEADER_BYTES + 1, n);

    FrameDecoderState st{};  // value-init: explicit known start state (the
    Frame f{};               // struct already default-inits phase=0, but be explicit)
    FrameFeedResult r = FRAME_NEED_MORE;
    for (size_t i = 0; i < n; ++i) r = frameFeedByte(st, out[i], f);

    TEST_ASSERT_EQUAL(FRAME_READY, r);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_PING, f.type);
    TEST_ASSERT_EQUAL_UINT(0, f.len);
}

// A flipped CRC must be rejected (decoder hunts for the next sync).
static void test_corrupt_crc_resyncs(void) {
    const uint8_t payload[] = {0xDE, 0xAD};
    uint8_t out[FRAME_MAX_BYTES];
    size_t n = frameEncode(FRAME_TYPE_TEXT_CMD, payload, sizeof(payload), out);
    out[n - 1] ^= 0xFF;  // trash the CRC

    FrameDecoderState st{};  // value-init: explicit known start state (the
    Frame f{};               // struct already default-inits phase=0, but be explicit)
    FrameFeedResult r = FRAME_NEED_MORE;
    for (size_t i = 0; i < n; ++i) r = frameFeedByte(st, out[i], f);

    TEST_ASSERT_EQUAL(FRAME_RESYNCING, r);
}

// Payload larger than the protocol max is refused by the encoder.
static void test_oversize_payload_rejected(void) {
    static uint8_t big[FRAME_MAX_PAYLOAD + 1] = {0};
    uint8_t out[FRAME_MAX_BYTES + 8];
    size_t n = frameEncode(FRAME_TYPE_STATUS, big, FRAME_MAX_PAYLOAD + 1, out);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_structure_and_crc);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_empty_payload_round_trip);
    RUN_TEST(test_corrupt_crc_resyncs);
    RUN_TEST(test_oversize_payload_rejected);
    return UNITY_END();
}
