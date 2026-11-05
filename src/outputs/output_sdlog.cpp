/*
 * output_sdlog.cpp - SD card data logging module
 *
 * Writes one CSV row per sample tick, one column per logged input:
 *
 *     millis,CLT_C,OILP_PSI,EGT_C
 *     14231,87.50,42.10,610.00
 *     19231,88.10,41.90,615.50
 *     24231,88.40,ERROR,617.20
 *
 * The column set is frozen when the file is opened. If the participating inputs
 * change mid-log (an input added or cleared, SD routing toggled, units changed)
 * the file is closed and the next one opened, rather than shifting columns
 * underneath an already-written header.
 *
 * Timestamps are millis() uptime — there is no RTC on any supported board — so
 * rows are correlatable within a session and sessions are distinguished by
 * filename, but not tied to wall-clock time.
 */

#include "output_base.h"
#include "../config.h"
#include "../sensors/sensor_library.h"
#include "../sensors/units_registry.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../core/system_mode.h"

#if INCLUDE_SD_LOGGING

// Use Arduino SD library for consistency across platforms
#include <SD.h>
#include "../storage/sd_manager.h"
#include "../storage/system_config.h"
#include "../core/watchdog.h"
#include "../core/hash.h"
#include "../inputs/input_manager.h"
#include "output_format.h"
#include "sd_status_json.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif

// SD object is provided globally by SD.h

// Highest sequence number expressible as LOG####.CSV.
#define SDLOG_MAX_FILE_INDEX 9999

// Rows a reopened file must produce before the logger trusts the card again
// and re-arms its single retry. Without this, a card that faults on every row
// would churn out a new file per row.
#define SDLOG_REOPEN_PROBATION_ROWS 60

// Longest gap between flush() calls. At the default 5 s sample interval this is
// effectively flush-per-row; it bounds the loss window on an abrupt power cut.
#define SDLOG_FLUSH_INTERVAL_MS 1000

static File     logFile;
static char     logFileName[13];        // 8.3 name + NUL
static uint16_t logFileIndex   = 0;     // last sequence number handed out
static uint32_t logRowsWritten = 0;
static uint16_t logWriteErrors = 0;
static bool     logFaulted     = false; // latched: stop touching the card
static bool     logCardRemoved = false; // why it latched: card gone, not bad write
static uint32_t columnSignature = 0;
static uint8_t  openFileFormat = SDLOG_FORMAT_WIDE;  // layout the open file was written with
static uint32_t lastRowMs      = 0;
static uint32_t lastFlushMs    = 0;
static bool     reopenUsed     = false;
static uint16_t rowsSinceReopen = 0;
static bool     wasInConfig    = false;
static uint64_t closedFileBytes = 0;   // sizes of this session's closed files (#536)

static void openLogFile();
static void handleCardRemoved();

/**
 * Does this slot contribute a column?
 *
 * Occupied, enabled, and routed to SD by its per-input mask. sendToOutputs()
 * used to apply the mask for us; now that the logger owns its own cadence
 * (see updateSDLog) it has to apply the mask itself, or
 * `SET <pin> OUTPUT SD_LOG DISABLE` would stop working.
 */
static inline bool logsThisInput(uint8_t slot) {
    return inputs[slot].pin != 0xFF &&
           inputs[slot].flags.isEnabled &&
           (inputs[slot].outputMask & (1 << OUTPUT_SD));
}

static uint8_t countLoggedInputs() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (logsThisInput(i)) n++;
    }
    return n;
}

/**
 * Cheap fingerprint of the current column set: which slots, in what order,
 * under what names and units. Any change means the header on disk no longer
 * describes the rows we would write.
 */
static uint32_t computeColumnSignature() {
    uint32_t sig = 5381;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (!logsThisInput(i)) continue;
        sig = sig * 31u + i;
        sig = sig * 31u + inputs[i].unitsIndex;
        sig = sig * 31u + djb2_hash(inputs[i].abbrName);
    }
    return sig;
}

