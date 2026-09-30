/*
 * obd_ecu_sim - OBD-II Mode 01 ECU simulator on an MCP2515 (bench tool, #476)
 *
 * Answers Mode 01 requests on 0x7DF (functional) and 0x7E0 (physical) from
 * 0x7E8, like an engine ECU, with knobs for the cases preOBD's poller has to
 * survive: a slow ECU, an unsupported PID, a negative response, and an ECU
 * that goes silent. It counts every request it sees, so the request rate
 * (and "zero requests in LISTEN") is measured here, not inferred from
 * preOBD's side.
 *
 * Supported PIDs (values drift so updates are visible):
 *   0x0C RPM       800-3000 rpm   ((A*256+B)/4)
 *   0x05 Coolant   80-95 C        (A-40)
 *   0x0D Speed     0-100 km/h     (A)
 *   0x0F Intake    30 C           (A-40)   <- default "unsupported" PID
 *   0x0B MAP       101 kPa        (A)      <- fixed, checks kPa -> bar -> psi
 * Anything else is ignored, like a real ECU.
 *
 * Serial commands (115200, end with Enter):
 *   h            help
 *   d <ms>       answer delay (0 = immediate)
 *   u <hex>      PID to treat as unsupported (00 = none), e.g. "u 0F"
 *   n            toggle unsupported PID: silence <-> NRC 7F 01 31
 *   s            toggle silent (ECU off: no answers, still counts requests)
 *   r            print stats now
 *   z            zero counters
 *   q <hex>      act as a TESTER: send 02 01 <pid> to 0x7DF and print any
 *                0x7E8 answer for 200 ms (tests preOBD's own OBD responder)
 * Stats print every 5 s: requests per PID, overall rate, min/avg gap.
 */

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

#ifndef MCP_CS_PIN
#define MCP_CS_PIN 10
#endif
#ifndef MCP_CRYSTAL_MHZ
#define MCP_CRYSTAL_MHZ 16
#endif
#ifndef CAN_BITRATE_KBPS
#define CAN_BITRATE_KBPS 500
#endif

static MCP2515 mcp(MCP_CS_PIN);

// ---- behaviour knobs ----
static uint16_t answerDelayMs = 0;
static uint8_t  unsupportedPid = 0x0F;
static bool     unsupportedSendsNrc = false;
static bool     silent = false;

// ---- one queued answer (delay is non-blocking) ----
static bool      replyQueued = false;
static uint32_t  replyDueMs;
static can_frame replyFrame;

// ---- stats ----
static const uint8_t TRACKED = 8;
static uint8_t  trackedPid[TRACKED];
static uint16_t trackedCount[TRACKED];
static uint8_t  trackedUsed = 0;
static uint16_t otherCount = 0;
static uint32_t totalRequests = 0;
static uint32_t answersSent = 0;
static uint32_t txErrors = 0;
static uint32_t windowStartMs = 0;
static uint32_t lastRequestMs = 0;
static uint32_t minGapMs = 0xFFFFFFFF;
static uint32_t gapSumMs = 0;
static uint32_t gapCount = 0;

// ---- tester mode (q <pid>) ----
static uint32_t queryUntilMs = 0;
static uint8_t  queryPid = 0;

static void zeroStats() {
    trackedUsed = 0;
    otherCount = 0;
    totalRequests = answersSent = txErrors = 0;
    minGapMs = 0xFFFFFFFF;
    gapSumMs = gapCount = 0;
    lastRequestMs = 0;
    windowStartMs = millis();
}

static void countRequest(uint8_t pid, uint32_t now) {
    totalRequests++;
    if (lastRequestMs != 0) {
        uint32_t gap = now - lastRequestMs;
        if (gap < minGapMs) minGapMs = gap;
        gapSumMs += gap;
        gapCount++;
    }
    lastRequestMs = now;

    for (uint8_t i = 0; i < trackedUsed; i++) {
        if (trackedPid[i] == pid) { trackedCount[i]++; return; }
    }
    if (trackedUsed < TRACKED) {
        trackedPid[trackedUsed] = pid;
        trackedCount[trackedUsed] = 1;
        trackedUsed++;
    } else {
        otherCount++;
    }
}

static void printHex2(uint8_t v) {
    if (v < 0x10) Serial.print('0');
    Serial.print(v, HEX);
}

