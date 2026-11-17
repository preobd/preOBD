/*
 * message_router.h - Message Routing Engine
 *
 * Routes messages to appropriate transports based on:
 * - Message type (CONTROL/DATA/DEBUG)
 * - Runtime configuration (persisted in EEPROM)
 * - Multi-cast support (send to multiple transports)
 *
 * Configuration is stored in SystemConfig.router and persisted to EEPROM.
 */

#ifndef MESSAGE_ROUTER_H
#define MESSAGE_ROUTER_H

#include "transport_interface.h"
#include "log_filter.h"
#include <Arduino.h>

// Message plane enumeration
enum MessagePlane {
    PLANE_CONTROL = 0,  // The console: commands and their replies (CONSOLE TRANSPORT)
    PLANE_DEBUG   = 1,  // The log (LOG TRANSPORT). Sensor data is the outputs' (#612)
    NUM_PLANES    = 2
};

// Registered predicate: is a hardware serial port's transport currently
// enabled? (Same shape as PinOwnerInputProbe/buses/pinctrl/pin_owner.h,
// SystemModeObserver/core/system_mode.h, and
// setMuxFrameHandler/transports/transport_mux.h: the layer that actually knows --
// buses/serial_manager.h -- implements it, main.cpp wires it up, and
// transports/ never includes buses/serial_manager.h, #418.)
// port_id is 1-based (TRANSPORT_SERIAL1 -> port_id 1, ... SERIAL8 -> 8).
typedef bool (*SerialPortActiveProbe)(uint8_t port_id);

/**
 * Register the serial-port-active probe. MUST be called before the first
 * SET/BUS-driven setTransport() or LIST TRANSPORTS call can run -- both are
 * CLI commands processed from loop(), so registering anywhere in main.cpp's
 * setup() is sufficient (this repo has a history of boot-order regressions
 * here -- #352, #368, #369 -- so register early, alongside the other probes).
 *
 * Unregistered, the probe defaults to ACTIVE (does not block). Early boot
 * (before main.cpp registers it) and host unit tests have no driver to ask;
 * defaulting to "inactive" would silently refuse every serial transport in
 * that state -- exactly the "stored routing choice vanishes" failure
 * loadConfig()'s unvalidated path exists to avoid. Mirrors
 * PinOwnerInputProbe's unregistered default ("no input holds this pin" --
 * pin_owner.h) and SystemModeObserver's ("notifies no one" -- system_mode.h):
 * this codebase's probes fail permissive, not restrictive.
 */
void setSerialPortActiveProbe(SerialPortActiveProbe probe);

// Registered control-plane consumer: who actually pumps CLI command
// processing (same shape as PinOwnerInputProbe/buses/pinctrl/pin_owner.h,
// SystemModeObserver/core/system_mode.h, setMuxFrameHandler/
// addMuxResyncHandler in transports/transport_mux.h, and
// setSerialPortActiveProbe() above: the layer that actually knows --
// console/serial_config.h -- implements it, main.cpp wires it up, and transports/
// never includes console/serial_config.h, #418).
//
// byteSink receives one drained byte at a time (handleCommandInput()).
// dispatchFn is called after a transport's bytes are drained, to run
// whatever the CLI buffered (processSerialCommands()).
typedef void (*CommandByteSink)(char c);
typedef void (*CommandDispatchFn)();

/**
 * Register the control-plane command sink. MUST be called before loop()'s
 * first router.update() -- registering anywhere in main.cpp's setup(),
 * alongside setSerialPortActiveProbe() above, is sufficient. A missed
 * registration here is the loudest possible failure: the CLI goes dead over
 * every transport, but the firmware still compiles and still passes the
 * native suite, so get this one right (#352, #368, #369 are this same class
 * of boot-order bug for other probes).
 *
 * Unregistered, the router does NOT drain control transports: bytes stay
 * buffered in the transport (the CLI appears dead, but nothing is
 * destroyed). This is the opposite default from setSerialPortActiveProbe()
 * above -- that probe answers a yes/no question where "permissive" is
 * harmless, but this is a data sink, and there is nowhere safe to route a
 * drained byte with no consumer registered. Draining and discarding would
 * silently eat commands (and the JSON import stream) with no way to get them
 * back; leaving them buffered means once registration does happen (or the
 * transport is re-read) nothing typed has been lost.
 */
void setCommandSink(CommandByteSink byteSink, CommandDispatchFn dispatchFn);

