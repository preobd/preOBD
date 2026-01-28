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
    PLANE_CONTROL = 0,  // Interactive commands, configuration responses
    PLANE_DATA    = 1,  // Sensor data streams (CSV, RealDash binary)
    PLANE_DEBUG   = 2,  // Debug/diagnostic messages
    NUM_PLANES    = 3
};

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

    // Log filtering (runtime configurable)
    LogFilter logFilter;

public:
    MessageRouter();

    // ========== Initialization ==========

    void begin();

    // Register a transport backend
    void registerTransport(TransportID id, TransportInterface* transport);

    // Load configuration from EEPROM (called by begin())
    void loadConfig();

    // Sync router state to SystemConfig (without saving to EEPROM)
    void syncConfig();

    // Save configuration to EEPROM
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

    // Query current transport routing (STATUS)
    void printTransportStatus();

    // List available transports (LIST)
    void listAvailableTransports();

    // List all transports and their current assignments (old function, kept for compatibility)
    void listTransports();

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
