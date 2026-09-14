/*
 * message_router.cpp - Message Routing Engine Implementation
 */

#include "message_router.h"
#include "../core/message_api.h"
#include <string.h>

// Global router instance
MessageRouter router;

// Serial-port-active probe (see header comment above setSerialPortActiveProbe()).
static SerialPortActiveProbe serialPortActiveProbe = nullptr;

void setSerialPortActiveProbe(SerialPortActiveProbe probe) {
    serialPortActiveProbe = probe;
}

// Local wrapper: applies the unregistered-probe default documented in the
// header (fail permissive -- see setSerialPortActiveProbe()'s comment).
static bool serialTransportPortActive(uint8_t port_id) {
    if (serialPortActiveProbe == nullptr) return true;
    return serialPortActiveProbe(port_id);
}

// Control-plane command sink (see header comment above setCommandSink()).
static CommandByteSink commandByteSink = nullptr;
static CommandDispatchFn commandDispatchFn = nullptr;

void setCommandSink(CommandByteSink byteSink, CommandDispatchFn dispatchFn) {
    commandByteSink = byteSink;
    commandDispatchFn = dispatchFn;
}

MessageRouter::MessageRouter() : activeControlTransport(nullptr) {
    // Initialize transport registry to NULL
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        transports[i] = nullptr;
    }

    // Initialize plane mappings to USB Serial (default)
    for (int i = 0; i < NUM_PLANES; i++) {
        primaryTransport[i] = TRANSPORT_USB_SERIAL;
        secondaryTransport[i] = TRANSPORT_NONE;
    }
}

void MessageRouter::begin() {
    // Load configuration from EEPROM (will be implemented once SystemConfig is extended)
    loadConfig();

    // Initialize all registered transports
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            transports[i]->begin();
        }
    }
}

void MessageRouter::registerTransport(TransportID id, TransportInterface* transport) {
    if (id > 0 && id < NUM_TRANSPORTS && transport != nullptr) {
        transports[id] = transport;
    }
}

// loadConfig() / syncConfig() / saveConfig() are declared in message_router.h
// but DEFINED in storage/system_config.cpp (#418) -- see the header comment
// there for why.

TransportID MessageRouter::getPrimaryTransportId(MessagePlane plane) const {
    if (plane >= NUM_PLANES) return TRANSPORT_NONE;
    return (TransportID)primaryTransport[plane];
}

TransportID MessageRouter::getSecondaryTransportId(MessagePlane plane) const {
    if (plane >= NUM_PLANES) return TRANSPORT_NONE;
    return (TransportID)secondaryTransport[plane];
}

void MessageRouter::setTransportUnchecked(MessagePlane plane, TransportID transportId, bool secondary) {
    // Deliberately no transportId range check: this mirrors the original
    // loadConfig()'s direct field assignment byte-for-byte, including its
    // handling of an out-of-range EEPROM value (getTransport() already
    // treats any transportId >= NUM_TRANSPORTS as "no transport", so a
    // corrupted stored byte behaves the same either way -- see #418 PR notes).
    if (plane >= NUM_PLANES) return;

    if (secondary) {
        secondaryTransport[plane] = transportId;
    } else {
        primaryTransport[plane] = transportId;
    }
}

TransportInterface* MessageRouter::getTransport(MessagePlane plane, bool primary) {
    if (plane >= NUM_PLANES) return nullptr;

    uint8_t transportId = primary ? primaryTransport[plane] : secondaryTransport[plane];

    if (transportId == TRANSPORT_NONE || transportId >= NUM_TRANSPORTS) {
        return nullptr;
    }

    return transports[transportId];
}

void MessageRouter::routeMessage(MessagePlane plane, const char* message) {
    if (!message) return;

    // Route to primary transport
    TransportInterface* primary = getTransport(plane, true);
    if (primary && primary->isConnected()) {
        primary->print(message);
    }

    // Route to secondary transport (multi-cast)
    TransportInterface* secondary = getTransport(plane, false);
    if (secondary && secondary->isConnected()) {
        secondary->print(message);
    }
}

