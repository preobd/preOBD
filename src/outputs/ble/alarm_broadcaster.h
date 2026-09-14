// alarm_broadcaster.h -- Pushes 19-byte PreobdAlarmEvent frames over the
// framed transport (type 0x04) whenever a configured input's alarm
// (severity, kind) tuple changes.
//
// Design notes:
//   - tick() is called every loop iteration. Walks inputs[0..numActiveInputs),
//     re-derives (severity, kind) per input, compares against per-input
//     cached values, and emits one frame per transition (including return-
//     to-normal, which the webapp uses to clear UI).
//   - A failed send (transport not framed, backpressure, flow control)
//     leaves the per-input cache untouched so the next tick retries
//     automatically. No queue, no blocking.
//   - Sequence number is global (not per-input) so a client can detect any
//     dropped notify across the link.
//   - The broadcaster targets hwSerial1 directly; revisit if a second framed
//     port is ever introduced.

#ifndef ALARM_BROADCASTER_H
#define ALARM_BROADCASTER_H

namespace AlarmBroadcaster {

void init();
void tick();

} // namespace AlarmBroadcaster

#endif // ALARM_BROADCASTER_H