static void printStats() {
    uint32_t elapsed = millis() - windowStartMs;
    Serial.print(F("[stats] "));
    Serial.print(elapsed / 1000.0, 1);
    Serial.print(F(" s  req="));
    Serial.print(totalRequests);
    Serial.print(F(" ("));
    Serial.print(elapsed ? totalRequests * 1000.0 / elapsed : 0.0, 1);
    Serial.print(F("/s)  ans="));
    Serial.print(answersSent);
    Serial.print(F("  txErr="));
    Serial.print(txErrors);
    Serial.print(F("  gap min/avg="));
    if (gapCount) {
        Serial.print(minGapMs);
        Serial.print('/');
        Serial.print(gapSumMs / gapCount);
        Serial.print(F(" ms"));
    } else {
        Serial.print(F("-"));
    }
    Serial.print(F("  per PID:"));
    for (uint8_t i = 0; i < trackedUsed; i++) {
        Serial.print(F(" 0x"));
        printHex2(trackedPid[i]);
        Serial.print('=');
        Serial.print(trackedCount[i]);
    }
    if (otherCount) {
        Serial.print(F(" other="));
        Serial.print(otherCount);
    }
    Serial.println();
}

static void printSettings() {
    Serial.print(F("[cfg] delay="));
    Serial.print(answerDelayMs);
    Serial.print(F(" ms  unsupported=0x"));
    printHex2(unsupportedPid);
    Serial.print(unsupportedSendsNrc ? F(" (NRC)") : F(" (silent)"));
    Serial.print(F("  ecu="));
    Serial.println(silent ? F("SILENT") : F("answering"));
}

static void printHelp() {
    Serial.println(F("h help | d <ms> answer delay | u <hex> unsupported PID (00=none)"));
    Serial.println(F("n toggle unsupported: silence/NRC | s toggle ECU silent | r stats | z zero"));
    Serial.println(F("q <hex> send a tester request to 0x7DF and print 0x7E8 answers"));
    printSettings();
}

// Fill `f` with the Mode 01 answer for `pid`; false if this ECU ignores it.
static bool buildAnswer(uint8_t pid, can_frame& f, uint32_t now) {
    memset(&f, 0, sizeof(f));
    f.can_id = 0x7E8;
    f.can_dlc = 8;

    if (pid == unsupportedPid && pid != 0x00) {
        if (!unsupportedSendsNrc) return false;
        f.data[0] = 0x03;       // NRC: 7F <service> <code>
        f.data[1] = 0x7F;
        f.data[2] = 0x01;
        f.data[3] = 0x31;       // requestOutOfRange
        return true;
    }

    // Slow triangle wave, 0..1000 over 20 s, so values visibly move
    uint32_t phase = (now / 20) % 2000;
    uint16_t tri = phase < 1000 ? phase : 2000 - phase;

    f.data[1] = 0x41;
    f.data[2] = pid;
    switch (pid) {
        case 0x0C: {            // RPM 800..3000
            uint16_t raw = (uint16_t)((800 + tri * 22 / 10) * 4);
            f.data[0] = 0x04;
            f.data[3] = raw >> 8;
            f.data[4] = raw & 0xFF;
            return true;
        }
        case 0x05:              // Coolant 80..95 C
            f.data[0] = 0x03;
            f.data[3] = (uint8_t)(80 + tri * 15 / 1000 + 40);
            return true;
        case 0x0D:              // Speed 0..100 km/h
            f.data[0] = 0x03;
            f.data[3] = (uint8_t)(tri / 10);
            return true;
        case 0x0B:              // MAP 101 kPa (14.65 psi)
            f.data[0] = 0x03;
            f.data[3] = 101;
            return true;
        case 0x0F:              // Intake 30 C
            f.data[0] = 0x03;
            f.data[3] = 30 + 40;
            return true;
        default:
            return false;
    }
}

