/*
 * console_routing.h - Where the console and the log read and write (#612 phase 2b-2)
 *
 * Where console and log text goes. This file holds both pairs of transports
 * (CONSOLE TRANSPORT, LOG TRANSPORT) and tells msg (core/message_api.h) where
 * each write goes: main.cpp registers consoleSink and logSink with it. Where
 * commands come FROM is console_input.h, which reads every console source and
 * marks the one whose command is running (consoleSetActive). The transports
 * themselves are in transports/transport_registry.h.
 *   - a reply (a command running) goes to its source alone;
 *   - unsolicited console output (no command running) goes to primary + secondary;
 *   - with neither console transport usable (a serial port turned off), the
 *     console answers on USB (#558); the log never falls back.
 *
 * Names: plane* take either stream (MessagePlane: PLANE_CONTROL = the console,
 * PLANE_DEBUG = the log); console* are the console's alone; routing* cover both.
 */
#ifndef CONSOLE_ROUTING_H
#define CONSOLE_ROUTING_H

#include "../core/transport_interface.h"
#include "../core/message_plane.h"

// msg's sink resolvers (setMessageSinkResolver). The console's primary is the
// sender while a command runs, and its secondary is then nullptr, so the reply
// goes to the sender only.
TransportInterface* consoleSink(bool secondary);
TransportInterface* logSink(bool secondary);

// The source of the command being dispatched, nullptr between commands
TransportInterface* consoleActiveSource();

// console_input.cpp: the source whose bytes are being handled now, nullptr after
void consoleSetActive(TransportInterface* t);

// The console's primary (with the USB fallback) or secondary transport, as
// the console reads them; nullptr when unset or not usable
TransportInterface* consoleTransport(bool secondary);

// Neither console transport is usable, so the console answers on USB (#558)
bool consoleOnUsbFallback();

// A stream's transport id as set (no validation)
TransportID planeTransportId(MessagePlane plane, bool secondary);

// CONSOLE TRANSPORT / LOG TRANSPORT: refused (false) for an unregistered
// transport or a serial port that isn't enabled (BUS SERIAL <n> ENABLE first)
bool planeSetTransport(MessagePlane plane, TransportID id, bool secondary);

// SystemConfig <-> the live console and log (were MessageRouter::loadConfig /
// syncConfig). Apply writes the stored ids unchecked -- a stored choice must
// not vanish because its port isn't up yet at load time -- then applies the
// log filter. Capture copies the live ids and the filter back (SAVE persists).
void routingApplyConfig();
void routingCaptureConfig();

#endif // CONSOLE_ROUTING_H