static FLASHMEM void closeLogFile() {
    if (logFile) {
        // flush()/close() are card I/O and block for seconds on a card that has
        // gone away — this is a closing path, so it is exactly when the card is
        // most likely to be missing. Widen the watchdog around it (#331).
        SdWatchdogWindow window;
        logFile.flush();
        closedFileBytes += logFile.position();
        logFile.close();
    }
}

/**
 * Claim the next unused LOG####.CSV.
 *
 * Sequential rather than millis()-derived: millis() restarts at 0 every boot,
 * so the old scheme regenerated the same name after a power cycle and
 * FILE_WRITE appended, interleaving two drives in one file. It also produced a
 * 10-character basename once millis() reached 100000, which the SD library's
 * 8.3 short-name limit rejects outright — enabling logging more than ~100 s
 * after boot silently never started.
 *
 * logFileIndex persists across calls so a rotation or a re-enable resumes the
 * scan instead of restarting it.
 */
static FLASHMEM bool claimNextFileName(char* name, size_t len) {
    // A card holding thousands of logs makes this a long run of SD lookups;
    // widen the watchdog for the scan the same way initSD() does for begin().
    SdWatchdogWindow window;

    bool found = false;
    for (uint16_t i = logFileIndex + 1; i <= SDLOG_MAX_FILE_INDEX; i++) {
        snprintf(name, len, "LOG%04u.CSV", (unsigned)i);
        if (!SD.exists(name)) {
            logFileIndex = i;
            found = true;
            break;
        }
        watchdogReset();
    }

    return found;
}

static FLASHMEM void writeHeader() {
    // Long layout is self-describing per row, so its header is a fixed string —
    // byte-identical to what this module wrote before the wide format landed,
    // which is the whole point of keeping the mode available.
    if (openFileFormat == SDLOG_FORMAT_LONG) {
        logFile.println(F(SDLOG_LONG_HEADER));
        return;
    }

    logFile.print(F("millis"));

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (!logsThisInput(i)) continue;

        logFile.print(',');
        logFile.print(inputs[i].abbrName);

        char unitBuf[UNIT_FIELD_LEN];
        formatUnitSymbol(unitBuf, sizeof(unitBuf), getUnitStringByIndex(inputs[i].unitsIndex));
        if (unitBuf[0] != '\0') {
            logFile.print('_');
            // A symbol carrying a comma would split the column; none in the
            // registry does today, but the header must stay parseable.
            for (char* p = unitBuf; *p; p++) {
                logFile.print(*p == ',' ? '_' : *p);
            }
        }
    }

    logFile.println();
}

static FLASHMEM void openLogFile() {
    SdWatchdogWindow window;   // open/header/flush are all blocking card I/O

    closeLogFile();   // never leak the previous handle over a re-init

    if (!isSDInitialized()) {
        // Latch rather than warn once per sample interval for the rest of the
        // drive. The card is only ever mounted by initSD() at boot, so it
        // cannot appear later; re-enabling the output resets this.
        msg.debug.warn(TAG_SD, "SD logging failed - SD card not initialized");
        logFaulted = true;
        return;
    }

    // Built locally and copied in only once the file exists, so a failed open
    // never leaves SYSTEM SD naming a file preOBD did not create (#536).
    char name[sizeof(logFileName)];
    if (!claimNextFileName(name, sizeof(name))) {
        msg.debug.error(TAG_SD, "SD log names exhausted (LOG%04u.CSV) - logging halted",
                        (unsigned)SDLOG_MAX_FILE_INDEX);
        logFaulted = true;
        return;
    }

    logFile = SD.open(name, FILE_WRITE);
    if (!logFile) {
        msg.debug.error(TAG_SD, "Failed to create log file %s", name);
        logFaulted = true;
        return;
    }
    memcpy(logFileName, name, sizeof(logFileName));

    // Latch the layout for this file's lifetime — writeHeader() branches on it,
    // and a file cannot hold both layouts.
    openFileFormat  = systemConfig.sdLogFormat;
    writeHeader();
    columnSignature = computeColumnSignature();
    logFile.flush();
    lastFlushMs = millis();

    msg.debug.info(TAG_SD, "Logging to: %s (%s)", logFileName,
                   sdLogFormatName(openFileFormat));
}

