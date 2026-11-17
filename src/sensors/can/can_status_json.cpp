/*
 * can_status_json.cpp - see can_status_json.h
 */

#include <Arduino.h>   // FLASHMEM (Teensy); before the fallback below

#if SUPPORTS_JSON_EXPORT || defined(NATIVE_TEST)

#include "can_status_json.h"
#include "can_poll.h"   // isObdResponseId

#ifndef FLASHMEM
#define FLASHMEM
#endif

static const char* const SCAN_STATE_NAMES[] = {"idle", "listening", "done", "querying"};
static const char* const BUS_MODE_NAMES[] = {"OFF", "NORMAL", "LISTEN"};
static const char* const BUS_STATE_NAMES[] = {"off", "idle", "up", "waiting", "pinsTaken"};
static const uint32_t CAN_BAUDRATES[] = {125000, 250000, 500000, 1000000};

FLASHMEM void canScanStatusToJson(const CanScanSnapshot& s, JsonObject out) {
    out["state"] = SCAN_STATE_NAMES[s.state <= CAN_SCAN_JSON_QUERYING ? s.state : 0];
    out["scanId"] = s.scanId;
    out["durationMs"] = s.durationMs;
    out["remainingMs"] = s.remainingMs;
    out["found"] = s.found;
    // SCAN CAN only listens: it reports what is on the bus. SCAN PIDS asks
    // the ECUs (#511); `ecus` tells "no ECU answered" (ignition off) apart
    // from "answered, supports nothing". null for a passive scan.
    out["passive"] = s.passive;
    if (s.passive) out["ecus"] = nullptr;
    else           out["ecus"] = s.ecus;
}

FLASHMEM void canScanSupportedToJson(uint16_t canId, const uint8_t bits[32], JsonObject out) {
    out["canId"] = canId;
    JsonArray pids = out["pids"].to<JsonArray>();
    for (uint16_t p = 1; p <= 0xFF; p++) {
        if ((p & 0x1F) == 0) continue;   // bitmap PIDs
        if (bits[p >> 3] & (0x80 >> (p & 7))) pids.add(p);
    }
}

FLASHMEM void canScanEntryToJson(const CanScanEntrySnapshot& e, JsonObject out) {
    out["canId"] = e.canId;
    out["pid"] = e.pid;
    out["obd"] = isObdResponseId(e.canId);
    out["dataLength"] = e.dataLength;
    out["frames"] = e.frames;
    out["ageMs"] = e.ageMs;
    char hex[17];
    static const char digits[] = "0123456789ABCDEF";
    uint8_t n = e.dataLength > 8 ? 8 : e.dataLength;
    for (uint8_t i = 0; i < n; i++) {
        hex[2 * i]     = digits[e.data[i] >> 4];
        hex[2 * i + 1] = digits[e.data[i] & 0x0F];
    }
    hex[2 * n] = '\0';
    out["data"] = hex;   // copied: ArduinoJson duplicates a char* value
}

FLASHMEM JsonArray canBusStatusToJson(const CanBusSnapshot& s, JsonObject out) {
    JsonObject caps = out["caps"].to<JsonObject>();
    caps["buses"] = s.buses;
    JsonArray rates = caps["baudrates"].to<JsonArray>();
    for (uint32_t r : CAN_BAUDRATES) rates.add(r);
    caps["maxScanResults"] = s.maxScanResults;
    JsonArray scanMs = caps["scanMs"].to<JsonArray>();
    scanMs.add(1000);
    scanMs.add(60000);
    JsonArray pollMs = caps["pollMs"].to<JsonArray>();
    pollMs.add(CAN_POLL_MS_MIN);
    pollMs.add(CAN_POLL_MS_MAX);

    // One entry per bus this board has (#612 phase 2a)
    JsonArray buses = out["buses"].to<JsonArray>();
    for (uint8_t b = 0; b < s.buses && b < 3; b++) {
        const CanBusEntrySnapshot& e = s.bus[b];
        JsonObject o = buses.add<JsonObject>();
        o["bus"] = b;
        o["mode"] = BUS_MODE_NAMES[e.mode <= 2 ? e.mode : 0];
        o["baudrate"] = e.baudrate;
        o["state"] = BUS_STATE_NAMES[e.state <= 4 ? e.state : 0];
        o["input"] = e.input;
        o["obd"] = e.obd;
        o["pinsFree"] = e.pinsFree;
    }

    JsonObject in = out["input"].to<JsonObject>();
    in["bus"] = s.inBus;
    in["running"] = s.inRunning;
    in["pollMs"] = s.pollMs;
    in["framesRx"] = s.framesRx;
    if (s.haveFrame) in["lastFrameAgeMs"] = s.lastFrameAgeMs;
    else             in["lastFrameAgeMs"] = nullptr;
    JsonObject poll = in["poll"].to<JsonObject>();
    poll["active"] = s.pollActive;
    poll["backoff"] = s.pollBackoff;
    poll["missStreak"] = s.missStreak;
    poll["requests"] = s.requests;
    poll["answers"] = s.answers;
    poll["txFail"] = s.txFail;

    return out["imports"].to<JsonArray>();
}

FLASHMEM void canImportToJson(const CanImportSnapshot& i, JsonObject out) {
    out["pin"] = i.pin;
    out["pinLabel"] = i.pinLabel;
    out["sourceCanId"] = i.sourceCanId;
    out["sourcePid"] = i.sourcePid;
    out["dataOffset"] = i.dataOffset;
    out["polled"] = i.polled;
    if (i.haveFrame) out["ageMs"] = i.ageMs;
    else             out["ageMs"] = nullptr;
}

#endif // SUPPORTS_JSON_EXPORT || NATIVE_TEST
