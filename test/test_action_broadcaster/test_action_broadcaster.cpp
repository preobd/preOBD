// Native unit test: ActionBroadcaster (src/lib/action_broadcaster.cpp).
//
// Two directions exercised on the host: (a) tick() packs actions[]
// into 16-byte PreobdActionState frames and emits on change, and (b)
// handleInbound() applies a client manual-toggle command via the control
// manager API. We capture emitted bytes and record the manager calls; the real
// transport and GPIO are not linked.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/lib/action_broadcaster.cpp"

// ---- Globals the broadcaster TU expects from the rest of the firmware ----
SystemConfig systemConfig;
Action       actions[MAX_ACTIONS];   // standalone table (was systemConfig.actions[])

// controlActionState() — we own the driven-state truth here.
static bool g_actionState[MAX_ACTIONS];
bool controlActionState(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_actionState[slot] : false;
}

// controlActionSlot() — resolve id → slot against our config.
int controlActionSlot(uint8_t id) {
    if (id == CONTROL_ID_NONE) return -1;
    for (int i = 0; i < MAX_ACTIONS; ++i) {
        if (actions[i].id == id) return i;
    }
    return -1;
}

// Record manual-toggle calls made by handleInbound().
static int     g_setManualCalls = 0;
static uint8_t g_setManualId = 0;
static bool    g_setManualVal = false;
bool controlActionSetManual(uint8_t id, bool on) {
    ++g_setManualCalls; g_setManualId = id; g_setManualVal = on; return true;
}

// controlActionCommand() — the shared ON/OFF router (#329). Mirror the real
// routing so a rule-driven write lands on the override, not the manual latch.
static ActionOverride g_override[MAX_ACTIONS];
static int            g_commandCalls = 0;
static uint8_t        g_commandId = 0;
static bool           g_commandVal = false;
bool controlActionCommand(uint8_t id, bool on) {
    int slot = controlActionSlot(id);
    if (slot < 0) return false;
    ++g_commandCalls; g_commandId = id; g_commandVal = on;
    if (actions[slot].ruleRef == CONTROL_ID_NONE) {
        controlActionSetManual(id, on);
    } else {
        g_override[slot] = on ? ACT_OVR_FORCE_ON : ACT_OVR_FORCE_OFF;
    }
    return true;
}

static int g_setOverrideCalls = 0;
bool controlActionSetOverride(uint8_t id, ActionOverride ovr) {
    int slot = controlActionSlot(id);
    if (slot < 0) return false;
    ++g_setOverrideCalls; g_override[slot] = ovr; return true;
}
ActionOverride controlActionOverride(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_override[slot] : ACT_OVR_AUTO;
}

// controlActionRuleCount() — membership size (#343). The real manager counts the
// inline Action.ruleRef plus actionLinks[] rows; here the extra members are just
// a per-slot number, which is all the frame cares about.
static uint8_t g_extraMembers[MAX_ACTIONS];
uint8_t controlActionRuleCount(int slot) {
    if (slot < 0 || slot >= MAX_ACTIONS) return 0;
    if (actions[slot].id == CONTROL_ID_NONE || actions[slot].ruleRef == CONTROL_ID_NONE) return 0;
    return (uint8_t)(1 + g_extraMembers[slot]);
}

// controlActionDrive() — the single membership walk the frame reads both its
// fault and its demand bit from (#348). The real manager derives it from
// resolveActionDrive(); here both are set truth, which is all packing cares
// about. Counting the calls also pins the "one walk per slot" property.
static bool g_sourceFaulted[MAX_ACTIONS];
static bool g_ruleActive[MAX_ACTIONS];
static int  g_driveCalls = 0;
ActionDrive controlActionDrive(int slot) {
    ActionDrive d{};
    if (slot < 0 || slot >= MAX_ACTIONS) return d;
    ++g_driveCalls;
    d.memberCount = controlActionRuleCount(slot);
    d.anyFaulted  = g_sourceFaulted[slot];
    d.anyActive   = g_ruleActive[slot];
    return d;
}

