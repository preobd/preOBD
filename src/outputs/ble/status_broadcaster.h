// status_broadcaster.h -- Pushes 32-byte PreobdSystemStatus snapshots over
// the framed transport (type 0x03) on state change and on a 5s liveness
// heartbeat.
//
// Design notes (see TASK1_STATUS_BROADCASTER_DESIGN.md):
//   - tick() is called every loop iteration. Each call packs current state,
//     compares against the last-sent copy ignoring uptime, and sends when
//     either differs or the heartbeat interval has elapsed.
//   - A failed send (transport not in framed mode, backpressure, or flow
//     control asserted) leaves last_sent unchanged so the next tick retries
//     automatically. No queue, no blocking.
//   - The broadcaster targets hwSerial1 directly; revisit if a second framed
//     port is ever introduced.

#ifndef STATUS_BROADCASTER_H
#define STATUS_BROADCASTER_H

namespace StatusBroadcaster {

void init();
void tick();

} // namespace StatusBroadcaster

#endif // STATUS_BROADCASTER_H
