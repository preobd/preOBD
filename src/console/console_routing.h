/*
 * console_routing.h - Where the console and the log read and write (#612 phase 2b-2)
 *
 * The console reads commands from its transports and answers on the one a
 * command came from; the log writes to its own. This file holds both pairs of
 * transports (CONSOLE TRANSPORT, LOG TRANSPORT), pumps the console's sources
 * into the CLI each loop, and tells msg (core/message_api.h) where each write
 * goes: main.cpp registers consoleSink and logSink with it. The transports
 * themselves are in transports/transport_registry.h. Together these replace
 * the old MessageRouter, with its behaviour kept:
 *   - sources, in order each loop: the console's primary, its secondary, then
 *     every started framed port (the nRF link is always a console source, so
 *     the app can't be cut off); each is fed to the CLI and dispatched before
 *     the next, and a reply goes to that source alone;
 *   - unsolicited console output (no command running) goes to primary + secondary;
 *   - with neither console transport usable (a serial port turned off), the
 *     console answers on USB (#558); the log never falls back;
 *   - all sources share one CLI/JSON buffer (#624 is the per-source fix).
 *
 * Names: plane* take either stream (MessagePlane: PLANE_CONTROL = the console,
 * PLANE_DEBUG = the log); console* are the console's alone; routing* cover both.
 */
#ifndef CONSOLE_ROUTING_H
#define CONSOLE_ROUTING_H

#include "../core/transport_interface.h"
#include "../core/message_plane.h"

// Each loop, after transportsUpdate(): read every console source and dispatch
void consolePump();

// msg's sink resolvers (setMessageSinkResolver). The console's primary is the
// sender while a command runs, and its secondary is then nullptr, so the reply
// goes to the sender only.
TransportInterface* consoleSink(bool secondary);
TransportInterface* logSink(bool secondary);

// The source of the command being dispatched, nullptr between commands
TransportInterface* consoleActiveSource();

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