// ---- Capturing transport (records every frame this tick) ----
struct CapturedFrame { uint8_t type; uint16_t len; uint8_t payload[32]; };
static CapturedFrame g_frames[8];
static int           g_frameCount = 0;

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (g_frameCount < 8) {
        g_frames[g_frameCount].type = type;
        g_frames[g_frameCount].len  = len;
        if (len <= sizeof(g_frames[0].payload)) memcpy(g_frames[g_frameCount].payload, payload, len);
    }
    ++g_frameCount;
    return true;
}
size_t MuxTransport::write(uint8_t) { return 0; }
size_t MuxTransport::write(const uint8_t*, size_t) { return 0; }
int    MuxTransport::available() { return 0; }
int    MuxTransport::read() { return -1; }
int    MuxTransport::peek() { return -1; }
void   MuxTransport::flush() {}
void   MuxTransport::update() {}
MuxTransport hwSerial1(nullptr, "SERIAL1", 115200);

static const PreobdActionState& frameState(int i) {
    return *reinterpret_cast<const PreobdActionState*>(g_frames[i].payload);
}

void setUp(void) {
    g_frameCount = 0;
    g_test_now = 0;
    memset(g_frames, 0, sizeof(g_frames));
    memset(&systemConfig, 0, sizeof(systemConfig));
    memset(g_actionState, 0, sizeof(g_actionState));
    for (int i = 0; i < MAX_ACTIONS; ++i) actions[i].id = CONTROL_ID_NONE;  // all empty
    g_setManualCalls = 0;
    g_commandCalls = 0;
    g_setOverrideCalls = 0;
    memset(g_override, 0, sizeof(g_override));
    memset(g_sourceFaulted, 0, sizeof(g_sourceFaulted));
    memset(g_ruleActive, 0, sizeof(g_ruleActive));
    g_driveCalls = 0;
    memset(g_extraMembers, 0, sizeof(g_extraMembers));
    ActionBroadcaster::init();
}
void tearDown(void) {}

static void configureAction(int i, uint8_t id, uint8_t type, uint8_t target,
                            uint8_t ruleRef, uint8_t behavior, uint8_t failsafe) {
    actions[i].id       = id;
    actions[i].type     = type;
    actions[i].target   = target;
    actions[i].ruleRef  = ruleRef;
    actions[i].behavior = behavior;
    actions[i].failsafe = failsafe;
}

// First tick snapshots every slot (cache empty) → MAX_ACTIONS frames.
static void test_first_tick_snapshots_all_slots(void) {
    configureAction(0, /*id*/3, PREOBD_ACTION_TYPE_RELAY, /*target*/9,
                    /*ruleRef*/5, /*behavior*/0, PREOBD_ACTION_FAILSAFE_ON);
    g_actionState[0] = true;  // action 0 currently ON

    ActionBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(MAX_ACTIONS, g_frameCount);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_RELAY, g_frames[0].type);  // frame value 0x05
    TEST_ASSERT_EQUAL_UINT16(PREOBD_ACTION_STATE_SIZE, g_frames[0].len);

    const PreobdActionState& a0 = frameState(0);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, a0.protocolVersion);
    TEST_ASSERT_EQUAL_UINT8(3, a0.actionId);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_TYPE_RELAY, a0.type);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_STATE_ON, a0.state);
    TEST_ASSERT_EQUAL_UINT8(9, a0.target);
    TEST_ASSERT_EQUAL_UINT8(5, a0.ruleRef);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_FAILSAFE_ON, a0.failsafe);
    TEST_ASSERT_EQUAL_UINT8(1, a0.ruleCount);   // one member (#343)
    TEST_ASSERT_EQUAL_UINT16(0, a0.sequence);
}

// Empty slots are emitted with actionId == NONE so a client can render the set.
static void test_empty_slot_emits_none_id(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_ID_NONE, frameState(1).actionId);  // slot 1 empty
}

// No field changed → nothing re-emitted.
static void test_idempotent_after_snapshot(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, 5, 0, 0);
    ActionBroadcaster::tick();
    g_frameCount = 0;
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
}

// Flipping an action's driven state emits exactly that slot, state updated.
static void test_state_change_emits_one_frame(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, 5, 0, 0);
    g_actionState[0] = false;
    ActionBroadcaster::tick();
    g_frameCount = 0;

    g_actionState[0] = true;
    ActionBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    const PreobdActionState& a = frameState(0);
    TEST_ASSERT_EQUAL_UINT8(3, a.actionId);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_STATE_ON, a.state);
}

