// Native unit test: the bus manager owns the CAN buses (#612 phase 2a,
// src/buses/can_bus.cpp, ARCHITECTURE §14.7).
//
// What a flashed unit depends on: a bus starts once, on the first attach, and
// is never re-begun because a second consumer attached (the #611 leftover:
// OBD ENABLE re-began the bus CAN input had set up); LISTEN never transmits;
// OBD requests reach only the OBD responder; a new rate or mode restarts the
// bus and resets every consumer; a start that fails reads as down, with the
// consumers still attached.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/config/system_config.h"

// ---- a fake controller per bus ----
namespace fakehal {
struct Frame { uint32_t id; uint8_t data[8]; uint8_t len; bool ext; };
struct Bus {
    int begins, restarts, writes, aborts;
    bool beginOk;
    bool listen;
    uint32_t baud;
    Frame rx[8];
    uint8_t rxHead, rxTail;
};
Bus bus[2];
bool begin(uint32_t baud, uint8_t b, bool listenOnly) {
    bus[b].begins++; bus[b].baud = baud; bus[b].listen = listenOnly;
    return bus[b].beginOk;
}
bool restart(uint32_t baud, uint8_t b, bool listenOnly) {
    bus[b].restarts++; bus[b].baud = baud; bus[b].listen = listenOnly;
    return bus[b].beginOk;
}
bool write(uint32_t, const uint8_t*, uint8_t, bool, uint8_t b) { bus[b].writes++; return true; }
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& ext, uint8_t b) {
    Bus& x = bus[b];
    if (x.rxHead == x.rxTail) return false;
    const Frame& f = x.rx[x.rxTail++ % 8];
    id = f.id; len = f.len; ext = f.ext; memcpy(data, f.data, 8);
    return true;
}
bool abortUnackedTx(uint8_t b) { bus[b].aborts++; return true; }
void push(uint8_t b, uint32_t id, bool ext = false) {
    Frame& f = bus[b].rx[bus[b].rxHead++ % 8];
    f.id = id; f.len = 8; f.ext = ext; memset(f.data, 0, 8);
}
}  // namespace fakehal

// ---- the pin claim bus_manager.cpp would make ----
static bool g_claimOk[2];
static bool g_claimed[2];
bool canBusClaimed(uint8_t b) { return b < 2 && g_claimed[b]; }
bool initCANBus(uint8_t b) {
    if (b >= 2 || !g_claimOk[b]) return false;
    g_claimed[b] = true;
    return true;
}

#define CAN_BUS_TEST 1
#define CANHAL fakehal
#define CAN_BUS_COUNT 2
#include "../../src/buses/can_bus.cpp"

SystemConfig systemConfig;

// ---- consumers that record what reaches them ----
static int g_inFrames, g_obdFrames, g_inResets, g_obdResets;
static uint32_t g_lastInId, g_lastObdId;
static bool g_obdBusy;
static void inFrame(uint32_t id, const uint8_t*, uint8_t, bool) { g_inFrames++; g_lastInId = id; }
static void obdFrame(uint32_t id, const uint8_t*, uint8_t, bool) { g_obdFrames++; g_lastObdId = id; }
static void inReset() { g_inResets++; }
static void obdReset() { g_obdResets++; }
static bool obdBusy() { return g_obdBusy; }

void setUp(void) {
    memset(&systemConfig, 0, sizeof(systemConfig));
    for (uint8_t b = 0; b < CAN_BUS_SLOTS; b++) {
        systemConfig.buses.can[b].mode = CAN_BUS_OFF;
        systemConfig.buses.can[b].baudIndex = CAN_BAUD_500K;
    }
    memset(fakehal::bus, 0, sizeof(fakehal::bus));
    fakehal::bus[0].beginOk = fakehal::bus[1].beginOk = true;
    g_claimOk[0] = g_claimOk[1] = true;
    g_claimed[0] = g_claimed[1] = false;
    memset(rt, 0, sizeof(rt));
    g_inFrames = g_obdFrames = g_inResets = g_obdResets = 0;
    g_lastInId = g_lastObdId = 0;
    g_obdBusy = false;
    canSetHandlers(CANC_INPUT, inFrame, inReset, nullptr);
    canSetHandlers(CANC_OBD, obdFrame, obdReset, obdBusy);
}
void tearDown(void) {}

static void test_attach_to_an_off_bus_is_refused(void) {
    TEST_ASSERT_EQUAL(CAN_ATTACH_BUS_OFF, canAttach(0, CANC_OBD));
    TEST_ASSERT_EQUAL(CAN_ATTACH_BUS_OFF, canAttach(0, CANC_INPUT));
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].begins);
    TEST_ASSERT_EQUAL(CAN_STATE_OFF, canBusState(0));
    TEST_ASSERT_EQUAL(CAN_ATTACH_NO_BUS, canAttach(2, CANC_INPUT));   // past CAN_BUS_COUNT
}