/**
 * A write failed. Try to recover once, then stop.
 *
 * Previously nothing checked at all: print() return values were ignored and
 * getWriteError() was never read, so a full or removed card meant the logger
 * wrote nothing for the rest of the drive while OUTPUT STATUS still reported
 * "Enabled".
 */
static FLASHMEM void handleWriteError() {
    logWriteErrors++;
    logFile.clearWriteError();
    closeLogFile();

    // Tell "card yanked" apart from "write failed on a card that is still
    // here": a reopen needs a card to reopen onto, and burning a filename scan
    // on an empty slot is seconds of blocked loop() for nothing.
    if (!sdMediaPresent()) {
        handleCardRemoved();
        return;
    }

    if (reopenUsed) {
        logFaulted = true;
        msg.debug.error(TAG_SD, "SD write failed again (%u errors) - logging halted",
                        (unsigned)logWriteErrors);
        return;
    }

    msg.debug.error(TAG_SD, "SD write failed (%u) - reopening", (unsigned)logWriteErrors);
    reopenUsed = true;
    rowsSinceReopen = 0;
    openLogFile();

    if (!logFile) {
        logFaulted = true;
        msg.debug.error(TAG_SD, "SD reopen failed - logging halted");
    }
}

/**
 * The card is no longer in the slot. Stop touching it, permanently.
 *
 * Distinct from handleWriteError()'s reopen-then-latch: a reopen needs a card
 * to reopen onto, and the SD library only ever mounts one at boot, so a removal
 * mid-drive is terminal for this session. Latching here is also what keeps the
 * engine running — the alternative is every subsequent tick spending seconds
 * blocked in the SD library while gauges, alarms and OBD output wait.
 */
static FLASHMEM void handleCardRemoved() {
    msg.debug.error(TAG_SD, "SD card removed - logging halted (%s, %lu rows)",
                    logFileName[0] ? logFileName : "(no file)",
                    (unsigned long)logRowsWritten);

    // close() rather than closeLogFile(): having just established the card is
    // gone, the flush() closeLogFile() does first is both redundant (close()
    // syncs anyway) and the single most dangerous call available — a cache
    // flush to a dead card is what blocks past even the wide window. What makes
    // the remaining close() survivable is that maybeFlush() runs at the end of
    // every row, so the block cache is normally already clean when removal is
    // detected; that is load-bearing, not incidental.
    if (logFile) {
        SdWatchdogWindow window;
        closedFileBytes += logFile.position();   // in the handle, no card I/O
        logFile.close();
    }

    logFaulted     = true;
    logCardRemoved = true;
}

static void writeRow(uint32_t now) {
    // One timestamp for the whole sample in both layouts: every reading in a
    // tick shares an instant, instead of each field re-reading millis() as it
    // is printed (which left rows of one sample a few ms apart).
    if (openFileFormat == SDLOG_FORMAT_LONG) {
        // Time,Sensor,Value,Units — one row per reading.
        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (!logsThisInput(i)) continue;

            char valBuf[VALUE_FIELD_LEN];
            formatValueField(valBuf, sizeof(valBuf),
                             convertFromBaseUnits(inputs[i].value, inputs[i].unitsIndex));
            char unitBuf[UNIT_FIELD_LEN];
            formatUnitSymbol(unitBuf, sizeof(unitBuf), getUnitStringByIndex(inputs[i].unitsIndex));

            logFile.print((unsigned long)now);
            logFile.print(',');
            logFile.print(inputs[i].abbrName);
            logFile.print(',');
            logFile.print(valBuf);
            logFile.print(',');
            logFile.print(unitBuf);
            logFile.println();
        }
        logRowsWritten++;   // counts samples, not lines, in both layouts
    } else {
        logFile.print((unsigned long)now);

        for (uint8_t i = 0; i < MAX_INPUTS; i++) {
            if (!logsThisInput(i)) continue;

            char valBuf[VALUE_FIELD_LEN];
            formatValueField(valBuf, sizeof(valBuf),
                             convertFromBaseUnits(inputs[i].value, inputs[i].unitsIndex));
            logFile.print(',');
            logFile.print(valBuf);
        }

        logFile.println();
        logRowsWritten++;
    }

    if (reopenUsed && ++rowsSinceReopen >= SDLOG_REOPEN_PROBATION_ROWS) {
        reopenUsed = false;   // card has proven itself; re-arm the single retry
    }
}