void MessageRouter::routeMessage(MessagePlane plane, const uint8_t* data, size_t len) {
    if (!data || len == 0) return;

    // Route to primary transport
    TransportInterface* primary = getTransport(plane, true);
    if (primary && primary->isConnected() && primary->supportsBinary()) {
        primary->write(data, len);
    }

    // Route to secondary transport (multi-cast)
    TransportInterface* secondary = getTransport(plane, false);
    if (secondary && secondary->isConnected() && secondary->supportsBinary()) {
        secondary->write(data, len);
    }
}

void MessageRouter::setActiveControlTransport(TransportInterface* transport) {
    activeControlTransport = transport;
}

bool MessageRouter::setTransport(MessagePlane plane, TransportID transportId, bool secondary) {
    if (plane >= NUM_PLANES) return false;
    if (transportId >= NUM_TRANSPORTS) return false;

    // Check if transport exists and is available
    if (transportId != TRANSPORT_NONE) {
        if (transports[transportId] == nullptr) {
            return false;  // Transport not registered
        }

        // For hardware serial transports, verify the port is enabled
        if (transportId >= TRANSPORT_SERIAL1 && transportId <= TRANSPORT_SERIAL8) {
            uint8_t port_id = transportId - TRANSPORT_SERIAL1 + 1;
            if (!serialTransportPortActive(port_id)) {
                return false;  // Serial port not enabled (use BUS SERIAL <n> ENABLE first)
            }
        }
    }

    // Update mapping
    if (secondary) {
        secondaryTransport[plane] = transportId;
    } else {
        primaryTransport[plane] = transportId;
    }

    return true;
}

void MessageRouter::printTransportStatus() {
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (!ctrl) return;

    msg.control.println(F("=== Transport Routing ==="));

    const char* planeNames[] = {"CONTROL", "DATA", "DEBUG"};
    const char* transportNames[] = {"NONE", "USB_SERIAL", "SERIAL1", "SERIAL2", "SERIAL3",
                                    "SERIAL4", "SERIAL5", "SERIAL6", "SERIAL7", "SERIAL8", "ESP32_BT"};

    for (int i = 0; i < NUM_PLANES; i++) {
        msg.control.print(planeNames[i]);
        msg.control.print(F(" → "));

        uint8_t tid = primaryTransport[i];
        if (tid < NUM_TRANSPORTS) {
            msg.control.print(transportNames[tid]);
        } else {
            msg.control.print(F("UNKNOWN"));
        }

        if (secondaryTransport[i] != TRANSPORT_NONE) {
            msg.control.print(F(" + "));
            tid = secondaryTransport[i];
            if (tid < NUM_TRANSPORTS) {
                msg.control.print(transportNames[tid]);
            }
        }

        msg.control.println();
    }
}

void MessageRouter::listAvailableTransports() {
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (!ctrl) return;

    msg.control.println(F("=== Available Transports ==="));

    const char* transportNames[] = {"NONE", "USB_SERIAL", "SERIAL1", "SERIAL2", "SERIAL3",
                                    "SERIAL4", "SERIAL5", "SERIAL6", "SERIAL7", "SERIAL8", "ESP32_BT"};

    for (int i = 1; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            // Skip disabled serial ports (TRANSPORT_SERIAL1=2 maps to port_id=1, etc.)
            if (i >= TRANSPORT_SERIAL1 && i <= TRANSPORT_SERIAL8) {
                uint8_t port_id = i - TRANSPORT_SERIAL1 + 1;
                if (!serialTransportPortActive(port_id)) continue;
            }

            msg.control.print(F("  "));
            msg.control.print(transportNames[i]);
            msg.control.print(F(" - "));

            switch (transports[i]->getState()) {
                case TRANSPORT_CONNECTED:
                    msg.control.println(F("Connected"));
                    break;
                case TRANSPORT_DISCONNECTED:
                    msg.control.println(F("Disconnected"));
                    break;
                case TRANSPORT_CONNECTING:
                    msg.control.println(F("Connecting"));
                    break;
                case TRANSPORT_ERROR:
                    msg.control.println(F("Error"));
                    break;
            }
        }
    }
}

