#include "frame_codec.h"

#include <string.h>

namespace {

// Decoder phases.
enum Phase : uint8_t {
    PHASE_SYNC = 0,
    PHASE_TYPE,
    PHASE_LEN_LO,
    PHASE_LEN_HI,
    PHASE_PAYLOAD,
    PHASE_CRC,
};

Phase    g_phase      = PHASE_SYNC;
uint8_t  g_type       = 0;
uint16_t g_len        = 0;
uint16_t g_payloadIdx = 0;
uint8_t  g_runningCrc = 0;
uint8_t  g_payload[FRAME_MAX_PAYLOAD];

inline uint8_t crc8Step(uint8_t crc, uint8_t b) {
    // Dallas/Maxim: poly 0x31, reflected -> 0x8C in lookup form.
    crc ^= b;
    for (uint8_t i = 0; i < 8; ++i) {
        crc = (crc & 0x01) ? static_cast<uint8_t>((crc >> 1) ^ 0x8C)
                           : static_cast<uint8_t>(crc >> 1);
    }
    return crc;
}

void resetDecoder() {
    g_phase      = PHASE_SYNC;
    g_type       = 0;
    g_len        = 0;
    g_payloadIdx = 0;
    g_runningCrc = 0;
}

} // namespace

uint8_t frameCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = crc8Step(crc, data[i]);
    }
    return crc;
}

size_t frameEncode(uint8_t type, const uint8_t* payload, uint16_t len,
                   uint8_t* out) {
    if (len > FRAME_MAX_PAYLOAD) return 0;
    size_t i = 0;
    out[i++] = FRAME_SYNC;
    out[i++] = type;
    out[i++] = static_cast<uint8_t>(len & 0xFF);
    out[i++] = static_cast<uint8_t>((len >> 8) & 0xFF);

    uint8_t crc = 0;
    crc = crc8Step(crc, type);
    crc = crc8Step(crc, static_cast<uint8_t>(len & 0xFF));
    crc = crc8Step(crc, static_cast<uint8_t>((len >> 8) & 0xFF));
    for (uint16_t j = 0; j < len; ++j) {
        out[i++] = payload[j];
        crc = crc8Step(crc, payload[j]);
    }
    out[i++] = crc;
    return i;
}

FrameFeedResult frameFeedByte(uint8_t b, Frame& out) {
    switch (g_phase) {
    case PHASE_SYNC:
        if (b == FRAME_SYNC) {
            g_phase = PHASE_TYPE;
            g_runningCrc = 0;
            return FRAME_NEED_MORE;
        }
        return FRAME_RESYNCING;

    case PHASE_TYPE:
        g_type = b;
        g_runningCrc = crc8Step(g_runningCrc, b);
        g_phase = PHASE_LEN_LO;
        return FRAME_NEED_MORE;

    case PHASE_LEN_LO:
        g_len = b;
        g_runningCrc = crc8Step(g_runningCrc, b);
        g_phase = PHASE_LEN_HI;
        return FRAME_NEED_MORE;

    case PHASE_LEN_HI:
        g_len |= static_cast<uint16_t>(b) << 8;
        g_runningCrc = crc8Step(g_runningCrc, b);
        if (g_len > FRAME_MAX_PAYLOAD) {
            resetDecoder();
            return FRAME_RESYNCING;
        }
        g_payloadIdx = 0;
        g_phase = (g_len == 0) ? PHASE_CRC : PHASE_PAYLOAD;
        return FRAME_NEED_MORE;

    case PHASE_PAYLOAD:
        g_payload[g_payloadIdx++] = b;
        g_runningCrc = crc8Step(g_runningCrc, b);
        if (g_payloadIdx >= g_len) {
            g_phase = PHASE_CRC;
        }
        return FRAME_NEED_MORE;

    case PHASE_CRC:
        if (b == g_runningCrc) {
            out.type = g_type;
            out.len  = g_len;
            if (g_len > 0) {
                memcpy(out.payload, g_payload, g_len);
            }
            resetDecoder();
            return FRAME_READY;
        }
        resetDecoder();
        return FRAME_RESYNCING;
    }
    resetDecoder();
    return FRAME_RESYNCING;
}

void frameDecoderReset() {
    resetDecoder();
}
