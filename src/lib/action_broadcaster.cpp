#include "action_broadcaster.h"

#include "../config.h"
#include "../control/control_types.h"  // defines INCLUDE_CONTROL_ENGINE

#if INCLUDE_CONTROL_ENGINE && INCLUDE_FRAMED_TRANSPORT

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
    const Action& a = actions[i];
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
    s.ruleRef   = a.ruleRef;   // PREOBD_ACTION_ID_NONE (0xFF) == manual
    s.ruleCount = controlActionRuleCount(i);  // >1 = OR-driven by several Rules (#343)
    s.behavior = a.behavior;
    s.failsafe = a.failsafe;
    s.command  = PREOBD_ACTION_CMD_NONE;
    // Operator override + whether it is being held against a faulted source (#329).
    switch (controlActionOverride(i)) {
        case ACT_OVR_FORCE_ON:  s.overrideFlags = PREOBD_ACTION_OVR_FORCE_ON;  break;
        case ACT_OVR_FORCE_OFF: s.overrideFlags = PREOBD_ACTION_OVR_FORCE_OFF; break;
        default:                s.overrideFlags = PREOBD_ACTION_OVR_AUTO;      break;
    }
    // One membership walk for both bits — this runs per slot per loop, and
    // tick() has no scan gate.
    const ActionDrive drive = controlActionDrive(i);
    if (drive.anyFaulted) s.overrideFlags |= PREOBD_ACTION_OVR_SOURCE_FAULTED;
    // Whether the automation is demanding ON right now, independent of whether an
    // override is currently winning (#348). sameIgnoringSequence() already covers
    // this because it compares the whole overrideFlags byte — the omission that
    // bit 2 (#329) and byte 12 (#343) each had to fix cannot recur for a bit
    // added to a byte already in the comparison.
    if (drive.anyActive)  s.overrideFlags |= PREOBD_ACTION_OVR_RULE_ACTIVE;
    // sequence stamped at emit time
}

// Compare every field except sequence (which the broadcaster owns).
bool sameIgnoringSequence(const PreobdActionState& a, const PreobdActionState& b) {
    return a.protocolVersion == b.protocolVersion &&
           a.actionId == b.actionId && a.type == b.type &&
           a.state == b.state && a.target == b.target &&
           a.ruleRef == b.ruleRef && a.behavior == b.behavior &&
           a.failsafe == b.failsafe &&
           a.overrideFlags == b.overrideFlags &&  // else an override/fault change never pushes
           a.ruleCount == b.ruleCount;           // …or a membership edit (#343)
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

    // A rule-driven action used to have its write dropped here silently — no state
    // change and no notify, so a client couldn't even detect the refusal (#329).
    // controlActionCommand() now routes it: manual latch when unlinked, operator
    // override when rule-driven. Same router the CLI uses, so the two can't diverge.
    switch (req.command) {
        case PREOBD_ACTION_CMD_MANUAL_ON:
            controlActionCommand(req.actionId, true);
            break;
        case PREOBD_ACTION_CMD_MANUAL_OFF:
            controlActionCommand(req.actionId, false);
            break;
        case PREOBD_ACTION_CMD_AUTO:
            controlActionSetOverride(req.actionId, ACT_OVR_AUTO);
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

#else // !(INCLUDE_CONTROL_ENGINE && INCLUDE_FRAMED_TRANSPORT)

namespace ActionBroadcaster {
void init() {}
void tick() {}
void handleInbound(const uint8_t*, uint16_t) {}
void resync() {}
}

#endif // INCLUDE_CONTROL_ENGINE && INCLUDE_FRAMED_TRANSPORT