static void maybeFlush(uint32_t now) {
    if (logFile && (now - lastFlushMs >= SDLOG_FLUSH_INTERVAL_MS)) {
        SdWatchdogWindow window;   // flush() blocks for seconds on a bad card
        logFile.flush();
        lastFlushMs = now;
    }
}

FLASHMEM void initSDLog() {
    logRowsWritten  = 0;
    logWriteErrors  = 0;
    logFaulted      = false;
    logCardRemoved  = false;
    reopenUsed      = false;
    rowsSinceReopen = 0;
    lastRowMs       = 0;
    wasInConfig     = isInConfigMode();

    // The file is opened lazily, on the first row written in RUN, not here.
    // Enabling SD logging almost always happens from CONFIG (that is where the
    // webapp and mobile app write config), and opening now would burn a
    // filename on a header describing a column set the user is still editing —
    // then roll to a second file the moment they add another input. Warn about
    // a missing card at enable time regardless, since that is when someone is
    // watching the console.
    if (!isSDInitialized()) {
        msg.debug.warn(TAG_SD, "SD logging enabled but no card mounted");
    }
}

/**
 * Per-input hook, unused.
 *
 * A wide row needs the whole sample at once, but this callback arrives one
 * input at a time — and sendToOutputs() filters out non-finite readings before
 * dispatching, so a faulted sensor never reaches here and could never be
 * recorded as ERROR. If every input were faulted it would not be called at all,
 * which is exactly when a row matters most. updateSDLog() owns the cadence
 * instead and walks inputs[] itself; the module registers a null send, like the
 * pull-based ELM327 output.
 */

void updateSDLog() {
    if (logFaulted) return;

    uint32_t now = millis();

    // CONFIG is setup, not operation (#268/#273): rows logged then are often
    // junk from disconnected or test sensors and would persist in the drive
    // log. Flush on the way in so whatever was recorded in RUN is durable.
    if (isInConfigMode()) {
        if (!wasInConfig) {
            if (logFile) {
                SdWatchdogWindow window;
                logFile.flush();
                lastFlushMs = now;
            }
            wasInConfig = true;
        }
        return;
    }
    wasInConfig = false;

    // Honour the configured interval. The old code gated rows on a hardcoded
    // 1000 ms const measured against a timestamp only ever updated inside the
    // 5 s flush branch — which suppressed rows for 1 s after each flush and
    // capped throughput at ~1 Hz, so OUTPUT SD_Log INTERVAL did nothing.
    uint16_t interval = systemConfig.outputInterval[OUTPUT_SD];
    if (interval == 0) interval = SD_LOG_INTERVAL_MS;

    if (now - lastRowMs < interval) {
        maybeFlush(now);
        return;
    }
    lastRowMs = now;

    // Nothing routed to SD: a file of bare timestamps is not worth a filename.
    // Close any open one rather than rolling a new empty file every tick.
    if (countLoggedInputs() == 0) {
        if (logFile) {
            closeLogFile();
            msg.debug.info(TAG_SD, "No inputs routed to SD - stopped logging");
        }
        return;
    }

    // Everything below touches the card, so hold one window across the whole
    // row (the nested ones inside openLogFile/closeLogFile/maybeFlush fold into
    // it) rather than re-arming the watchdog per operation.
    SdWatchdogWindow window;

    // Stop before writing to a card that is not there. This probe, not the
    // widened watchdog, is what actually saves the board: bench-testing a
    // probe-disabled build showed a pulled card still resets it (#331). The
    // rows after removal go into the library's block cache and report success
    // — 24 phantom rows, 0 write errors, DIAG HW still "OK" — until one cache
    // flush reaches the dead card and blocks past even the 20 s window.
    //
    // So getWriteError() is not a backstop for removal, only for a card that
    // is present and refusing writes (full, worn out). Platforms whose SD
    // library exposes no probe (SD_HAS_MEDIA_PROBE 0: ESP32 or any non-Teensy
    // board with an external card wired in) keep the old exposure.
    if (isSDInitialized() && !sdMediaPresent()) {
        handleCardRemoved();
        return;
    }

    if (!logFile) {
        openLogFile();                      // first row of the session
    } else if (systemConfig.sdLogFormat != openFileFormat) {
        msg.debug.info(TAG_SD, "Log format changed - starting new file");
        openLogFile();
    } else if (openFileFormat == SDLOG_FORMAT_WIDE &&
               computeColumnSignature() != columnSignature) {
        // Wide only: the header on disk no longer describes the rows we would
        // write. Long rows carry their own name and unit, so a changed input
        // set needs no new file — that is the layout's advantage.
        msg.debug.info(TAG_SD, "Logged input set changed - starting new file");
        openLogFile();
    }
    if (!logFile) return;

    writeRow(now);

    if (logFile.getWriteError()) {
        handleWriteError();
        return;
    }

    maybeFlush(now);
}

