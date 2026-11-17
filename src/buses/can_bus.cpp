/*
 * can_bus.cpp - CAN bus ownership: attach, one reader, one write gate, restart.
 * See can_bus.h for the rules and ARCHITECTURE §14.7.
 */

#include "can_bus.h"
#include "../config/system_config.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// The native tests supply their own HAL, bus count and pin claim
#ifndef CAN_BUS_TEST
#include "../hal/hal_can.h"
#include "../hal/bus_defaults.h"
#include "bus_manager.h"
#define CANHAL hal::can
#define CAN_BUS_COUNT NUM_CAN_BUSES
#endif

#if CAN_BUS_COUNT > 0

namespace {

struct CanBusRuntime {
    uint8_t consumers;   // bit n = CanConsumer n
    uint8_t state;       // CanBusState (OFF / IDLE derive from settings when not serving)
    uint8_t runMode;     // CanBusMode the controller runs in
    uint8_t runBaud;     // CanBaudIndex it runs at
    bool    begun;       // the controller was started once (later starts are restarts)
};

CanBusRuntime rt[CAN_BUS_COUNT];
CanFrameFn onFrameFn[NUM_CAN_CONSUMERS];
CanResetFn onResetFn[NUM_CAN_CONSUMERS];
CanBusyFn  busyFn[NUM_CAN_CONSUMERS];

inline bool validBus(uint8_t bus) { return bus < CAN_BUS_COUNT; }
inline uint8_t settingMode(uint8_t bus) { return systemConfig.buses.can[bus].mode; }

// Begin, or restart a running bus, in its stored mode and rate
FLASHMEM CanAttach startBus(uint8_t bus) {
    if (!canBusClaimed(bus) && !initCANBus(bus)) {
        rt[bus].state = CAN_STATE_NO_PINS;
        return CAN_ATTACH_NO_PINS;
    }
    const uint8_t mode = settingMode(bus);
    const uint8_t baud = systemConfig.buses.can[bus].baudIndex;
    const bool ok = rt[bus].begun ? CANHAL::restart(canBaudFromIndex(baud), bus, mode == CAN_BUS_LISTEN)
                                  : CANHAL::begin(canBaudFromIndex(baud), bus, mode == CAN_BUS_LISTEN);
    rt[bus].begun = true;   // begin() ran: from now on the driver restarts it
    if (!ok) {
        rt[bus].state = CAN_STATE_WAITING;
        return CAN_ATTACH_WAITING;
    }
    rt[bus].state = CAN_STATE_UP;
    rt[bus].runMode = mode;
    rt[bus].runBaud = baud;
    return CAN_ATTACH_OK;
}

// A bus that serves nobody (turned off, or its last consumer gone) must not
// ACK or transmit on a car's network, and no driver can stop cleanly: switch
// the controller to listen-only and stop reading it
FLASHMEM void silenceBus(uint8_t bus) {
    if (rt[bus].begun && rt[bus].state == CAN_STATE_UP && rt[bus].runMode != CAN_BUS_LISTEN) {
        CANHAL::restart(canBaudFromIndex(rt[bus].runBaud), bus, true);
        rt[bus].runMode = CAN_BUS_LISTEN;
    }
    if (rt[bus].state != CAN_STATE_NO_PINS) rt[bus].state = CAN_STATE_OFF;
}

FLASHMEM void resetConsumers(uint8_t bus) {
    for (uint8_t c = 0; c < NUM_CAN_CONSUMERS; c++) {
        if ((rt[bus].consumers & (1u << c)) && onResetFn[c]) onResetFn[c]();
    }
}

}  // namespace

void canSetHandlers(CanConsumer c, CanFrameFn onFrame, CanResetFn onReset, CanBusyFn busy) {
    if (c >= NUM_CAN_CONSUMERS) return;
    onFrameFn[c] = onFrame;
    onResetFn[c] = onReset;
    busyFn[c] = busy;
}

FLASHMEM CanAttach canAttach(uint8_t bus, CanConsumer c) {
    if (!validBus(bus) || c >= NUM_CAN_CONSUMERS) return CAN_ATTACH_NO_BUS;
    const uint8_t mode = settingMode(bus);
    if (mode == CAN_BUS_OFF) return CAN_ATTACH_BUS_OFF;
    if (c == CANC_OBD && mode == CAN_BUS_LISTEN) return CAN_ATTACH_LISTEN_ONLY;
    rt[bus].consumers |= (uint8_t)(1u << c);
    if (rt[bus].state == CAN_STATE_UP) return CAN_ATTACH_OK;   // never re-begun
    return startBus(bus);
}

