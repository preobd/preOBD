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

// How many heartbeats a slot keeps taking part in AFTER its Action is deleted.
// The clearing frame (actionId = NONE) is edge-triggered and this transport has
// no end-to-end ACK — sendFrame() reports only LOCAL flow refusal, so a frame
// dropped downstream (the nRF's per-destination stash, a notify a central never
// accepts) is invisible here. Repeating it over a bounded window keeps the
// self-healing the unscoped heartbeat used to provide, without paying for
// MAX_ACTIONS empty slots every 5 s (#365).
constexpr uint8_t kClearHeartbeats = 3;

// The window is spent per clearing frame the transport ACCEPTS (see sendSlot),
// never per heartbeat edge: on a busy link, counting edges burned the window on
// refused sends and the repeats never arrived (#349). The edge-triggered push
// costs one, leaving kClearHeartbeats heartbeat repeats.
constexpr uint8_t kClearFramesTotal = kClearHeartbeats + 1;

bool    g_wasOccupied[MAX_ACTIONS];
uint8_t g_clearGrace[MAX_ACTIONS];

// The frame an empty slot packs to. Single-sourced so init()'s prime() key is
// byte-identical to what packState() produces for an unconfigured slot — if the
// two drifted, every empty slot would emit once at cold start.
PreobdActionState emptyState() {
    PreobdActionState s;
    memset(&s, 0, sizeof(s));
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    s.actionId = PREOBD_ACTION_ID_NONE;   // empty slot
    return s;
}

// Occupancy: the sentinel packState() already keys the empty frame on.
bool slotOccupied(int i) { return actions[i].id != CONTROL_ID_NONE; }

// Heartbeat membership: configured slots, plus any slot still inside its
// post-delete grace window so the clearing frame gets more than one chance.
// Change detection never consults this — a slot it excludes still emits the
// moment its key changes — so nothing here can make a transition invisible.
bool inHeartbeat(int i) { return slotOccupied(i) || g_clearGrace[i] > 0; }

void packState(int i, PreobdActionState& s) {
    const Action& a = actions[i];
    if (a.id == CONTROL_ID_NONE) {
        s = emptyState();                     // empty slot: nothing else to say
        return;
    }
    memset(&s, 0, sizeof(s));
    s.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
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

bool sendSlot(int i, uint16_t seq) {
    PreobdActionState snap;
    packState(i, snap);
    snap.sequence = seq;
    const bool sent = hwSerial1.sendFrame(FRAME_TYPE_RELAY,  // 0x05 (legacy name; carries Action state)
                                          reinterpret_cast<const uint8_t*>(&snap),
                                          sizeof(snap));
    // Spend the grace window on clearing frames the transport ACCEPTED, never on
    // heartbeat edges — see kClearFramesTotal (#349).
    if (sent && !slotOccupied(i) && g_clearGrace[i] > 0) --g_clearGrace[i];
    return sent;
}

} // namespace

void init() {
    // Cold start: seed every slot with the EMPTY frame rather than reset()ing to
    // "nothing cached". A configured slot's key differs from it, so it still
    // emits once on the first tick; an unconfigured slot does not, so the
    // catch-up burst scales with the Action count instead of MAX_ACTIONS. A slot
    // deleted LATER still pushes its clearing frame, because its cached key is
    // then the occupied one.
    g_bc.prime(emptyState());
    memset(g_clearGrace, 0, sizeof(g_clearGrace));
    // Seed occupancy as "nothing occupied" rather than from actions[]: init()
    // runs BEFORE the control tables load (main.cpp:309 brings the broadcasters
    // up next to the transports; initInputManager() only runs at line 364), so
    // the table is still BSS-zeroed here and every slot reads id == 0 — which
    // slotOccupied() cannot tell from a real Action with id 0. Snapshotting that
    // would make the first tick see MAX_ACTIONS - n occupied->deleted edges and
    // arm the grace window for every slot that never held an Action, putting all
    // of them back in the heartbeat (#352, the live-value twin of this bug).
    //
    // Seeding empty is safe in both orders: the first tick records real
    // occupancy and can only arm on a LATER delete, and a slot occupied at this
    // point has nothing to clear — no client can hold a tile for it before the
    // first loop() pass.
    memset(g_wasOccupied, 0, sizeof(g_wasOccupied));
}

void tick() {
    // Arm the grace window on the occupied->deleted edge, before the tick that
    // emits the clearing frame. sendSlot() spends it, per accepted frame, so a
    // starved or refused heartbeat costs the slot nothing.
    for (int i = 0; i < MAX_ACTIONS; ++i) {
        const bool occ = slotOccupied(i);
        if (!occ && g_wasOccupied[i]) g_clearGrace[i] = kClearFramesTotal;
        g_wasOccupied[i] = occ;
    }

    // heartbeatFor = inHeartbeat: the periodic re-send covers the configured
    // Actions only. Unscoped, it swept all MAX_ACTIONS slots every 5 s — 59
    // empty frames per cycle on a 5-action board, ~92% of ~1344 B carrying
    // nothing and competing with live values for the same TX budget (#365).
    // Invisible until the sweep became resumable (#350): before that the burst
    // died after three frames and the empty slots were never transmitted.
    g_bc.tick(millis(), kHeartbeatMs,
        [](int i) { PreobdActionState s; packState(i, s); return s; },
        [](const PreobdActionState& a, const PreobdActionState& b) {
            return sameIgnoringSequence(a, b);
        },
        sendSlot, inHeartbeat);
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
    // Configured slots only, for the same reason the heartbeat skips the rest: a
    // resyncing client is rebuilding its tiles from scratch and an empty slot has
    // no tile to build. Empty slots keep their cached empty key, so a later
    // occupied->deleted transition still emits.
    for (int i = 0; i < MAX_ACTIONS; ++i) {
        if (inHeartbeat(i)) g_bc.invalidate(i);
    }
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