FLASHMEM void closeSDLog() {
    if (logFile) {
        closeLogFile();
        msg.debug.info(TAG_SD, "Log file closed: %s (%lu rows)",
                       logFileName, (unsigned long)logRowsWritten);
    }
}

// ===== Status reporting (SYSTEM DIAG HW) =====

bool sdLogIsActive()          { return (bool)logFile && !logFaulted; }
bool sdLogIsFaulted()         { return logFaulted; }
bool sdLogCardRemoved()       { return logCardRemoved; }
const char* sdLogFileName()   { return logFileName[0] ? logFileName : "(none)"; }
uint32_t sdLogRowsWritten()   { return logRowsWritten; }
uint16_t sdLogWriteErrors()   { return logWriteErrors; }
// position(), not size(): every log file is new and written sequentially, so
// they agree, but on ESP32 size() stat()s the card after a write and position()
// is in memory on both platforms. The status poll must not touch the card.
uint64_t sdLogBytesWritten()  { return closedFileBytes + (logFile ? (uint64_t)logFile.position() : 0); }

FLASHMEM void sdLogStatusSnapshot(SdStatusSnapshot& out) {
    out.mounted     = isSDInitialized();
    out.enabled     = systemConfig.outputEnabled[OUTPUT_SD];
    out.configMode  = isInConfigMode();
    // sdMediaKnownGone() as well: re-enabling the output clears the logger's
    // own latch, but the card is still gone (mounts happen at boot only).
    out.supported   = true;
    out.cardRemoved = logCardRemoved || sdMediaKnownGone();
    out.faulted     = logFaulted;
    out.fileOpen    = sdLogIsActive();
    out.fileName    = logFileName[0] ? logFileName : nullptr;
    out.rows        = logRowsWritten;
    out.errors      = logWriteErrors;
    // Estimate rather than re-read: see sdCapacityAtMount(). Undercounts by
    // under one cluster per file, and misses anything else written to the card
    // (a JSON config save), which is fine for "how much room is left".
    uint64_t freeBytes = 0, totalBytes = 0;
    out.capacityKnown = sdCapacityAtMount(freeBytes, totalBytes);
    const uint64_t used = sdLogBytesWritten();
    out.freeBytes  = freeBytes > used ? freeBytes - used : 0;
    out.totalBytes = totalBytes;
}

#else

// Dummy functions if SD logging is disabled
void initSDLog() {}
void updateSDLog() {}
void closeSDLog() {}

bool sdLogIsActive()          { return false; }
bool sdLogIsFaulted()         { return false; }
bool sdLogCardRemoved()       { return false; }
const char* sdLogFileName()   { return "(none)"; }
uint32_t sdLogRowsWritten()   { return 0; }
uint16_t sdLogWriteErrors()   { return 0; }
uint64_t sdLogBytesWritten()  { return 0; }

#endif
