/*
 * transport_registry.cpp - see transport_registry.h
 */

#include "transport_registry.h"
#include "../core/message_api.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

static TransportInterface* s_transports[NUM_TRANSPORTS] = {};
static SerialPortActiveProbe s_serialPortActiveProbe = nullptr;

void setSerialPortActiveProbe(SerialPortActiveProbe probe) {
    s_serialPortActiveProbe = probe;
}

bool serialTransportPortActive(uint8_t port_id) {
    if (s_serialPortActiveProbe == nullptr) return true;   // fail permissive (header)
    return s_serialPortActiveProbe(port_id);
}

bool transportPortStarted(uint8_t id) {
    if (id < TRANSPORT_SERIAL1 || id > TRANSPORT_SERIAL8) return true;
    return serialTransportPortActive(id - TRANSPORT_SERIAL1 + 1);
}

void transportRegister(TransportID id, TransportInterface* transport) {
    if (id > 0 && id < NUM_TRANSPORTS && transport != nullptr) {
        s_transports[id] = transport;
    }
}

TransportInterface* transportRegistered(uint8_t id) {
    return id < NUM_TRANSPORTS ? s_transports[id] : nullptr;
}

TransportInterface* transportGet(uint8_t id) {
    if (id == TRANSPORT_NONE || id >= NUM_TRANSPORTS) return nullptr;
    if (!transportPortStarted(id)) return nullptr;
    return s_transports[id];
}

FLASHMEM void transportsBegin() {
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        if (s_transports[i] != nullptr) s_transports[i]->begin();
    }
}

void transportsUpdate() {
    for (int i = 0; i < NUM_TRANSPORTS; i++) {
        if (s_transports[i] != nullptr && transportPortStarted(i)) {
            s_transports[i]->update();
        }
    }
}

FLASHMEM void transportListAvailable() {
    msg.control.println(F("=== Available Transports ==="));

    const char* transportNames[] = {"NONE", "USB_SERIAL", "SERIAL1", "SERIAL2", "SERIAL3",
                                    "SERIAL4", "SERIAL5", "SERIAL6", "SERIAL7", "SERIAL8", "ESP32_BT"};

    for (int i = 1; i < NUM_TRANSPORTS; i++) {
        if (s_transports[i] == nullptr) continue;
        // Skip disabled serial ports (TRANSPORT_SERIAL1=2 maps to port_id=1, etc.)
        if (!transportPortStarted(i)) continue;

        msg.control.print(F("  "));
        msg.control.print(transportNames[i]);
        msg.control.print(F(" - "));

        switch (s_transports[i]->getState()) {
            case TRANSPORT_CONNECTED:    msg.control.println(F("Connected")); break;
            case TRANSPORT_DISCONNECTED: msg.control.println(F("Disconnected")); break;
            case TRANSPORT_CONNECTING:   msg.control.println(F("Connecting")); break;
            case TRANSPORT_ERROR:        msg.control.println(F("Error")); break;
        }
    }
}
