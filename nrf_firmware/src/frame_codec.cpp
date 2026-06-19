#include "frame_codec.h"

#include <string.h>

namespace {

// Decoder phases stored opaquely in FrameDecoderState::phase (uint8_t).
enum Phase : uint8_t {
    PHASE_SYNC = 0,
    PHASE_TYPE,
    PHASE_LEN_LO,
    PHASE_LEN_HI,
    PHASE_PAYLOAD,
    PHASE_CRC,
};

inline uint8_t crc8Step(uint8_t crc, uint8_t b) {
    // Dallas/Maxim: poly 0x31, reflected -> 0x8C in lookup form.
    crc ^= b;
    for (uint8_t i = 0; i < 8; ++i) {
        crc = (crc & 0x01) ? static_cast<uint8_t>((crc >> 1) ^ 0x8C)
                           : static_cast<uint8_t>(crc >> 1);
    }
    return crc;
}

inline void clearState(FrameDecoderState& s) {
    s.phase      = PHASE_SYNC;
    s.type       = 0;
    s.len        = 0;
    s.payloadIdx = 0;
    s.runningCrc = 0;
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

FrameFeedResult frameFeedByte(FrameDecoderState& s, uint8_t b, Frame& out) {
    switch (s.phase) {
    case PHASE_SYNC:
        if (b == FRAME_SYNC) {
            s.phase = PHASE_TYPE;
            s.runningCrc = 0;
            return FRAME_NEED_MORE;
        }
        return FRAME_RESYNCING;

    case PHASE_TYPE:
        s.type = b;
        s.runningCrc = crc8Step(s.runningCrc, b);
        s.phase = PHASE_LEN_LO;
        return FRAME_NEED_MORE;

    case PHASE_LEN_LO:
        s.len = b;
        s.runningCrc = crc8Step(s.runningCrc, b);
        s.phase = PHASE_LEN_HI;
        return FRAME_NEED_MORE;

    case PHASE_LEN_HI:
        s.len |= static_cast<uint16_t>(b) << 8;
        s.runningCrc = crc8Step(s.runningCrc, b);
        if (s.len > FRAME_MAX_PAYLOAD) {
            clearState(s);
            return FRAME_RESYNCING;
        }
        s.payloadIdx = 0;
        s.phase = (s.len == 0) ? PHASE_CRC : PHASE_PAYLOAD;
        return FRAME_NEED_MORE;

    case PHASE_PAYLOAD:
        s.payload[s.payloadIdx++] = b;
        s.runningCrc = crc8Step(s.runningCrc, b);
        if (s.payloadIdx >= s.len) {
            s.phase = PHASE_CRC;
        }
        return FRAME_NEED_MORE;

    case PHASE_CRC:
        if (b == s.runningCrc) {
            out.type = s.type;
            out.len  = s.len;
            if (s.len > 0) {
                memcpy(out.payload, s.payload, s.len);
            }
            clearState(s);
            return FRAME_READY;
        }
        clearState(s);
        return FRAME_RESYNCING;
    }
    clearState(s);
    return FRAME_RESYNCING;
}

void frameDecoderReset(FrameDecoderState& s) {
    clearState(s);
}
