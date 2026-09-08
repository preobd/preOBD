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

#include "boot_order_fixture.h"  // inputsTableIsLiveForAssert() stub (#352, #369)
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
// Frames per actionId. Index 0xFF is the empty-slot ("this action is gone")
// frame — the thing #365 was flooding the link with, and the only handle the
// wire gives us on an unconfigured slot, since PreobdActionState carries no
// slot index.
static int           g_countById[256];

// Frames this pass may still accept before the "TX buffer" refuses; -1 = no
// limit. Models the real 64-byte buffer, which fits exactly three 21-byte
// frames — the shape of #350.
static int g_sendBudget = -1;
// Every call, accepted or refused. Refused sends are invisible to g_frameCount
// but are exactly what a busy link produces, which is #349's trap.
static int g_sendAttempts = 0;

bool MuxTransport::sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    ++g_sendAttempts;
    if (g_sendBudget == 0) return false;
    if (g_sendBudget > 0) --g_sendBudget;
    if (g_frameCount < 8) {
        g_frames[g_frameCount].type = type;
        g_frames[g_frameCount].len  = len;
        if (len <= sizeof(g_frames[0].payload)) memcpy(g_frames[g_frameCount].payload, payload, len);
    }
    if (len == PREOBD_ACTION_STATE_SIZE) {
        PreobdActionState st;
        memcpy(&st, payload, sizeof(st));
        ++g_countById[st.actionId];
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
    g_sendBudget = -1;
    g_sendAttempts = 0;
    g_test_now = 0;
    memset(g_countById, 0, sizeof(g_countById));
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
    setInputsTableLive(true);  // every test but the boot-order one below
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

// Cold start emits the CONFIGURED slots only: the catch-up burst scales with the
// Action count, not with MAX_ACTIONS (#365). Assert the payload layout too.
static void test_first_tick_snapshots_configured_slots(void) {
    configureAction(0, /*id*/3, PREOBD_ACTION_TYPE_RELAY, /*target*/9,
                    /*ruleRef*/5, /*behavior*/0, PREOBD_ACTION_FAILSAFE_ON);
    g_actionState[0] = true;  // action 0 currently ON

    ActionBroadcaster::tick();

    TEST_ASSERT_EQUAL_INT(1, g_frameCount);                      // the one configured slot
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

// A slot that goes empty is announced with actionId == NONE, so a client can drop
// its tile. Never-configured slots stay silent (#365) — the frame exists for the
// TRANSITION, not as a census of the table.
static void test_deleted_slot_emits_none_id(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    ActionBroadcaster::tick();
    g_frameCount = 0;
    actions[0].id = CONTROL_ID_NONE;          // ACTION 3 DELETE
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_ACTION_ID_NONE, frameState(0).actionId);
    TEST_ASSERT_EQUAL_UINT8(PREOBD_BLE_PROTOCOL_VERSION, frameState(0).protocolVersion);
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

// An emptied slot must not grow a demand bit: bytes past actionId stay zero.
static void test_rule_active_bit_absent_on_empty_slot(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, /*ruleRef*/5, 0, 0);
    ActionBroadcaster::tick();
    g_frameCount = 0;
    g_ruleActive[0] = true;                          // would-be truth for slot 0
    actions[0].id = CONTROL_ID_NONE;                 // …which is now empty
    ActionBroadcaster::tick();
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

// Heartbeat re-sends the CONFIGURED slots after the interval with no field
// change — and only those (#365).
static void test_heartbeat_resends_configured_only(void) {
    configureAction(0, 3, PREOBD_ACTION_TYPE_RELAY, 9, 5, 0, 0);
    ActionBroadcaster::tick();      // snapshot at t=0
    g_frameCount = 0;
    ActionBroadcaster::tick();      // still t=0 → quiet
    TEST_ASSERT_EQUAL_INT(0, g_frameCount);
    g_test_now = 5000;              // kHeartbeatMs elapsed
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(1, g_frameCount);
    TEST_ASSERT_EQUAL_UINT8(3, frameState(0).actionId);
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

// ---- #365: the heartbeat must not sweep the empty slots -------------------

// Configure `n` actions in slots 0..n-1, ids 1..n, each following a rule.
static void configureActions(int n) {
    for (int i = 0; i < n; ++i)
        configureAction(i, (uint8_t)(i + 1), PREOBD_ACTION_TYPE_RELAY,
                        (uint8_t)(20 + i), /*ruleRef*/5, 0, 0);
}

// Reproduce the boot order the firmware uses: init() runs at main.cpp:309,
// BEFORE initInputManager() (line 364) loads the control tables, so actions[] is
// still BSS-zeroed and every slot reads id == 0 (#352's twin — seeding
// occupancy from the table there would arm the grace window for slots that
// never held an Action, putting all of them back in the heartbeat).
static void bootAsFirmwareDoes(int configured) {
    memset(actions, 0, sizeof(actions));          // BSS: every id == 0
    setInputsTableLive(false);                    // table not ground truth yet (#369)
    ActionBroadcaster::init();                    // …before the table exists
    setInputsTableLive(true);                     // initInputManager() flips this
    for (int i = 0; i < MAX_ACTIONS; ++i) actions[i].id = CONTROL_ID_NONE;
    configureActions(configured);
    g_frameCount = 0;
    memset(g_countById, 0, sizeof(g_countById));
}

// THE BUG. The bench saw 236 actionId == 0xFF frames in 20 s on a 5-action
// board — MAX_ACTIONS - 5 empty slots, every 5 s — because tick() used the
// 5-argument SlottedBroadcaster::tick(), i.e. the always-true heartbeat
// predicate. The configured Actions must still beat.
static void test_heartbeat_skips_empty_slots(void) {
    bootAsFirmwareDoes(5);

    for (int hb = 0; hb < 4; ++hb) {              // 20 s of heartbeats
        g_test_now += 5000;
        ActionBroadcaster::tick();
    }
    TEST_ASSERT_EQUAL_INT(0, g_countById[PREOBD_ACTION_ID_NONE]);
    for (uint8_t id = 1; id <= 5; ++id) TEST_ASSERT_EQUAL_INT(4, g_countById[id]);
    TEST_ASSERT_EQUAL_INT(20, g_frameCount);      // 5 actions x 4 heartbeats
}

// A deleted Action still announces itself gone — but a BOUNDED number of times,
// not forever and not once. One edge-triggered push plus kClearHeartbeats
// repeats, then silence.
static void test_deleted_action_clear_repeats_then_stops(void) {
    bootAsFirmwareDoes(2);
    g_test_now += 5000;
    ActionBroadcaster::tick();                    // one clean heartbeat
    g_frameCount = 0;
    memset(g_countById, 0, sizeof(g_countById));

    actions[1].id = CONTROL_ID_NONE;              // ACTION 2 DELETE
    ActionBroadcaster::tick();                    // the edge-triggered push
    TEST_ASSERT_EQUAL_INT(1, g_countById[PREOBD_ACTION_ID_NONE]);

    for (int hb = 0; hb < 10; ++hb) {             // 50 s of heartbeats
        g_test_now += 5000;
        ActionBroadcaster::tick();
    }
    // 1 push + 3 heartbeat repeats, then the slot leaves the heartbeat for good.
    TEST_ASSERT_EQUAL_INT(4, g_countById[PREOBD_ACTION_ID_NONE]);
    TEST_ASSERT_EQUAL_INT(10, g_countById[1]);    // the survivor keeps beating
}

// #349's lesson: the grace window is spent per ACCEPTED frame, never per
// heartbeat edge. With a 3-frame TX budget the clears are refused for whole
// passes at a time; counting edges would burn the window down to nothing and
// the repeats would never arrive.
static void test_clear_window_survives_backpressure(void) {
    bootAsFirmwareDoes(8);
    for (int pass = 0; pass < 20; ++pass) { g_sendBudget = 3; g_test_now += 1; ActionBroadcaster::tick(); }
    g_frameCount = 0;
    memset(g_countById, 0, sizeof(g_countById));

    actions[7].id = CONTROL_ID_NONE;              // delete under a busy link
    for (int hb = 0; hb < 10; ++hb) {
        g_test_now += 5000;
        for (int pass = 0; pass < 20; ++pass) {   // let each sweep drain
            g_sendBudget = 3;                     // 64-byte TX buffer
            g_test_now += 1;
            ActionBroadcaster::tick();
        }
    }
    g_sendBudget = -1;
    TEST_ASSERT_EQUAL_INT(4, g_countById[PREOBD_ACTION_ID_NONE]);
}

// A reconnecting client resyncs the configured Actions only — an empty slot has
// no tile to rebuild. Pre-fix, invalidateAll() pushed the whole MAX_ACTIONS
// table on every connection edge.
static void test_resync_reemits_configured_only(void) {
    bootAsFirmwareDoes(3);
    g_frameCount = 0;
    memset(g_countById, 0, sizeof(g_countById));

    ActionBroadcaster::resync();                  // a central connects
    ActionBroadcaster::tick();
    TEST_ASSERT_EQUAL_INT(3, g_frameCount);
    TEST_ASSERT_EQUAL_INT(0, g_countById[PREOBD_ACTION_ID_NONE]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_snapshots_configured_slots);
    RUN_TEST(test_rule_count_reports_members);
    RUN_TEST(test_membership_change_pushes);
    RUN_TEST(test_deleted_slot_emits_none_id);
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
    RUN_TEST(test_heartbeat_resends_configured_only);
    RUN_TEST(test_heartbeat_skips_empty_slots);
    RUN_TEST(test_deleted_action_clear_repeats_then_stops);
    RUN_TEST(test_clear_window_survives_backpressure);
    RUN_TEST(test_resync_reemits_configured_only);
    return UNITY_END();
}