// Transport ID enumeration
enum TransportID {
    TRANSPORT_NONE = 0,
    TRANSPORT_USB_SERIAL = 1,   // Serial (USB)
    TRANSPORT_SERIAL1 = 2,      // Hardware Serial1
    TRANSPORT_SERIAL2 = 3,      // Hardware Serial2
    TRANSPORT_SERIAL3 = 4,      // Hardware Serial3
    TRANSPORT_SERIAL4 = 5,      // Hardware Serial4
    TRANSPORT_SERIAL5 = 6,      // Hardware Serial5
    TRANSPORT_SERIAL6 = 7,      // Hardware Serial6
    TRANSPORT_SERIAL7 = 8,      // Hardware Serial7
    TRANSPORT_SERIAL8 = 9,      // Hardware Serial8 (Teensy 4.1 only)
    TRANSPORT_ESP32_BT = 10,    // ESP32 built-in Bluetooth Classic
    NUM_TRANSPORTS = 11
};

// Message router class
class MessageRouter {
private:
    TransportInterface* transports[NUM_TRANSPORTS];
    TransportInterface* activeControlTransport;  // Last transport that sent a command

    // Plane to transport mapping (runtime configurable)
    uint8_t primaryTransport[NUM_PLANES];
    uint8_t secondaryTransport[NUM_PLANES];  // For multi-cast

    // transports[id], or nullptr if none is registered there or it is a
    // hardware serial port that was never started (#558)
    TransportInterface* usableTransport(uint8_t transportId) const;

    // Log filtering (runtime configurable)
    LogFilter logFilter;

public:
    MessageRouter();

    // ========== Initialization ==========

    void begin();

    // Register a transport backend
    void registerTransport(TransportID id, TransportInterface* transport);

    // Load configuration from EEPROM (called by begin())
    //
    // Declared here, DEFINED in config/system_config.cpp (#418): the
    // SystemConfig <-> router shape is config/'s to know, not transports/'s, so
    // this stays a plain method (call sites are unchanged) while its body
    // lives on the storage side of the edge. loadConfig() writes through
    // setTransportUnchecked() below, not setTransport() -- a persisted
    // routing choice must never be silently dropped because its serial port
    // isn't enabled yet at load time.
    void loadConfig();

    // Sync router state to SystemConfig (without saving to EEPROM)
    // Defined in config/system_config.cpp -- see loadConfig() note above.
    void syncConfig();

    // Save configuration to EEPROM
    // Defined in config/system_config.cpp -- see loadConfig() note above.
    void saveConfig();

    // ========== Message Routing ==========

    // Get transport for a specific plane
    TransportInterface* getTransport(MessagePlane plane, bool primary = true);

    // Route text message to appropriate transport(s)
    void routeMessage(MessagePlane plane, const char* message);

    // Route binary data to appropriate transport(s)
    void routeMessage(MessagePlane plane, const uint8_t* data, size_t len);

    // ========== Control Plane Helpers ==========

    // Set which transport last sent a command (for response routing)
    void setActiveControlTransport(TransportInterface* transport);

    // Get the active control transport
    TransportInterface* getActiveControlTransport() {
        return activeControlTransport;
    }

    // ========== Log Filtering ==========

    // Get the log filter instance
    LogFilter& getLogFilter() {
        return logFilter;
    }

    // ========== Configuration ==========

    // Set transport for a specific plane
    bool setTransport(MessagePlane plane, TransportID transportId, bool secondary = false);

    // ========== Config marshalling accessors (#418) ==========
    // Surface for config/'s loadConfig()/syncConfig() definitions (above) to
    // read and write router state without transports/ needing to know SystemConfig
    // or EEPROM exists.

    // Raw transport IDs, no validation.
    TransportID getPrimaryTransportId(MessagePlane plane) const;
    TransportID getSecondaryTransportId(MessagePlane plane) const;

    // Write a plane's transport route directly, bypassing setTransport()'s
    // isSerialPortActive() gate. Used ONLY for loading a persisted
    // configuration -- see loadConfig() note above.
    void setTransportUnchecked(MessagePlane plane, TransportID transportId, bool secondary = false);

    // A stream output's transport (#612): registered and its port started
    // (#558: writing to a UART that never began faults on Teensy 4), else nullptr
    TransportInterface* streamTransport(uint8_t transportId) const { return usableTransport(transportId); }
    // Neither console transport is usable, so the console answers on USB (#558):
    // a binary output on USB pauses meanwhile
    bool consoleOnUsbFallback() const {
        return usableTransport(primaryTransport[PLANE_CONTROL]) == nullptr &&
               usableTransport(secondaryTransport[PLANE_CONTROL]) == nullptr;
    }

    // Query current transport routing (STATUS)

    // List available transports (LIST)
    void listAvailableTransports();

    // List all transports and their current assignments (old function, kept for compatibility)

    // ========== Update Loop ==========

    // Poll transports, handle incoming data (called each loop)
    void update();

private:
    // Process incoming commands from control plane transports
    void processIncomingCommands();

    // Read and process characters from a specific transport
    void processCommandFromTransport(TransportInterface* transport);
};

// Global router instance
extern MessageRouter router;

#endif // MESSAGE_ROUTER_H
