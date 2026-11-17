/*
 * transport_registry.h - Which transports exist, and which may be touched (#612 phase 2b-2)
 *
 * One slot per TransportID (core/transport_interface.h). main.cpp registers
 * each transport object at boot; the console (console/console_routing.h), the
 * log and the stream outputs look them up here. This was the registry half
 * of the old MessageRouter.
 *
 * The rule it keeps (#558): a hardware serial transport is only touched once
 * its port has been started. On Teensy 4 a UART that never ran begin() has
 * its clock gated, and reading any of its registers (available() reads WATER)
 * is a hard fault: BUS SERIAL 1 DISABLE + SAVE on a FRAMED port boot-looped
 * the board, whatever the firmware.
 */
#ifndef TRANSPORT_REGISTRY_H
#define TRANSPORT_REGISTRY_H

#include "../core/transport_interface.h"

// Registered predicate: is a hardware serial port's transport currently
// enabled? (Same shape as PinOwnerInputProbe/buses/pinctrl/pin_owner.h and
// SystemModeObserver/core/system_mode.h: the layer that actually knows --
// buses/serial_manager.h -- implements it, main.cpp wires it up, and
// transports/ never includes buses/serial_manager.h, #418.)
// port_id is 1-based (TRANSPORT_SERIAL1 -> port_id 1, ... SERIAL8 -> 8).
//
// Unregistered, the probe answers ACTIVE (does not block): early boot and the
// host unit tests have no driver to ask, and refusing every serial transport
// then would silently drop a stored console choice. This codebase's probes
// fail permissive, not restrictive. Register it early in setup(), before any
// CLI command can run (#352, #368, #369 were boot-order bugs of this class).
typedef bool (*SerialPortActiveProbe)(uint8_t port_id);
void setSerialPortActiveProbe(SerialPortActiveProbe probe);

// Is serial port <port_id> (1-based) enabled, by the probe
bool serialTransportPortActive(uint8_t port_id);

// May transport <id> be touched: not a serial port, or one whose port started
bool transportPortStarted(uint8_t id);

// Register a transport in its slot (ignored for NONE, out of range or nullptr)
void transportRegister(TransportID id, TransportInterface* transport);

// The transport in slot <id>, started or not; nullptr if none is registered.
// Only for what may bypass the started-port guard: the console's USB fallback
// (USB has no port to start) and CONSOLE/LOG TRANSPORT's "is it registered"
TransportInterface* transportRegistered(uint8_t id);

// The transport in slot <id> if it is registered and its port started, else
// nullptr. What the console, the log and the stream outputs write to.
TransportInterface* transportGet(uint8_t id);

// begin() every registered transport (once, at boot, after the console's and
// the log's stored transports are applied: routingApplyConfig())
void transportsBegin();

// update() every registered, started transport (each loop: the framed mux
// pumps its frames here)
void transportsUpdate();

// LIST TRANSPORTS: the registered, started transports and their state
void transportListAvailable();

#endif // TRANSPORT_REGISTRY_H
