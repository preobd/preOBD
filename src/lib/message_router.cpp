/*
 * message_router.cpp - Message Routing Engine Implementation
 */

#include "message_router.h"
#include "system_config.h"
#include <string.h>

// Global router instance
MessageRouter router;

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

void MessageRouter::loadConfig() {
    // TODO: Load from SystemConfig.router once EEPROM struct is extended
    // For now, use defaults (all planes → USB Serial)
    primaryTransport[PLANE_CONTROL] = TRANSPORT_USB_SERIAL;
    primaryTransport[PLANE_DATA] = TRANSPORT_USB_SERIAL;
    primaryTransport[PLANE_DEBUG] = TRANSPORT_USB_SERIAL;

    secondaryTransport[PLANE_CONTROL] = TRANSPORT_NONE;
    secondaryTransport[PLANE_DATA] = TRANSPORT_NONE;
    secondaryTransport[PLANE_DEBUG] = TRANSPORT_NONE;
}

void MessageRouter::saveConfig() {
    // TODO: Save to SystemConfig.router and call saveSystemConfig()
    // Will be implemented when system_config.h is extended
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
    }

    // Update mapping
    if (secondary) {
        secondaryTransport[plane] = transportId;
    } else {
        primaryTransport[plane] = transportId;
    }

    return true;
}

void MessageRouter::listTransports() {
    // This will be implemented after msg.control is available
    // For now, just a placeholder
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (!ctrl) return;

    ctrl->println(F("=== Transport Configuration ==="));

    // Show plane assignments
    const char* planeNames[] = {"CONTROL", "DATA", "DEBUG"};
    const char* transportNames[] = {"NONE", "USB_SERIAL", "SERIAL1", "SERIAL2", "SERIAL3", "BLUETOOTH"};

    for (int i = 0; i < NUM_PLANES; i++) {
        ctrl->print(planeNames[i]);
        ctrl->print(F(" → "));

        uint8_t tid = primaryTransport[i];
        if (tid < NUM_TRANSPORTS) {
            ctrl->print(transportNames[tid]);
        } else {
            ctrl->print(F("UNKNOWN"));
        }

        if (secondaryTransport[i] != TRANSPORT_NONE) {
            ctrl->print(F(" + "));
            tid = secondaryTransport[i];
            if (tid < NUM_TRANSPORTS) {
                ctrl->print(transportNames[tid]);
            }
        }

        ctrl->println();
    }

    ctrl->println();
    ctrl->println(F("Available transports:"));

    // List all registered transports
    for (int i = 1; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            ctrl->print(F("  "));
            ctrl->print(transports[i]->getName());
            ctrl->print(F(" ("));

            if (transports[i]->getCapabilities() & CAP_HARDWARE_SERIAL) {
                ctrl->print(F("Hardware"));
            } else if (transports[i]->getCapabilities() & CAP_VIRTUAL) {
                ctrl->print(F("Virtual"));
            } else {
                ctrl->print(F("Unknown"));
            }

            ctrl->print(F(", "));

            switch (transports[i]->getState()) {
                case TRANSPORT_CONNECTED:
                    ctrl->print(F("Connected"));
                    break;
                case TRANSPORT_DISCONNECTED:
                    ctrl->print(F("Disconnected"));
                    break;
                case TRANSPORT_CONNECTING:
                    ctrl->print(F("Connecting"));
                    break;
                case TRANSPORT_ERROR:
                    ctrl->print(F("Error"));
                    break;
            }

            ctrl->println(F(")"));
        }
    }
}

void MessageRouter::update() {
    // Poll all transports for housekeeping
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        if (transports[i] != nullptr) {
            transports[i]->update();
        }
    }

    // TODO: Check for incoming commands on control plane transports
    // This will be integrated with serial_config.cpp command handling
}