static void test_an_on_bus_starts_on_the_first_attach_only(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    TEST_ASSERT_EQUAL(CAN_STATE_IDLE, canBusState(0));   // on, nothing attached: not begun
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].begins);
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_OBD));
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].begins);
    TEST_ASSERT_EQUAL_UINT32(500000, fakehal::bus[0].baud);
    // The #611 leftover: a second consumer must not re-begin the bus. (CAN input
    // after OBD: the one order #632 still lets them meet, a LOAD in passing)
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_INPUT));
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].begins);
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].restarts);
    TEST_ASSERT_EQUAL_HEX8(0x03, canBusConsumers(0));
    TEST_ASSERT_TRUE(canBusUp(0));
}

static void test_listen_never_transmits(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_LISTEN;
    TEST_ASSERT_EQUAL(CAN_ATTACH_LISTEN_ONLY, canAttach(0, CANC_OBD));   // OBD sends
    TEST_ASSERT_EQUAL_HEX8(0, canBusConsumers(0));
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_INPUT));
    TEST_ASSERT_TRUE(fakehal::bus[0].listen);
    TEST_ASSERT_TRUE(canBusListening(0));
    const uint8_t d[8] = {0};
    TEST_ASSERT_FALSE(canBusWrite(0, 0x7DF, d, 8, false));
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].writes);
}

// One job per bus (#632), the guard behind output admission: OBD never joins
// CAN input; CAN input is never refused (reading the car wins)
static void test_obd_cannot_join_can_input(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_INPUT));
    TEST_ASSERT_EQUAL(CAN_ATTACH_INPUT_BUS, canAttach(0, CANC_OBD));
    TEST_ASSERT_EQUAL_HEX8(0x01, canBusConsumers(0));   // OBD not attached
    canDetach(0, CANC_INPUT);
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_OBD));
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_INPUT));   // input still joins
}

static void test_obd_requests_reach_only_obd(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_INPUT);
    // Input alone takes every frame, OBD request ids included
    fakehal::push(0, 0x7DF);
    canBusesUpdate();
    TEST_ASSERT_EQUAL_INT(1, g_inFrames);

    // OBD then input (OBD can't join input, #632)
    canDetach(0, CANC_INPUT);
    canAttach(0, CANC_OBD);
    canAttach(0, CANC_INPUT);
    fakehal::push(0, 0x7DF);
    fakehal::push(0, 0x7E0);
    fakehal::push(0, 0x7E8);
    fakehal::push(0, 0x7DF, true);   // a 29-bit 0x7DF is not an OBD request
    canBusesUpdate();
    TEST_ASSERT_EQUAL_INT(2, g_obdFrames);
    TEST_ASSERT_EQUAL_UINT32(0x7E0, g_lastObdId);
    TEST_ASSERT_EQUAL_INT(3, g_inFrames);   // + 0x7E8 and the 29-bit frame
}

static void test_a_detached_consumer_gets_nothing(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_OBD);
    canAttach(0, CANC_INPUT);
    canDetach(0, CANC_OBD);
    fakehal::push(0, 0x7DF);
    canBusesUpdate();
    TEST_ASSERT_EQUAL_INT(0, g_obdFrames);
    TEST_ASSERT_EQUAL_INT(1, g_inFrames);   // the request goes to input again
    TEST_ASSERT_TRUE(canBusUp(0));          // the bus keeps running
}

static void test_a_new_rate_restarts_and_resets_every_consumer(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_OBD);
    canAttach(0, CANC_INPUT);
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canBusApply(0));   // nothing changed: nothing happens
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].restarts);
    TEST_ASSERT_EQUAL_INT(0, g_inResets);

    systemConfig.buses.can[0].baudIndex = CAN_BAUD_250K;
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canBusApply(0));
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].restarts);
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].begins);   // restarted, not re-begun
    TEST_ASSERT_EQUAL_UINT32(250000, fakehal::bus[0].baud);
    TEST_ASSERT_EQUAL_UINT32(250000, canBusRunningBaud(0));
    TEST_ASSERT_EQUAL_INT(1, g_inResets);
    TEST_ASSERT_EQUAL_INT(1, g_obdResets);
}

