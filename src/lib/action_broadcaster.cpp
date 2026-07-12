#include "action_broadcaster.h"

#include "../config.h"
#include "../control/control_types.h"  // defines ENABLE_CONTROL_ENGINE

#if ENABLE_CONTROL_ENGINE && ENABLE_FRAMED_TRANSPORT

#include <Arduino.h>
#include <string.h>

#include "ble_gatt_defs.h"
#include "frame_codec.h"
#include "system_config.h"
#include "transport_mux.h"
#include "slotted_broadcaster.h"
#include "../control/control_manager.h"

extern MuxTransport hwSerial1;

namespace ActionBroadcaster {

namespace {

// Re-send every slot this often regardless of change, so a late subscriber gets
// the full Action set (mirrors Status/AlarmBroadcaster heartbeat).
constexpr uint32_t kHeartbeatMs = 5000;

// Change key = the full payload; equality ignores the sequence stamp.
SlottedBroadcaster<PreobdActionState, MAX_ACTIONS> g_bc;

void packState(int i, PreobdActionState& s) {
    const Action& a = systemConfig.actions[i];
    memset(&s, 0, sizeof(s));
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    if (a.id == CONTROL_ID_NONE) {
        s.actionId = PREOBD_ACTION_ID_NONE;   // empty slot
        return;                               // leave the rest zeroed
    }
    s.actionId = a.id;
    s.type     = a.type;
    s.state    = controlActionState(i) ? PREOBD_ACTION_STATE_ON
                                       : PREOBD_ACTION_STATE_OFF;
    s.target   = a.target;
    s.ruleRef  = a.ruleRef;   // PREOBD_ACTION_ID_NONE (0xFF) == manual
    s.behavior = a.behavior;
    s.failsafe = a.failsafe;
    s.command  = PREOBD_ACTION_CMD_NONE;
    // sequence stamped at emit time
}

// Compare every field except sequence (which the broadcaster owns).
bool sameIgnoringSequence(const PreobdActionState& a, const PreobdActionState& b) {
    return a.protocolVersion == b.protocolVersion &&
           a.actionId == b.actionId && a.type == b.type &&
           a.state == b.state && a.target == b.target &&
           a.ruleRef == b.ruleRef && a.behavior == b.behavior &&
           a.failsafe == b.failsafe;
}

} // namespace

void init() {
    g_bc.reset();
}

void tick() {
    g_bc.tick(millis(), kHeartbeatMs,
        [](int i) { PreobdActionState s; packState(i, s); return s; },
        [](const PreobdActionState& a, const PreobdActionState& b) {
            return sameIgnoringSequence(a, b);
        },
        [](int i, uint16_t seq) {
            PreobdActionState snap;
            packState(i, snap);
            snap.sequence = seq;
            return hwSerial1.sendFrame(FRAME_TYPE_RELAY,  // 0x05 (legacy name; carries Action state)
                                       reinterpret_cast<const uint8_t*>(&snap),
                                       sizeof(snap));
        });
}

void handleInbound(const uint8_t* payload, uint16_t len) {
    if (!payload || len != PREOBD_ACTION_STATE_SIZE) return;
    PreobdActionState req;
    memcpy(&req, payload, sizeof(req));

    // The 0x05 frame id is shared with the retired relay wire format (same 16-byte
    // size). Gate on protocolVersion so a stale client's differently-laid-out
    // PreobdRelayState write isn't reinterpreted as an Action command.
    if (req.protocolVersion != PREOBD_BLE_PROTOCOL_VERSION) return;

    int slot = controlActionSlot(req.actionId);
    if (slot < 0) return;

    switch (req.command) {
        case PREOBD_ACTION_CMD_MANUAL_ON:
            controlActionSetManual(req.actionId, true);
            break;
        case PREOBD_ACTION_CMD_MANUAL_OFF:
            controlActionSetManual(req.actionId, false);
            break;
        default:
            return;  // no-op command: nothing to confirm
    }

    // Invalidate the cache so tick() emits a confirmation notify next loop.
    g_bc.invalidate(slot);
}

void resync() {
    g_bc.invalidateAll();
}

} // namespace ActionBroadcaster

#else // !(ENABLE_CONTROL_ENGINE && ENABLE_FRAMED_TRANSPORT)

namespace ActionBroadcaster {
void init() {}
void tick() {}
void handleInbound(const uint8_t*, uint16_t) {}
void resync() {}
}

#endif // ENABLE_CONTROL_ENGINE && ENABLE_FRAMED_TRANSPORT
