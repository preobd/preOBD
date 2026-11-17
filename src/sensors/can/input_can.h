/*
 * input_can.h - CAN Input Subsystem
 *
 * Receives CAN frames and populates the frame cache readCANSensor() reads.
 * A consumer of one CAN bus (#612 phase 2a): systemConfig.buses.canInputBus,
 * set with `BUS CAN <bus> INPUT ENABLE|DISABLE`. The bus manager starts and
 * reads the bus (buses/can_bus.h); input never touches the controller.
 * Supports any CAN ID (OBD-II, J1939, custom protocols).
 */

#ifndef INPUT_CAN_H
#define INPUT_CAN_H

#include <Arduino.h>

/**
 * Attach CAN input to systemConfig.buses.canInputBus (detaching first from
 * any bus it was on). Call again after the setting changes.
 *
 * @return true if attached to a running bus
 */
bool initCANInput();

/**
 * Called from the main loop. On a NORMAL bus it requests each imported OBD-II
 * PID (source ID 0x7E8-0x7EF) round-robin with a Mode 01 request, so a lone
 * preOBD on an ECU bus gets data (#476). LISTEN never transmits.
 */
void updateCANInput();

/** Detach CAN input from its bus. */
void shutdownCANInput();

/** Is CAN input attached to a bus that is running? */
bool canInputIsRunning();

/** The bus CAN input is attached to, 0xFF if none. */
uint8_t canInputAttachedBus();

/** After a JSON IMPORT or LOAD: buses and CAN input to the config, at once (#612). */
void syncCanToConfig();

#if SUPPORTS_JSON_EXPORT
struct Input;
struct CanBusSnapshot;

/** May CAN input transmit requests now? Attached to a bus running NORMAL (SCAN PIDS, #511). */
bool canInputRunsNormal();

/** Would the poller request this input's PID (NORMAL bus, OBD source, #476)? */
bool canInputIsPolled(const Input& in);

/** Fill BUS CAN JSON's live input fields (#510): running, frame and poll counters, backoff. */
void getCANInputStatus(CanBusSnapshot& s, uint32_t now);
#endif

#endif // INPUT_CAN_H
