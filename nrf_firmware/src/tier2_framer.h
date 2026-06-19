// tier2_framer.h -- Tier 2 frame codec.
//
// Frame layout (ARCHITECTURE.md §3.3):
//   [0xAA] [TYPE u8] [LEN u16_le] [PAYLOAD 0..512] [CRC8]
//
// CRC8 covers TYPE + LEN + PAYLOAD. Variant: Dallas/Maxim
//   poly 0x31, reflected (== 0x8C in lookup-table form)
//   init 0x00, no final xor.
// This is locked here for Phase 2 and must match the host-side MuxTransport
// when it lands.

#ifndef TIER2_FRAMER_H
#define TIER2_FRAMER_H

#include <stdint.h>
#include <stddef.h>

constexpr uint8_t  TIER2_SYNC          = 0xAA;
constexpr size_t   TIER2_MAX_PAYLOAD   = 512;
constexpr size_t   TIER2_HEADER_BYTES  = 4;   // SYNC + TYPE + LEN(2)
constexpr size_t   TIER2_MAX_FRAME     = TIER2_HEADER_BYTES + TIER2_MAX_PAYLOAD + 1;

// Type codes per ARCHITECTURE.md §3.4. Phase 2 only handles 0x01/0x02/0xFF;
// the rest are listed for documentation.
constexpr uint8_t  TIER2_TYPE_TEXT_CMD  = 0x01;
constexpr uint8_t  TIER2_TYPE_TEXT_RESP = 0x02;
constexpr uint8_t  TIER2_TYPE_STATUS    = 0x03;
constexpr uint8_t  TIER2_TYPE_ALARM     = 0x04;
constexpr uint8_t  TIER2_TYPE_RELAY     = 0x05;
constexpr uint8_t  TIER2_TYPE_CONN_EVT  = 0x10;
constexpr uint8_t  TIER2_TYPE_OTA_DATA  = 0x20;
constexpr uint8_t  TIER2_TYPE_OTA_ACK   = 0x21;
constexpr uint8_t  TIER2_TYPE_ELM327    = 0x30;
constexpr uint8_t  TIER2_TYPE_PING      = 0xFF;

// CRC8 Dallas/Maxim across `len` bytes. Exposed for tests; the encoder /
// decoder use it internally.
uint8_t tier2Crc8(const uint8_t* data, size_t len);

// Encode a frame into `out`. Caller must provide at least
// TIER2_HEADER_BYTES + len + 1 bytes. Returns total bytes written, or 0 if
// len > TIER2_MAX_PAYLOAD.
size_t tier2Encode(uint8_t type, const uint8_t* payload, uint16_t len,
                   uint8_t* out);

// Streaming decoder result.
enum Tier2FeedResult : uint8_t {
    TIER2_NEED_MORE = 0,    // byte consumed, frame not yet complete
    TIER2_FRAME_READY,      // a complete, CRC-valid frame is in `out`
    TIER2_RESYNCING,        // bad sync / CRC; decoder is hunting for next 0xAA
};

struct Tier2Frame {
    uint8_t  type;
    uint16_t len;
    uint8_t  payload[TIER2_MAX_PAYLOAD];
};

// Feed one byte into the streaming decoder. When TIER2_FRAME_READY is
// returned, `out` holds a valid frame and the decoder resets for the next.
Tier2FeedResult tier2FeedByte(uint8_t b, Tier2Frame& out);

// Reset the decoder to its initial DISCONNECTED-equivalent state. Called on
// BLE disconnect so stale partial frames don't leak into the next session.
void tier2DecoderReset();

#endif // TIER2_FRAMER_H
