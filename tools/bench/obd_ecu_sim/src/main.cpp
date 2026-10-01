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
 * #509 units (fixed values, J1979 formulas):
 *   0x10 MAF 12.34 g/s   0x1F run time = uptime s   0x21 dist MIL 42 km
 *   0x31 dist since clear 1500 km   0x4D time MIL 90 min   0x5E fuel 5.5 L/h
 *   0x66 MAF sensor A 20 g/s (A = support byte 01)
 *   0x67 ECT sensor 1 90 C (A = support byte 01)   0xA6 odometer 123456.7 km
 *   0x00/0x20/... supported-PID bitmaps of the above, minus the unsupported
 *   PID (#511); a bitmap block with nothing above it is treated like the
 *   unsupported PID (silence, or NRC with `n`).
 * Anything else is ignored, like a real ECU.
 *
 * With `e`, a second ECU (transmission, 0x7E9, physical 0x7E1) also answers:
 *   0x00 bitmap: 0x05 + continuation   0x20 bitmap: 0x21
 *   0x05 Trans temp 70 C, 0x21 distance 1234 km
 * It exercises SCAN PIDS's multi-ECU collection and block walk.
 *
 * Serial commands (115200, end with Enter):
 *   h            help
 *   d <ms>       answer delay (0 = immediate)
 *   u <hex>      PID to treat as unsupported (00 = none), e.g. "u 0F"
 *   n            toggle unsupported PID: silence <-> NRC 7F 01 31
 *   s            toggle silent (ECU off: no answers, still counts requests)
 *   l            toggle listen-only: the MCP2515 stops ACKing, like an
 *                unpowered ECU, and counts every retransmission of preOBD's
 *                unACKed request (#506). Also silent while on.
 *   e            toggle the second ECU (0x7E9)
 *   r            print stats now
 *   z            zero counters
 *   q <hex>      act as a TESTER: send 02 01 <pid> to 0x7DF and print any
 *                0x7E8 answer for 200 ms (tests preOBD's own OBD responder).
 *                A multi-frame answer (First Frame 1x) gets Flow Control
 *                30 00 00 on 0x7E0, and its Consecutive Frames print too (#516).
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
static bool     listenOnly = false;
static bool     secondEcu = false;

// ---- one queued answer per ECU (delay is non-blocking) ----
static bool      replyQueued = false;
static uint32_t  replyDueMs;
static can_frame replyFrame;
static bool      reply2Queued = false;
static uint32_t  reply2DueMs;
static can_frame reply2Frame;

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
    Serial.print(listenOnly ? F("LISTEN-ONLY (no ACK)") : silent ? F("SILENT") : F("answering"));
    Serial.println(secondEcu ? F("  ecu2=0x7E9 on") : F("  ecu2=off"));
}

static void printHelp() {
    Serial.println(F("h help | d <ms> answer delay | u <hex> unsupported PID (00=none)"));
    Serial.println(F("n toggle unsupported: silence/NRC | s toggle ECU silent | e toggle 2nd ECU 0x7E9"));
    Serial.println(F("l toggle listen-only (no ACK: unpowered ECU) | r stats | z zero"));
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

    static const uint8_t answered[] = {0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x1F, 0x21,
                                       0x31, 0x4D, 0x5E, 0x66, 0x67, 0xA6};
    if ((pid & 0x1F) == 0) {    // supported-PID bitmap for (pid, pid+0x20]
        bool any = false, more = false;
        uint8_t bits[4] = {0, 0, 0, 0};
        for (uint8_t p : answered) {
            if (p == unsupportedPid || p <= pid) continue;
            any = true;
            if (p > pid + 0x20) { more = true; continue; }
            uint8_t i = p - pid - 1;
            bits[i / 8] |= (uint8_t)(0x80 >> (i % 8));
        }
        if (!any) {             // nothing above this block: as unsupported
            if (!unsupportedSendsNrc) return false;
            f.data[0] = 0x03;
            f.data[1] = 0x7F;
            f.data[2] = 0x01;
            f.data[3] = 0x31;
            return true;
        }
        if (more) bits[3] |= 0x01;   // PID pid+0x20 is the next bitmap
        f.data[0] = 0x06;
        f.data[1] = 0x41;
        f.data[2] = pid;
        memcpy(&f.data[3], bits, 4);
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
        case 0x10:              // MAF 12.34 g/s: (256A+B)/100
            f.data[0] = 0x04; f.data[3] = 1234 >> 8; f.data[4] = 1234 & 0xFF;
            return true;
        case 0x1F: {            // run time since start: uptime, s
            uint16_t t = (uint16_t)(now / 1000);
            f.data[0] = 0x04; f.data[3] = t >> 8; f.data[4] = t & 0xFF;
            return true;
        }
        case 0x21:              // distance with MIL on: 42 km
            f.data[0] = 0x04; f.data[4] = 42;
            return true;
        case 0x31:              // distance since codes cleared: 1500 km
            f.data[0] = 0x04; f.data[3] = 1500 >> 8; f.data[4] = 1500 & 0xFF;
            return true;
        case 0x4D:              // time run with MIL on: 90 min
            f.data[0] = 0x04; f.data[4] = 90;
            return true;
        case 0x5E:              // fuel rate 5.5 L/h: (256A+B)/20
            f.data[0] = 0x04; f.data[4] = 110;
            return true;
        case 0x66:              // MAF: A = support (sensor A), sensor A 20 g/s = 640/32
            f.data[0] = 0x07; f.data[3] = 0x01; f.data[4] = 640 >> 8; f.data[5] = 640 & 0xFF;
            return true;
        case 0x67:              // ECT: A = support (sensor 1), B = 90 + 40
            f.data[0] = 0x05; f.data[3] = 0x01; f.data[4] = 130;
            return true;
        case 0xA6: {            // odometer 123456.7 km: 32-bit / 10
            uint32_t o = 1234567;
            f.data[0] = 0x06; f.data[3] = o >> 24; f.data[4] = o >> 16; f.data[5] = o >> 8; f.data[6] = o;
            return true;
        }
        default:
            return false;
    }
}

// The second ECU's answer (0x7E9); false if it ignores `pid`.
static bool buildAnswer2(uint8_t pid, can_frame& f) {
    memset(&f, 0, sizeof(f));
    f.can_id = 0x7E9;
    f.can_dlc = 8;
    f.data[1] = 0x41;
    f.data[2] = pid;
    switch (pid) {
        case 0x00:              // 0x05 + continuation (0x20)
            f.data[0] = 0x06;
            f.data[3] = 0x08;
            f.data[6] = 0x01;
            return true;
        case 0x20:              // 0x21
            f.data[0] = 0x06;
            f.data[3] = 0x80;
            return true;
        case 0x05:              // Trans temp 70 C
            f.data[0] = 0x03;
            f.data[3] = 70 + 40;
            return true;
        case 0x21:              // Distance with MIL on: 1234 km
            f.data[0] = 0x04;
            f.data[3] = 1234 >> 8;
            f.data[4] = 1234 & 0xFF;
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
            case 's': silent = !silent; replyQueued = false; reply2Queued = false; printSettings(); break;
            case 'l':
                listenOnly = !listenOnly;
                replyQueued = false; reply2Queued = false;
                if ((listenOnly ? mcp.setListenOnlyMode() : mcp.setNormalMode()) != MCP2515::ERROR_OK) {
                    Serial.println(F("[cfg] MODE CHANGE FAILED"));
                }
                printSettings();
                break;
            case 'e': secondEcu = !secondEcu; reply2Queued = false; printSettings(); break;
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
            if ((rx.data[0] & 0xF0) == 0x10) {   // First Frame: allow the rest, no gap
                can_frame fc;
                memset(&fc, 0, sizeof(fc));
                fc.can_id = 0x7E0;
                fc.can_dlc = 8;
                fc.data[0] = 0x30;
                bool ok = mcp.sendMessage(&fc) == MCP2515::ERROR_OK;
                Serial.println(ok ? F("[query] flow control 30 00 00 sent") : F("[query] FLOW CONTROL SEND FAILED"));
            }
            continue;
        }
        if (id != 0x7DF && id != 0x7E0 && id != 0x7E1) continue;
        // ISO-TP single frame: 02 01 <pid>
        if (rx.can_dlc < 3 || rx.data[0] != 0x02 || rx.data[1] != 0x01) continue;

        uint8_t pid = rx.data[2];
        countRequest(pid, now);
        if (silent || listenOnly) continue;

        // Engine: functional or 0x7E0. One answer in flight, like a busy ECU.
        if (id != 0x7E1 && !replyQueued && buildAnswer(pid, replyFrame, now)) {
            replyQueued = true;
            replyDueMs = now + answerDelayMs;
        }
        // Transmission: functional or 0x7E1, answering alongside the engine
        if (secondEcu && id != 0x7E0 && !reply2Queued && buildAnswer2(pid, reply2Frame)) {
            reply2Queued = true;
            reply2DueMs = now + answerDelayMs;
        }
    }

    if (replyQueued && (int32_t)(now - replyDueMs) >= 0) {
        replyQueued = false;
        if (mcp.sendMessage(&replyFrame) == MCP2515::ERROR_OK) answersSent++;
        else txErrors++;
    }
    if (reply2Queued && (int32_t)(now - reply2DueMs) >= 0) {
        reply2Queued = false;
        if (mcp.sendMessage(&reply2Frame) == MCP2515::ERROR_OK) answersSent++;
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
