/*
 * console_routing.cpp - see console_routing.h
 */

#include "console_routing.h"
#include "../transports/transport_registry.h"
#include "../config/system_config.h"        // systemConfig, applyLogFilterConfig
#include "../core/message_api.h"            // msg.filter

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// Constant-initialised to the old router's defaults, so the console and the
// log have USB from the first byte of boot, before routingApplyConfig() runs
static uint8_t s_primary[NUM_PLANES]   = {TRANSPORT_USB_SERIAL, TRANSPORT_USB_SERIAL};
static uint8_t s_secondary[NUM_PLANES] = {TRANSPORT_NONE, TRANSPORT_NONE};
static TransportInterface* s_active = nullptr;   // the source of the running command

// Neither console transport is usable (one was a serial port now disabled)
static bool consoleUnusable() {
    return transportGet(s_primary[PLANE_CONTROL]) == nullptr && transportGet(s_secondary[PLANE_CONTROL]) == nullptr;
}

// A plane's configured transport, the console's primary with the USB fallback:
// the CLI must always be reachable, rather than boot a board nothing can talk
// to, or reset from the CLI (#558)
static TransportInterface* planeTransport(MessagePlane plane, bool secondary) {
    if (plane == PLANE_CONTROL && !secondary && consoleUnusable()) {
        return transportRegistered(TRANSPORT_USB_SERIAL);
    }
    return transportGet(secondary ? s_secondary[plane] : s_primary[plane]);
}

TransportInterface* consoleSink(bool secondary) {
    if (s_active) return secondary ? nullptr : s_active;   // a reply: the sender only
    return planeTransport(PLANE_CONTROL, secondary);
}

TransportInterface* logSink(bool secondary) {
    return planeTransport(PLANE_DEBUG, secondary);
}

TransportInterface* consoleActiveSource() { return s_active; }

void consoleSetActive(TransportInterface* t) { s_active = t; }

TransportInterface* consoleTransport(bool secondary) { return planeTransport(PLANE_CONTROL, secondary); }

bool consoleOnUsbFallback() { return consoleUnusable(); }

TransportID planeTransportId(MessagePlane plane, bool secondary) {
    if (plane >= NUM_PLANES) return TRANSPORT_NONE;
    return (TransportID)(secondary ? s_secondary[plane] : s_primary[plane]);
}

// No range check: an out-of-range stored byte is kept as it is and reads as
// no transport (transportGet), as the router's load did
static void planeSetUnchecked(MessagePlane plane, uint8_t id, bool secondary) {
    if (plane >= NUM_PLANES) return;
    (secondary ? s_secondary : s_primary)[plane] = id;
}

bool planeSetTransport(MessagePlane plane, TransportID id, bool secondary) {
    if (plane >= NUM_PLANES || id >= NUM_TRANSPORTS) return false;
    // Registered, and a serial port must be enabled (BUS SERIAL <n> ENABLE first)
    if (id != TRANSPORT_NONE && (transportRegistered(id) == nullptr || !transportPortStarted(id))) return false;
    planeSetUnchecked(plane, id, secondary);
    return true;
}

FLASHMEM void routingApplyConfig() {
    planeSetUnchecked(PLANE_CONTROL, systemConfig.console.primary,   false);
    planeSetUnchecked(PLANE_DEBUG,   systemConfig.log.primary,       false);
    planeSetUnchecked(PLANE_CONTROL, systemConfig.console.secondary, true);
    planeSetUnchecked(PLANE_DEBUG,   systemConfig.log.secondary,     true);
    applyLogFilterConfig();
}

FLASHMEM void routingCaptureConfig() {
    systemConfig.console.primary   = s_primary[PLANE_CONTROL];
    systemConfig.console.secondary = s_secondary[PLANE_CONTROL];
    systemConfig.log.primary       = s_primary[PLANE_DEBUG];
    systemConfig.log.secondary     = s_secondary[PLANE_DEBUG];

    systemConfig.logFilter.console_level = msg.filter.getLevel(PLANE_CONTROL);
    systemConfig.logFilter.debug_level   = msg.filter.getLevel(PLANE_DEBUG);
    systemConfig.logFilter.enabledTags   = msg.filter.getEnabledTags();
}