void MessageRouter::listTransports() {

    // This will be implemented after msg.control is available
    // For now, just a placeholder
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (!ctrl) return;

    msg.control.println(F("=== Transport Configuration ==="));

    // Show plane assignments
    const char* planeNames[] = {"CONTROL", "DATA", "DEBUG"};
    const char* transportNames[] = {"NONE", "USB_SERIAL", "SERIAL1", "SERIAL2", "SERIAL3",
                                    "SERIAL4", "SERIAL5", "SERIAL6", "SERIAL7", "SERIAL8", "ESP32_BT"};

    for (int i = 0; i < NUM_PLANES; i++) {
        msg.control.print(planeNames[i]);
        msg.control.print(F(" → "));

        uint8_t tid = primaryTransport[i];
        if (tid < NUM_TRANSPORTS) {
            msg.control.print(transportNames[tid]);
        } else {
            msg.control.print(F("UNKNOWN"));
        }

        if (secondaryTransport[i] != TRANSPORT_NONE) {
            msg.control.print(F(" + "));
            tid = secondaryTransport[i];
            if (tid < NUM_TRANSPORTS) {
                msg.control.print(transportNames[tid]);
            }
        }

        msg.control.println();
    }

    msg.control.println();
    msg.control.println(F("Available transports:"));

    // List all registered transports
    for (int i = 1; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            // Skip disabled serial ports (TRANSPORT_SERIAL1=2 maps to port_id=1, etc.)
            if (i >= TRANSPORT_SERIAL1 && i <= TRANSPORT_SERIAL8) {
                uint8_t port_id = i - TRANSPORT_SERIAL1 + 1;
                if (!serialTransportPortActive(port_id)) continue;
            }

            msg.control.print(F("  "));
            msg.control.print(transports[i]->getName());
            msg.control.print(F(" ("));

            if (transports[i]->getCapabilities() & CAP_HARDWARE_SERIAL) {
                msg.control.print(F("Hardware"));
            } else if (transports[i]->getCapabilities() & CAP_VIRTUAL) {
                msg.control.print(F("Virtual"));
            } else {
                msg.control.print(F("Unknown"));
            }

            msg.control.print(F(", "));

            switch (transports[i]->getState()) {
                case TRANSPORT_CONNECTED:
                    msg.control.print(F("Connected"));
                    break;
                case TRANSPORT_DISCONNECTED:
                    msg.control.print(F("Disconnected"));
                    break;
                case TRANSPORT_CONNECTING:
                    msg.control.print(F("Connecting"));
                    break;
                case TRANSPORT_ERROR:
                    msg.control.print(F("Error"));
                    break;
            }

            msg.control.println(F(")"));
        }
    }
}

void MessageRouter::update() {
    // NOTE: the JSON-import idle sweep used to run here, first thing every
    // tick. It is periodic work that needs a heartbeat, not routing -- it
    // lived here only because the router happens to tick. Moved to loop()
    // in main.cpp, which owns periodic work (#418).

    // Poll all transports for housekeeping
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            transports[i]->update();
        }
    }

    // Process incoming commands from control plane transports
    processIncomingCommands();
}

void MessageRouter::processIncomingCommands() {
    // Drain each control transport independently: set the active transport,
    // feed bytes into the CLI buffer, then dispatch before moving to the next
    // port.  This ensures responses always go to the sender even when both
    // ports have bytes ready in the same loop tick.
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (ctrl && ctrl->isConnected() && ctrl->available()) {
        setActiveControlTransport(ctrl);
        processCommandFromTransport(ctrl);
        if (commandDispatchFn != nullptr) commandDispatchFn();
    }

    TransportInterface* ctrl2 = getTransport(PLANE_CONTROL, false);
    if (ctrl2 && ctrl2->isConnected() && ctrl2->available()) {
        setActiveControlTransport(ctrl2);
        processCommandFromTransport(ctrl2);
        if (commandDispatchFn != nullptr) commandDispatchFn();
    }

    // Clear after dispatch so unsolicited msg.control output (alarms, background
    // notifications) falls back to normal primary + secondary multi-cast instead
    // of sticking to whoever sent the last command.
    setActiveControlTransport(nullptr);
}

void MessageRouter::processCommandFromTransport(TransportInterface* transport) {
    // Unregistered sink: don't drain. Bytes stay buffered in the transport
    // rather than being read and discarded -- see setCommandSink()'s header
    // comment for why this direction was chosen over the alternative.
    if (commandByteSink == nullptr) return;

    // Read and process all available characters
    while (transport->available()) {
        char c = transport->read();
        commandByteSink(c);
    }
}