// Inbound MANUAL_ON command toggles the targeted action via the manager.
static void test_handle_inbound_manual_on(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_MANUAL_ON;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(1, g_setManualCalls);
    TEST_ASSERT_EQUAL_UINT8(7, g_setManualId);
    TEST_ASSERT_TRUE(g_setManualVal);
}

// A manual command targeting a RULE-DRIVEN action is now ACCEPTED and routed to
// an operator override (#329) — it used to be dropped silently at the wire
// boundary, with no state change and no notify for the client to detect. The
// manual latch must stay untouched: that separation is what keeps #294's stale
// latch from coming back when the rule is later unlinked.
static void test_handle_inbound_manual_on_rule_driven_sets_override(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_MANUAL_ON;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(1, g_commandCalls);
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);   // latch untouched
    TEST_ASSERT_EQUAL_INT(ACT_OVR_FORCE_ON, controlActionOverride(0));
}

// CMD_AUTO releases an override.
static void test_handle_inbound_auto_releases_override(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    g_override[0] = ACT_OVR_FORCE_ON;
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_AUTO;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(1, g_setOverrideCalls);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));
}

// CMD_AUTO on an unlinked action is harmless: it clears an override that was
// never set, and must not disturb the manual latch that actually drives the pin.
static void test_handle_inbound_auto_on_manual_action_is_harmless(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_AUTO;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);   // manual latch untouched
}

// An unknown/garbage command byte must not mutate any state.
static void test_handle_inbound_unknown_command_is_inert(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    g_override[0] = ACT_OVR_FORCE_ON;
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;
    req.actionId = 7;
    req.command  = 0x7F;                          // not a defined CMD_*
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_commandCalls);
    TEST_ASSERT_EQUAL_INT(0, g_setOverrideCalls);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_FORCE_ON, controlActionOverride(0));  // force survives
}

// The override state is reported in byte 11, and a change in it ALONE must push
// a fresh frame — otherwise the client never learns an override was applied.
static void test_override_flags_reported_and_push_on_change(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    ActionBroadcaster::tick();                       // snapshot everything
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_AUTO, frameState(0).overrideFlags);

    g_frameCount = 0;
    g_override[0] = ACT_OVR_FORCE_ON;
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_FORCE_ON, frameState(0).overrideFlags);

    // ...and so must a source going faulted underneath a held override.
    g_frameCount = 0;
    g_sourceFaulted[0] = true;
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_FORCE_ON | PREOBD_ACTION_OVR_SOURCE_FAULTED,
                            frameState(0).overrideFlags);
}

// Bit 3 reports the automation's demand independently of what is driving the
// pin, so a client can tell a FORCE that is countermanding a live rule from an
// idle one (#348). A rule going active UNDER a held force must push: that is the
// exact transition the app cannot see any other way.
static void test_rule_active_bit_reported_under_held_force(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    g_override[0] = ACT_OVR_FORCE_OFF;
    ActionBroadcaster::tick();                       // snapshot everything
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_FORCE_OFF, frameState(0).overrideFlags);

    g_frameCount = 0;
    g_ruleActive[0] = true;                          // rule trips beneath the force
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_FORCE_OFF | PREOBD_ACTION_OVR_RULE_ACTIVE,
                            frameState(0).overrideFlags);
    // The pin is still OFF — the demand and the driven state disagree, which is
    // the whole point of surfacing the bit.
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_STATE_OFF, frameState(0).state);

    g_frameCount = 0;
    g_ruleActive[0] = false;                         // …and the release pushes too
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_FORCE_OFF, frameState(0).overrideFlags);
}

// Both reported bits come from ONE membership walk per slot: packState() used to
// resolve it twice (once per bit), and ActionBroadcaster::tick() has no scan gate.
static void test_pack_resolves_membership_once_per_slot(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    g_ruleActive[0] = true;
    g_sourceFaulted[0] = true;
    g_driveCalls = 0;
    ActionBroadcaster::tick();
    // packState() returns early for an unconfigured slot, so only the one
    // occupied slot resolves: once to pack, once to send -- never twice per pack.
    TEST_ASSERT_EQUAL_INT(2, g_driveCalls);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_OVR_SOURCE_FAULTED | PREOBD_ACTION_OVR_RULE_ACTIVE,
                            frameState(0).overrideFlags);
}

