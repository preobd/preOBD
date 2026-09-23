// frame_codec.h -- Framed-mode wire codec for the host-MCU UART link.
//
// Frame layout (ARCHITECTURE.md §3.3, historically "Tier 2"):
//   [0xAA] [TYPE u8] [LEN u16_le] [PAYLOAD 0..512] [CRC8]
//
// CRC8 covers TYPE + LEN + PAYLOAD. Variant: Dallas/Maxim
//   poly 0x31, reflected (== 0x8C in lookup-table form)
//   init 0x00, no final xor.
// Locked here and must match the nRF firmware's frame_codec.

#ifndef FRAME_CODEC_H
#define FRAME_CODEC_H

#include <stdint.h>
#include <stddef.h>

constexpr uint8_t  FRAME_SYNC          = 0xAA;
constexpr size_t   FRAME_MAX_PAYLOAD   = 512;
constexpr size_t   FRAME_HEADER_BYTES  = 4;   // SYNC + TYPE + LEN(2)
constexpr size_t   FRAME_MAX_BYTES     = FRAME_HEADER_BYTES + FRAME_MAX_PAYLOAD + 1;

// Type codes per ARCHITECTURE.md §3.4. MVP handles 0x01/0x02/0xFF end to
// end; the rest are listed for documentation.
constexpr uint8_t  FRAME_TYPE_TEXT_CMD  = 0x01;
constexpr uint8_t  FRAME_TYPE_TEXT_RESP = 0x02;
constexpr uint8_t  FRAME_TYPE_STATUS    = 0x03;
constexpr uint8_t  FRAME_TYPE_ALARM     = 0x04;
constexpr uint8_t  FRAME_TYPE_RELAY     = 0x05;
constexpr uint8_t  FRAME_TYPE_LIVE      = 0x06;
constexpr uint8_t  FRAME_TYPE_CONN_EVT  = 0x10;
constexpr uint8_t  FRAME_TYPE_OTA_DATA  = 0x20;
constexpr uint8_t  FRAME_TYPE_OTA_ACK   = 0x21;
constexpr uint8_t  FRAME_TYPE_ELM327    = 0x30;
constexpr uint8_t  FRAME_TYPE_PING      = 0xFF;

// CRC8 Dallas/Maxim across `len` bytes. Pure helper.
uint8_t frameCrc8(const uint8_t* data, size_t len);

// Encode a frame into `out`. Caller must provide at least
// FRAME_HEADER_BYTES + len + 1 bytes. Returns total bytes written, or 0 if
// len > FRAME_MAX_PAYLOAD. Pure -- no state.
size_t frameEncode(uint8_t type, const uint8_t* payload, uint16_t len,
                   uint8_t* out);

// Streaming decoder result.
enum FrameFeedResult : uint8_t {
    FRAME_NEED_MORE = 0,    // byte consumed, frame not yet complete
    FRAME_READY,            // a complete, CRC-valid frame is in `out`
    FRAME_RESYNCING,        // bad sync / CRC; decoder is hunting for next 0xAA
};

struct Frame {
    uint8_t  type;
    uint16_t len;
    uint8_t  payload[FRAME_MAX_PAYLOAD];
};

// Per-decoder state. Default-constructed equals the post-reset state, so
// declaring one as a member is sufficient -- no explicit init needed.
// Keep one instance per logical input stream (e.g. per MuxTransport);
// sharing one across streams will trample partial-frame state.
struct FrameDecoderState {
    uint8_t  phase = 0;        // opaque, internal enum -- treat as scratch
    uint8_t  type = 0;
    uint16_t len = 0;
    uint16_t payloadIdx = 0;
    uint8_t  runningCrc = 0;
    uint8_t  payload[FRAME_MAX_PAYLOAD] = {};
};

// Feed one byte into the streaming decoder. When FRAME_READY is returned,
// `out` holds a valid frame and `state` resets for the next.
FrameFeedResult frameFeedByte(FrameDecoderState& state, uint8_t b, Frame& out);

// Reset the decoder to its initial state. Called on disconnect / mode
// transition so a partial frame can't leak into the next session.
void frameDecoderReset(FrameDecoderState& state);

// True while the decoder is inside a frame (has seen SYNC and is waiting for
// more of TYPE/LEN/PAYLOAD/CRC). The owner uses this with its own clock to
// abandon a frame whose remaining bytes never arrive: a byte lost from the
// LEN field on an overrun otherwise parks the decoder waiting for up to 513
// phantom bytes, and every real frame that lands in that window -- the nRF's
// one connect PING included -- is swallowed as payload (#449).
bool frameDecoderMidFrame(const FrameDecoderState& state);

#endif // FRAME_CODEC_H