static void handleSerial() {
    static char line[24];
    static uint8_t len = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (len < sizeof(line) - 1) line[len++] = c;
            continue;
        }
        if (len == 0) continue;
        line[len] = '\0';
        len = 0;

        const char* arg = line + 1;
        while (*arg == ' ') arg++;
        switch (line[0]) {
            case 'h': printHelp(); break;
            case 'd': answerDelayMs = (uint16_t)atoi(arg); printSettings(); break;
            case 'u': unsupportedPid = (uint8_t)strtoul(arg, nullptr, 16); printSettings(); break;
            case 'n': unsupportedSendsNrc = !unsupportedSendsNrc; printSettings(); break;
            case 's': silent = !silent; replyQueued = false; printSettings(); break;
            case 'r': printStats(); break;
            case 'z': zeroStats(); Serial.println(F("[stats] zeroed")); break;
            case 'q': {
                can_frame q;
                memset(&q, 0, sizeof(q));
                q.can_id = 0x7DF;
                q.can_dlc = 8;
                q.data[0] = 0x02;
                q.data[1] = 0x01;
                queryPid = (uint8_t)strtoul(arg, nullptr, 16);
                q.data[2] = queryPid;
                bool ok = mcp.sendMessage(&q) == MCP2515::ERROR_OK;
                queryUntilMs = millis() + 200;
                Serial.print(F("[query] 0x7DF 02 01 "));
                printHex2(queryPid);
                Serial.println(ok ? F(" sent") : F(" SEND FAILED"));
                break;
            }
            default:  Serial.println(F("? (h for help)")); break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    SPI.begin();

    CAN_SPEED speed = CAN_BITRATE_KBPS == 250 ? CAN_250KBPS
                    : CAN_BITRATE_KBPS == 125 ? CAN_125KBPS
                    : CAN_BITRATE_KBPS == 1000 ? CAN_1000KBPS
                    : CAN_500KBPS;
    CAN_CLOCK clock = MCP_CRYSTAL_MHZ == 8 ? MCP_8MHZ : MCP_16MHZ;

    mcp.reset();
    bool ok = mcp.setBitrate(speed, clock) == MCP2515::ERROR_OK &&
              mcp.setNormalMode() == MCP2515::ERROR_OK;

    Serial.println(F("obd_ecu_sim: OBD-II ECU on 0x7E8 (requests on 0x7DF/0x7E0)"));
    Serial.print(F("MCP2515 CS="));
    Serial.print(MCP_CS_PIN);
    Serial.print(F(" crystal="));
    Serial.print(MCP_CRYSTAL_MHZ);
    Serial.print(F(" MHz bitrate="));
    Serial.print(CAN_BITRATE_KBPS);
    Serial.println(ok ? F(" kbps: OK") : F(" kbps: INIT FAILED (check CS / crystal / wiring)"));
    printHelp();
    zeroStats();
}

void loop() {
    uint32_t now = millis();
    handleSerial();

    can_frame rx;
    while (mcp.readMessage(&rx) == MCP2515::ERROR_OK) {
        if (rx.can_id & CAN_EFF_FLAG) continue;
        uint32_t id = rx.can_id & CAN_SFF_MASK;
        if (queryUntilMs && id >= 0x7E8 && id <= 0x7EF) {
            Serial.print(F("[query] answer 0x"));
            Serial.print(id, HEX);
            Serial.print(':');
            for (uint8_t i = 0; i < rx.can_dlc; i++) { Serial.print(' '); printHex2(rx.data[i]); }
            Serial.println();
            continue;
        }
        if (id != 0x7DF && id != 0x7E0) continue;
        // ISO-TP single frame: 02 01 <pid>
        if (rx.can_dlc < 3 || rx.data[0] != 0x02 || rx.data[1] != 0x01) continue;

        uint8_t pid = rx.data[2];
        countRequest(pid, now);

        if (silent || replyQueued) continue;   // one answer in flight, like a busy ECU
        if (!buildAnswer(pid, replyFrame, now)) continue;
        replyQueued = true;
        replyDueMs = now + answerDelayMs;
    }

    if (replyQueued && (int32_t)(now - replyDueMs) >= 0) {
        replyQueued = false;
        if (mcp.sendMessage(&replyFrame) == MCP2515::ERROR_OK) answersSent++;
        else txErrors++;
    }

    if (queryUntilMs && (int32_t)(now - queryUntilMs) >= 0) {
        queryUntilMs = 0;
        Serial.println(F("[query] window closed"));
    }

    static uint32_t lastStatsMs = 0;
    if (now - lastStatsMs >= 5000) {
        lastStatsMs = now;
        printStats();
    }
}