// An unconfigured slot must not grow a demand bit: bytes past actionId stay zero.
static void test_rule_active_bit_absent_on_empty_slot(void) {
    g_ruleActive[0] = true;                          // would-be truth for slot 0
    ActionBroadcaster::tick();                       // slot 0 is empty
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_ID_NONE, frameState(0).actionId);
    TEST_ASSERT_EQUAL_UINT8(0, frameState(0).overrideFlags);
}

// A NONE (status-only) command performs no toggle.
static void test_handle_inbound_none_is_noop(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;  // else the version gate
    req.actionId = 7;                                   // rejects it and this test
    req.command  = PREOBD_ACTION_CMD_NONE;              // passes vacuously
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);
}

// Unknown actionId is rejected.
static void test_handle_inbound_unknown_id(void) {
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION;  // reach the id check, not
    req.actionId = 99;  // not configured                  the version gate
    req.command  = PREOBD_ACTION_CMD_MANUAL_ON;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);
    TEST_ASSERT_EQUAL_INT(0, g_commandCalls);
}

// A mismatched protocolVersion is rejected outright — the 0x05 frame id is shared
// with the retired 16-byte PreobdRelayState, whose byte 10 is NOT a command field.
static void test_handle_inbound_rejects_wrong_protocol_version(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.protocolVersion = PREOBD_BLE_PROTOCOL_VERSION + 1;
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_MANUAL_ON;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_commandCalls);
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);
}

// Wrong payload length is rejected outright.
static void test_handle_inbound_rejects_bad_length(void) {
    uint8_t shortBuf[8] = {0};
    ActionBroadcaster::handleInbound(shortBuf, sizeof(shortBuf));
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);
}

// Heartbeat re-sends every slot after the interval with no field change.
static void test_heartbeat_resends_all(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, 5, 0, 0);
    ActionBroadcaster::tick();      // snapshot at t=0
    g_frameCount = 0;
    ActionBroadcaster::tick();      // still t=0 → quiet
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
    g_test_now = 5000;              // kHeartbeatMs elapsed
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(MAX_ACTIONS, g_frameCount);
}


// A multi-rule Action reports its member count, and ruleRef stays the FIRST
// member so a client predating the byte still renders a working link (#343).
static void test_rule_count_reports_members(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    g_extraMembers[0] = 2;  // rules 5 + two more
    ActionBroadcaster::tick();
    const PreobdActionState& a = frameState(0);
    TEST_ASSERT_EQUAL_UINT8(3, a.ruleCount);
    TEST_ASSERT_EQUAL_UINT8(5, a.ruleRef);
}

// A membership edit changes nothing else in the frame, so it only reaches the
// client if ruleCount is part of the change key.
static void test_membership_change_pushes(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, 5, 0, 0);
    ActionBroadcaster::tick();
    g_frameCount = 0;

    g_extraMembers[0] = 1;  // ACTION 3 RULE ADD <other>
    ActionBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(2, frameState(0).ruleCount);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_snapshots_all_slots);
    RUN_TEST(test_rule_count_reports_members);
    RUN_TEST(test_membership_change_pushes);
    RUN_TEST(test_empty_slot_emits_none_id);
    RUN_TEST(test_idempotent_after_snapshot);
    RUN_TEST(test_state_change_emits_one_frame);
    RUN_TEST(test_handle_inbound_manual_on);
    RUN_TEST(test_handle_inbound_manual_on_rule_driven_sets_override);
    RUN_TEST(test_handle_inbound_auto_releases_override);
    RUN_TEST(test_handle_inbound_auto_on_manual_action_is_harmless);
    RUN_TEST(test_handle_inbound_unknown_command_is_inert);
    RUN_TEST(test_override_flags_reported_and_push_on_change);
    RUN_TEST(test_rule_active_bit_reported_under_held_force);
    RUN_TEST(test_rule_active_bit_absent_on_empty_slot);
    RUN_TEST(test_pack_resolves_membership_once_per_slot);
    RUN_TEST(test_handle_inbound_none_is_noop);
    RUN_TEST(test_handle_inbound_unknown_id);
    RUN_TEST(test_handle_inbound_rejects_wrong_protocol_version);
    RUN_TEST(test_handle_inbound_rejects_bad_length);
    RUN_TEST(test_heartbeat_resends_all);
    return UNITY_END();
}
