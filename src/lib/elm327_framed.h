// elm327_framed.h -- Routes framed-transport type 0x30 (ELM327) frames
// through the existing ELM327 AT/OBD parser and emits the response as a
// 0x30 frame on the originating transport.
//
// This is the framed-mode counterpart to the serial-port ELM327 output
// module (output_elm327.cpp). It lets a concurrent BLE client (Torque, OBD
// Fusion, RealDash) stream ELM327 data over the multiplexed UART *alongside*
// the webapp's text channel, instead of monopolizing the link in transparent
// mode. See ARCHITECTURE.md §3.6 (ELM327Transport).
//
// Design:
//   - Reuses ELM327Output unchanged, driven through an in-memory bridge
//     Stream. The parser does all I/O via Stream*, so a 0x30 frame's payload
//     is fed to the bridge's read buffer and the parser's response is captured
//     from the bridge's write buffer -- no parser duplication.
//   - A dedicated parser instance (separate from the serial-bound singleton)
//     keeps per-channel session state (echo/headers/...) independent of any
//     serial-port ELM327 binding.
//   - The whole module compiles to empty stubs unless both ENABLE_FRAMED_TRANSPORT
//     and ENABLE_ELM327 are set (the framed transport is the channel these
//     frames ride on; the parser is the ELM327 emulator).

#ifndef ELM327_FRAMED_H
#define ELM327_FRAMED_H

#include <stdint.h>

class MuxTransport;  // forward decl; .cpp includes transport_mux.h for the full type

namespace Elm327Framed {

// Bind the dedicated parser instance to the bridge Stream and start it.
// Call once in setup() after obdQuery_buildLookupTable() -- the framed parser
// shares the same OBD lookup table as the CAN/serial paths.
void init();

// Called by MuxTransport::handleFrame() when a type 0x30 frame arrives. Runs
// the ELM327 parser synchronously on the payload and emits the response as
// one or more 0x30 frames back on `out`. Non-blocking: if `out.sendFrame`
// back-pressures, the unsent remainder is retried on the next 0x30 frame or
// by poll().
void handleInbound(const uint8_t* payload, uint16_t len, MuxTransport& out);

// Retry any response bytes a back-pressured sendFrame left queued (e.g. the
// request was pumped from inside a partially-written text frame, when
// emission defers to the next frame boundary -- see MuxTransport). Called
// from MuxTransport's frame-boundary and update paths; no-op when idle.
void poll(MuxTransport& out);

} // namespace Elm327Framed

#endif // ELM327_FRAMED_H
