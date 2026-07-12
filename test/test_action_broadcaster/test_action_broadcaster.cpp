// Native unit test: ActionBroadcaster (src/lib/action_broadcaster.cpp).
//
// Two directions exercised on the host: (a) tick() packs systemConfig.actions[]
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

// controlActionState() — we own the driven-state truth here.
static bool g_actionState[MAX_ACTIONS];
bool controlActionState(int slot) {
    return (slot >= 0 && slot < MAX_ACTIONS) ? g_actionState[slot] : false;
}

// controlActionSlot() — resolve id → slot against our config.
int controlActionSlot(uint8_t id) {
    if (id == CONTROL_ID_NONE) return -1;
    for (int i = 0; i < MAX_ACTIONS; ++i) {
        if (systemConfig.actions[i].id == id) return i;
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
    for (int i = 0; i < MAX_ACTIONS; ++i) systemConfig.actions[i].id = CONTROL_ID_NONE;  // all empty
    g_setManualCalls = 0;
    ActionBroadcaster::init();
}
void tearDown(void) {}

static void configureAction(int i, uint8_t id, uint8_t type, uint8_t target,
                            uint8_t ruleRef, uint8_t behavior, uint8_t failsafe) {
    systemConfig.actions[i].id       = id;
    systemConfig.actions[i].type     = type;
    systemConfig.actions[i].target   = target;
    systemConfig.actions[i].ruleRef  = ruleRef;
    systemConfig.actions[i].behavior = behavior;
    systemConfig.actions[i].failsafe = failsafe;
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

// A NONE (status-only) command performs no toggle.
static void test_handle_inbound_none_is_noop(void) {
    configureAction(0, 7, PREOBD_ACTION_TYPE_RELAY, 9, CONTROL_ID_NONE, 0, 0);
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.actionId = 7;
    req.command  = PREOBD_ACTION_CMD_NONE;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    TEST_ASSERT_EQUAL_INT(0, g_setManualCalls);
}

// Unknown actionId is rejected.
static void test_handle_inbound_unknown_id(void) {
    PreobdActionState req;
    memset(&req, 0, sizeof(req));
    req.actionId = 99;  // not configured
    req.command  = PREOBD_ACTION_CMD_MANUAL_ON;
    ActionBroadcaster::handleInbound(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_snapshots_all_slots);
    RUN_TEST(test_empty_slot_emits_none_id);
    RUN_TEST(test_idempotent_after_snapshot);
    RUN_TEST(test_state_change_emits_one_frame);
    RUN_TEST(test_handle_inbound_manual_on);
    RUN_TEST(test_handle_inbound_none_is_noop);
    RUN_TEST(test_handle_inbound_unknown_id);
    RUN_TEST(test_handle_inbound_rejects_bad_length);
    RUN_TEST(test_heartbeat_resends_all);
    return UNITY_END();
}