FLASHMEM void canDetach(uint8_t bus, CanConsumer c) {
    if (!validBus(bus) || c >= NUM_CAN_CONSUMERS) return;
    rt[bus].consumers &= (uint8_t)~(1u << c);
    if (rt[bus].consumers == 0) silenceBus(bus);   // the next attach restarts it
}

FLASHMEM CanBusState canBusState(uint8_t bus) {
    if (!validBus(bus)) return CAN_STATE_OFF;
    if (settingMode(bus) == CAN_BUS_OFF) return CAN_STATE_OFF;
    if (rt[bus].state == CAN_STATE_OFF) return CAN_STATE_IDLE;
    return (CanBusState)rt[bus].state;
}

uint8_t canBusConsumers(uint8_t bus) { return validBus(bus) ? rt[bus].consumers : 0; }

bool canBusUp(uint8_t bus) {
    return validBus(bus) && rt[bus].state == CAN_STATE_UP && settingMode(bus) != CAN_BUS_OFF;
}

bool canBusListening(uint8_t bus) { return canBusUp(bus) && rt[bus].runMode == CAN_BUS_LISTEN; }

FLASHMEM uint32_t canBusRunningBaud(uint8_t bus) {
    return canBusUp(bus) ? canBaudFromIndex(rt[bus].runBaud) : 0;
}

bool canBusWrite(uint8_t bus, uint32_t id, const uint8_t* data, uint8_t len, bool extended) {
    if (!canBusUp(bus) || rt[bus].runMode != CAN_BUS_NORMAL) return false;
    return CANHAL::write(id, data, len, extended, bus);
}

bool canBusAbortUnackedTx(uint8_t bus, CanConsumer requester) {
    if (!canBusUp(bus)) return false;
    for (uint8_t c = 0; c < NUM_CAN_CONSUMERS; c++) {
        if (c == requester || !(rt[bus].consumers & (1u << c))) continue;
        if (busyFn[c] && busyFn[c]()) return false;   // e.g. OBD's multi-frame answer
    }
    return CANHAL::abortUnackedTx(bus);
}

FLASHMEM CanAttach canBusApply(uint8_t bus) {
    if (!validBus(bus)) return CAN_ATTACH_NO_BUS;
    const uint8_t mode = settingMode(bus);
    if (mode == CAN_BUS_OFF) {
        // BUS CAN <bus> DISABLE is refused while anything is attached, and an
        // import detaches what it moves off first; anything still attached
        // waits for the bus to come back on
        silenceBus(bus);
        return CAN_ATTACH_BUS_OFF;
    }
    if (rt[bus].consumers == 0) return CAN_ATTACH_OK;   // the settings apply on the next attach
    if (rt[bus].state == CAN_STATE_UP && rt[bus].runMode == mode &&
        rt[bus].runBaud == systemConfig.buses.can[bus].baudIndex) {
        return CAN_ATTACH_OK;   // nothing changed
    }
    const CanAttach r = startBus(bus);
    resetConsumers(bus);
    return r;
}

void canBusesUpdate() {
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
    bool ext;
    for (uint8_t bus = 0; bus < CAN_BUS_COUNT; bus++) {
        if (!canBusUp(bus)) continue;
        const uint8_t who = rt[bus].consumers;
        while (CANHAL::read(id, data, len, ext, bus)) {
            const bool obdRequest = !ext && (id == 0x7DF || id == 0x7E0);
            if (obdRequest && (who & (1u << CANC_OBD))) {
                if (onFrameFn[CANC_OBD]) onFrameFn[CANC_OBD](id, data, len, ext);
            } else if (who & (1u << CANC_INPUT)) {
                if (onFrameFn[CANC_INPUT]) onFrameFn[CANC_INPUT](id, data, len, ext);
            }
        }
    }
}

#else  // CAN_BUS_COUNT == 0: no CAN on this board

void canSetHandlers(CanConsumer, CanFrameFn, CanResetFn, CanBusyFn) {}
CanAttach canAttach(uint8_t, CanConsumer) { return CAN_ATTACH_NO_BUS; }
void canDetach(uint8_t, CanConsumer) {}
CanBusState canBusState(uint8_t) { return CAN_STATE_OFF; }
uint8_t canBusConsumers(uint8_t) { return 0; }
bool canBusUp(uint8_t) { return false; }
bool canBusListening(uint8_t) { return false; }
uint32_t canBusRunningBaud(uint8_t) { return 0; }
bool canBusWrite(uint8_t, uint32_t, const uint8_t*, uint8_t, bool) { return false; }
bool canBusAbortUnackedTx(uint8_t, CanConsumer) { return false; }
CanAttach canBusApply(uint8_t) { return CAN_ATTACH_NO_BUS; }
void canBusesUpdate() {}

#endif
