/*
 * can_bus.h - The bus manager owns the CAN buses (#612 phase 2a, ARCHITECTURE §14.7)
 *
 * One set of settings per bus (systemConfig.buses.can[]), started once, read
 * by one reader, written through one gate:
 *
 *   - Consumers (CAN input, the OBD output) never start a bus. They register
 *     handlers once and attach to a bus; the first attach to a bus that is on
 *     begins it. A running bus is never re-begun because a consumer attached.
 *   - canBusesUpdate() is the only code that reads frames. On a bus with OBD
 *     attached, OBD requests (0x7DF / 0x7E0, 11-bit) go to OBD only; every
 *     other frame goes to CAN input if it is attached. The controllers accept
 *     every frame (no driver filters in hardware), so this is the filter.
 *   - Every transmit goes through canBusWrite(), which refuses on a LISTEN bus.
 *   - canBusApply() makes a bus match its settings after BUS CAN or an import
 *     (B2): a new rate or mode on a running bus restarts it, every attached
 *     consumer's reset handler runs, and a failed restart leaves the bus
 *     WAITING with its consumers still attached. Nothing reverts.
 *   - No driver can release a bus's pins while running: a bus turned off is
 *     no longer read or written, and its pins are free after a reboot.
 */

#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdint.h>

enum CanConsumer : uint8_t {
    CANC_INPUT = 0,   // CAN input (sensors/can/input_can.cpp)
    CANC_OBD   = 1,   // the OBD output (outputs/formats/output_can.cpp)
    CANC_LAYOUT = 2,  // the layout outputs, PREOBD and HALTECH (outputs/formats/can_layout_output.cpp)
    NUM_CAN_CONSUMERS = 3
};

enum CanBusState : uint8_t {
    CAN_STATE_OFF     = 0,   // mode OFF
    CAN_STATE_IDLE    = 1,   // on, nothing attached yet, not begun
    CAN_STATE_UP      = 2,   // begun and read
    CAN_STATE_WAITING = 3,   // a consumer is attached but the start failed
    CAN_STATE_NO_PINS = 4    // its pins belong to another function this boot
};

enum CanAttach : uint8_t {
    CAN_ATTACH_OK          = 0,
    CAN_ATTACH_NO_BUS      = 1,   // this board has no such bus
    CAN_ATTACH_BUS_OFF     = 2,   // BUS CAN <bus> ENABLE first
    CAN_ATTACH_LISTEN_ONLY = 3,   // a sending consumer on a LISTEN bus
    CAN_ATTACH_WAITING     = 4,   // attached, but the bus did not start
    CAN_ATTACH_NO_PINS     = 5,   // attached, but the bus's pins are taken
    CAN_ATTACH_INPUT_BUS   = 6    // a sending consumer where CAN input reads (#632)
};

typedef void (*CanFrameFn)(uint32_t id, const uint8_t* data, uint8_t len, bool extended);
typedef void (*CanResetFn)();     // the bus restarted: drop in-flight state
typedef bool (*CanBusyFn)();      // an exchange is in flight (abort would cut it)

// True for a consumer that transmits (OBD, layouts)
inline bool canConsumerSends(CanConsumer c) { return c == CANC_OBD || c == CANC_LAYOUT; }

// Register a consumer's handlers, once, before its first attach.
void canSetHandlers(CanConsumer c, CanFrameFn onFrame, CanResetFn onReset, CanBusyFn busy);

// Attach to / detach from a bus. Attaching to a bus that is on begins it if it
// is not running yet. Every sending consumer (OBD, layouts) is refused on LISTEN
// and on the bus CAN input reads (#632). Several layout outputs share the one
// CANC_LAYOUT bit on a bus: attaches are counted, and the bit clears on the last
// detach.
CanAttach canAttach(uint8_t bus, CanConsumer c);
void canDetach(uint8_t bus, CanConsumer c);

CanBusState canBusState(uint8_t bus);
uint8_t     canBusConsumers(uint8_t bus);   // bit n = CanConsumer n
bool        canBusUp(uint8_t bus);
// The mode and rate the bus is running in (not the stored setting)
bool        canBusListening(uint8_t bus);
uint32_t    canBusRunningBaud(uint8_t bus);

// Transmit on a running NORMAL bus; false otherwise.
bool canBusWrite(uint8_t bus, uint32_t id, const uint8_t* data, uint8_t len, bool extended);
// Abort unACKed transmits (CAN input's poll to an absent ECU), unless another
// consumer has an exchange in flight on the same bus.
bool canBusAbortUnackedTx(uint8_t bus, CanConsumer requester);

// Make one bus match systemConfig.buses.can[bus] (CONFIG mode). Returns the
// outcome for the bus's consumers: OK, WAITING, NO_PINS, or BUS_OFF.
CanAttach canBusApply(uint8_t bus);

// The one reader: drain every running bus and dispatch. Called from loop().
void canBusesUpdate();

#endif // CAN_BUS_H
