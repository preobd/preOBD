/*
 * message_router.cpp - Message Routing Engine Implementation
 */

#include "message_router.h"
#include "message_api.h"
#include "system_config.h"
#include "serial_manager.h"
#include "../inputs/serial_config.h"
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
    // Load from SystemConfig.router (persisted in EEPROM)
    primaryTransport[PLANE_CONTROL] = systemConfig.router.control_primary;
    primaryTransport[PLANE_DATA] = systemConfig.router.data_primary;
    primaryTransport[PLANE_DEBUG] = systemConfig.router.debug_primary;

    secondaryTransport[PLANE_CONTROL] = systemConfig.router.control_secondary;
    secondaryTransport[PLANE_DATA] = systemConfig.router.data_secondary;
    secondaryTransport[PLANE_DEBUG] = systemConfig.router.debug_secondary;

    // Load log filter configuration from SystemConfig.logFilter
    logFilter.setLevel(PLANE_CONTROL, (LogLevel)systemConfig.logFilter.control_level);
    logFilter.setLevel(PLANE_DATA, (LogLevel)systemConfig.logFilter.data_level);
    logFilter.setLevel(PLANE_DEBUG, (LogLevel)systemConfig.logFilter.debug_level);
    logFilter.setEnabledTags(systemConfig.logFilter.enabledTags);
}

void MessageRouter::syncConfig() {
    // Copy router state to SystemConfig.router (does NOT save to EEPROM)
    systemConfig.router.control_primary = primaryTransport[PLANE_CONTROL];
    systemConfig.router.control_secondary = secondaryTransport[PLANE_CONTROL];
    systemConfig.router.data_primary = primaryTransport[PLANE_DATA];
    systemConfig.router.data_secondary = secondaryTransport[PLANE_DATA];
    systemConfig.router.debug_primary = primaryTransport[PLANE_DEBUG];
    systemConfig.router.debug_secondary = secondaryTransport[PLANE_DEBUG];

    // Copy log filter state to SystemConfig.logFilter
    systemConfig.logFilter.control_level = logFilter.getLevel(PLANE_CONTROL);
    systemConfig.logFilter.data_level = logFilter.getLevel(PLANE_DATA);
    systemConfig.logFilter.debug_level = logFilter.getLevel(PLANE_DEBUG);
    systemConfig.logFilter.enabledTags = logFilter.getEnabledTags();
}

void MessageRouter::saveConfig() {
    // Sync state and persist to EEPROM
    syncConfig();
    saveSystemConfig();
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
            if (!isSerialPortActive(port_id)) {
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
                if (!isSerialPortActive(port_id)) continue;
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
                if (!isSerialPortActive(port_id)) continue;
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
    // Poll primary control transport
    TransportInterface* ctrl = getTransport(PLANE_CONTROL, true);
    if (ctrl && ctrl->isConnected() && ctrl->available()) {
        setActiveControlTransport(ctrl);
        processCommandFromTransport(ctrl);
    }

    // Poll secondary control transport (if configured)
    TransportInterface* ctrl2 = getTransport(PLANE_CONTROL, false);
    if (ctrl2 && ctrl2->isConnected() && ctrl2->available()) {
        setActiveControlTransport(ctrl2);
        processCommandFromTransport(ctrl2);
    }

    // Process received characters (embedded-cli needs this after receiving chars)
    processSerialCommands();
}

void MessageRouter::processCommandFromTransport(TransportInterface* transport) {
    // Read and process all available characters
    while (transport->available()) {
        char c = transport->read();
        handleCommandInput(c);
    }
}
