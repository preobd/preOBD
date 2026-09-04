// live_value_broadcaster.h -- Pushes 16-byte PreobdLiveValue frames over the
// framed transport (type 0x06) so a subscribed client (mobile app, webapp)
// renders live gauge tiles without polling the text CLI (ARCHITECTURE §8 Q5).
//
// Design notes (mirrors the Status/Alarm/Action broadcasters):
//   - tick() scans inputs[0..MAX_INPUTS) at a 500 ms gate (sensors update
//     ~20x faster; the gate caps frame rate), packs each slot into a compact
//     change key (display-converted value, flags, unitsIndex) and emits a
//     frame on change. A 5s heartbeat re-sends all slots so a late
//     subscriber gets the full set. Send-failure leaves the cache untouched
//     so the next scan retries.
//   - A non-finite reading (NAN fault, Inf overflow) never enters the change
//     key: packKey zeroes the value and clears VALID, so a faulted sensor
//     emits one !VALID transition instead of re-firing per scan.
//   - Value is DISPLAY units via convertFromBaseUnits(), matching the serial
//     CSV output; alarm frames stay BASE units (see ble_gatt_defs.h).
//   - Compiles to empty stubs unless INCLUDE_FRAMED_TRANSPORT is set.

#ifndef LIVE_VALUE_BROADCASTER_H
#define LIVE_VALUE_BROADCASTER_H

namespace LiveValueBroadcaster {

void init();
void tick();

// Called on (re)connect (FRAME_TYPE_PING) to force a full re-broadcast next tick.
void resync();

} // namespace LiveValueBroadcaster

#endif // LIVE_VALUE_BROADCASTER_H