static void test_a_failed_restart_waits_with_its_consumers(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_INPUT);
    fakehal::bus[0].beginOk = false;
    systemConfig.buses.can[0].baudIndex = CAN_BAUD_1M;
    TEST_ASSERT_EQUAL(CAN_ATTACH_WAITING, canBusApply(0));
    TEST_ASSERT_EQUAL(CAN_STATE_WAITING, canBusState(0));
    TEST_ASSERT_FALSE(canBusUp(0));
    TEST_ASSERT_EQUAL_HEX8(0x01, canBusConsumers(0));   // nothing reverts, nobody detached
    const uint8_t d[8] = {0};
    TEST_ASSERT_FALSE(canBusWrite(0, 0x7DF, d, 8, false));
    fakehal::push(0, 0x100);
    canBusesUpdate();
    TEST_ASSERT_EQUAL_INT(0, g_inFrames);   // a down bus is not read
    // Fixed: the next apply starts it
    fakehal::bus[0].beginOk = true;
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canBusApply(0));
    TEST_ASSERT_TRUE(canBusUp(0));
}

// A failed first start must read as down, not up (the bus manager's half of
// the MCP2515 fix in can_mcp2515.h; the driver itself is benched, not here)
static void test_a_failed_first_start_reads_as_down(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    fakehal::bus[0].beginOk = false;
    TEST_ASSERT_EQUAL(CAN_ATTACH_WAITING, canAttach(0, CANC_INPUT));
    TEST_ASSERT_FALSE(canBusUp(0));
    TEST_ASSERT_EQUAL(CAN_STATE_WAITING, canBusState(0));
}

static void test_taken_pins_never_start_a_controller(void) {
    systemConfig.buses.can[1].mode = CAN_BUS_NORMAL;
    g_claimOk[1] = false;   // e.g. Serial1 on Teensy 4 CAN2's pins 0/1
    TEST_ASSERT_EQUAL(CAN_ATTACH_NO_PINS, canAttach(1, CANC_INPUT));
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[1].begins);
    TEST_ASSERT_EQUAL(CAN_STATE_NO_PINS, canBusState(1));
}

static void test_abort_waits_for_obd_answer_in_flight(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_OBD);     // OBD first: it can't join CAN input (#632)
    canAttach(0, CANC_INPUT);
    g_obdBusy = true;   // a multi-frame answer still going out
    TEST_ASSERT_FALSE(canBusAbortUnackedTx(0, CANC_INPUT));
    TEST_ASSERT_EQUAL_INT(0, fakehal::bus[0].aborts);
    g_obdBusy = false;
    TEST_ASSERT_TRUE(canBusAbortUnackedTx(0, CANC_INPUT));
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].aborts);
}

// A bus nobody uses must stop ACKing: the last detach (and DISABLE) puts the
// controller in listen-only, and the next attach restarts it, never re-begins
static void test_a_bus_nobody_uses_goes_listen_only(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_OBD);
    canDetach(0, CANC_OBD);
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].restarts);
    TEST_ASSERT_TRUE(fakehal::bus[0].listen);
    TEST_ASSERT_FALSE(canBusUp(0));
    TEST_ASSERT_EQUAL(CAN_STATE_IDLE, canBusState(0));
    TEST_ASSERT_EQUAL(CAN_ATTACH_OK, canAttach(0, CANC_OBD));
    TEST_ASSERT_EQUAL_INT(1, fakehal::bus[0].begins);
    TEST_ASSERT_EQUAL_INT(2, fakehal::bus[0].restarts);
    TEST_ASSERT_FALSE(fakehal::bus[0].listen);
    TEST_ASSERT_TRUE(canBusUp(0));
}

static void test_turning_a_bus_off_stops_reading_it(void) {
    systemConfig.buses.can[0].mode = CAN_BUS_NORMAL;
    canAttach(0, CANC_INPUT);
    canDetach(0, CANC_INPUT);
    systemConfig.buses.can[0].mode = CAN_BUS_OFF;
    TEST_ASSERT_EQUAL(CAN_ATTACH_BUS_OFF, canBusApply(0));
    TEST_ASSERT_FALSE(canBusUp(0));
    TEST_ASSERT_EQUAL(CAN_STATE_OFF, canBusState(0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_attach_to_an_off_bus_is_refused);
    RUN_TEST(test_an_on_bus_starts_on_the_first_attach_only);
    RUN_TEST(test_listen_never_transmits);
    RUN_TEST(test_obd_cannot_join_can_input);
    RUN_TEST(test_obd_requests_reach_only_obd);
    RUN_TEST(test_a_detached_consumer_gets_nothing);
    RUN_TEST(test_a_new_rate_restarts_and_resets_every_consumer);
    RUN_TEST(test_a_failed_restart_waits_with_its_consumers);
    RUN_TEST(test_a_failed_first_start_reads_as_down);
    RUN_TEST(test_taken_pins_never_start_a_controller);
    RUN_TEST(test_abort_waits_for_obd_answer_in_flight);
    RUN_TEST(test_a_bus_nobody_uses_goes_listen_only);
    RUN_TEST(test_turning_a_bus_off_stops_reading_it);
    return UNITY_END();
}
