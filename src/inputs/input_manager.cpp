/*
 * input_manager.cpp - Input Configuration & Management Implementation
 */

#include <EEPROM.h>
#include <math.h>  // isnan
#include <string.h>
 #include "../config.h"
#include "../version.h"
#include "input_manager.h"
#include "command_helpers.h"
#include "../lib/system_config.h"
#include "../lib/analog_pins.h"
#include "../lib/units_registry.h"
#include "../lib/message_router.h"  // For msg.control
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include "../lib/application_presets.h"
#include "../lib/sensor_library.h"
#include "../lib/pin_registry.h"
#include "../lib/bus_needs.h"
#include "../control/control_manager.h"  // controlResetTables + rule/action table helpers

// ===== GLOBAL STATE =====
Input inputs[MAX_INPUTS];
uint8_t numActiveInputs = 0;

// ===== EEPROM LAYOUT — shared front-growing config-store arena =====
// One arena holds every persisted record table, packed after a single header at
// address 0:
//   [ConfigStoreHeader] [InputEEPROM × numInputs] [Rule × numRules] [Action × numActions]
//   [ActionLink × numLinks]
//
// The SystemConfig block is anchored at the BACK of EEPROM (system_config.h); the
// arena grows forward toward it. All record tables draw from ONE shared budget —
// when the arena would collide with SystemConfig, trailing sections truncate
// LOUDLY in priority order inputs > rules > actions > links (a rule needs its
// source input to evaluate; an action needs its rule; a link needs both ends).
// This generalizes the original input-only truncation to the whole store (#296).
//
// The trailing ActionLink section (2nd+ rule memberships, #343) was ADDED without
// an EEPROM_VERSION bump, and that is safe for exactly two reasons: its count
// lives in a header byte every prior firmware explicitly zeroed at save, and the
// checksum covers only record bytes — so a pre-#343 store reads numLinks == 0 and
// re-checksums identically. Do NOT reuse another reserved byte without re-checking
// both of those.
//
// IMPORTANT: inputs store hashes (not registry indices) for stability — registry
// indices shift when entries are reordered, hashes don't; resolved back at boot.
// Rule/Action are stored DIRECTLY (no hash mirror): they key off the stable input
// PIN and stable record ids, not registry indices, so they round-trip as PODs.

#define EEPROM_MAGIC 0x4F454D53            // "OEMS" in ASCII - validates the arena is ours
#define EEPROM_HEADER_SIZE sizeof(ConfigStoreHeader)
#define EEPROM_INPUT_SIZE sizeof(InputEEPROM)    // size varies by platform alignment (AVR vs ARM)

struct ConfigStoreHeader {
    uint32_t magic;        // EEPROM_MAGIC
    uint16_t version;      // EEPROM_VERSION (store-layout version). NB: does NOT encode
                           // the INCLUDE_CONTROL_ENGINE build flag — a store written with
                           // rules/actions won't re-checksum on a control-off build (fails
                           // safe → reject → blank). Academic: a board's control state is fixed.
    uint8_t  numInputs;    // records actually persisted in each section…
    uint8_t  numRules;
    uint8_t  numActions;
    uint8_t  checksum;     // XOR over every persisted record byte (all sections)
    uint8_t  numLinks;     // ActionLink rows (#343) — a former reserved byte, always
                           // zeroed by prior firmware, so old stores read 0 (see above)
    uint8_t  reserved[5];  // future record-type counts; fills the struct to exactly 16 bytes
                           // (4+2+5+5) so there are NO indeterminate pad bytes on any platform
};

// InputEEPROM - Compact struct for EEPROM storage
// Only stores user-configurable fields (not runtime data or function pointers)
// Uses hashes instead of indices for stability across registry reordering
struct InputEEPROM {
    // === Hardware ===
    uint8_t pin;                    // Physical pin

    // === User Configuration (stored as hashes) ===
    char abbrName[8];               // "CHT", "OIL"
    char displayName[32];           // "Cylinder Head Temp"
    uint16_t applicationHash;       // djb2_hash of application name
    uint16_t sensorHash;            // djb2_hash of sensor name
    uint16_t unitsHash;             // djb2_hash of units name

    // === Alarm Thresholds (in STANDARD UNITS) ===
    // NOTE: alarm warmup/persist timings are NOT persisted here — they are
    // re-seeded from the application preset on load (loadInputConfig), so any
    // runtime SET ALARM WARMUP/PERSIST override is lost across reboot. Expanding
    // this would require an EEPROM version bump.
    float minValue;
    float maxValue;

    // === OBDII ===
    uint8_t obd2pid;
    uint8_t obd2length;

    // === Flags ===
    uint8_t flagsByte;              // Packed flags

    // === Output Routing ===
    uint8_t outputMask;             // Per-input output routing (bits 0-3: CAN, RealDash, Serial, SD)

    // === Calibration ===
    uint8_t calibrationType;
    CalibrationOverride customCalibration;  // 16 bytes

    // === Hardware Signal Conditioning ===
    float divider_ratio;                    // Voltage divider ratio (1.0 = none)
};

// MAX_EEPROM_INPUTS is the worst-case input ceiling: how many inputs fit between
// the header and the SystemConfig block if inputs were the ONLY section. Rules
// and actions share the same budget, so the live input limit at save time may be
// lower (handled dynamically by fitSection() below). Independent of MAX_INPUTS
// (a RAM limit) — on small-EEPROM platforms (e.g. Teensy 4.0) the user can
// configure more records in RAM than will persist; the store truncates loudly.
// The header is a fixed 16 bytes on every platform; the #343 numLinks byte came
// out of `reserved`, so this must NOT have changed (it is what lets a pre-#343
// store keep its section offsets).
static_assert(sizeof(ConfigStoreHeader) == 16, "ConfigStoreHeader must stay exactly 16 bytes");

constexpr size_t MAX_EEPROM_INPUTS =
    (SYSTEM_CONFIG_ADDRESS - sizeof(ConfigStoreHeader)) / sizeof(InputEEPROM);
static_assert(MAX_EEPROM_INPUTS >= 1,
              "EEPROM too small to persist even one input — check EEPROM_TOTAL_BYTES vs sizeof(SystemConfig)");

// ===== INITIALIZATION =====
bool initInputManager() {
    // Clear all inputs
    memset(inputs, 0, sizeof(inputs));
    numActiveInputs = 0;
#if INCLUDE_CONTROL_ENGINE
    // Empty the BSS-zeroed control tables up front: id==0 is a valid record id, so
    // an unstamped table looks full of phantom rule/action #0. loadInputConfig()
    // below is validate-before-mutate (it only touches the tables once a valid
    // store is confirmed), so cold-boot safety with a blank/invalid EEPROM relies
    // on this reset, not on the loader (#296 B2).
    controlResetTables();
#endif

    // Initialize with invalid values
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;  // Invalid pin
        inputs[i].applicationIndex = 0;  // 0 = NONE
        inputs[i].sensorIndex = 0;       // 0 = NONE
    }

    // Try to load from EEPROM
    bool eepromLoaded = loadInputConfig();
    if (!eepromLoaded) {
        msg.debug.info(TAG_CONFIG, "No valid config in EEPROM - starting with blank configuration");
    }
    return eepromLoaded;
}

// ===== EEPROM PERSISTENCE (shared config-store arena) =====

// XOR a record's raw bytes into a checksum. Called over the SAME bytes on save
// (the RAM image handed to EEPROM.put) and load (read back via EEPROM.get), so
// the two always agree byte-for-byte (padding included).
static uint8_t xorBytes(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) c ^= b[i];
    return c;
}

// How many records of one section fit in the remaining arena budget. Warns
// loudly on truncation (generalizes the legacy input-only message to any
// section), so a user who overfills the store sees exactly what won't persist.
static uint8_t fitSection(size_t remaining, uint8_t want, size_t recSize,
                          const __FlashStringHelper* label) {
    size_t fits = (recSize > 0) ? (remaining / recSize) : 0;
    if (fits > 254) fits = 254;            // record-count fields are uint8_t
    if (want <= fits) return want;
    msg.control.print(F("⚠ EEPROM holds "));
    msg.control.print((unsigned)fits);
    msg.control.print(F(" "));
    msg.control.print(label);
    msg.control.print(F("; truncating "));
    msg.control.print(want);
    msg.control.println(F(" — extras will not persist across reboot"));
    return (uint8_t)fits;
}

// Serialize one Input → its compact EEPROM record (registry indices → stable hashes).
static void buildInputRecord(const Input& in, InputEEPROM& out) {
    memset(&out, 0, sizeof(out));
    out.pin = in.pin;
    strncpy(out.abbrName, in.abbrName, sizeof(out.abbrName) - 1);
    out.abbrName[sizeof(out.abbrName) - 1] = '\0';
    strncpy(out.displayName, in.displayName, sizeof(out.displayName) - 1);
    out.displayName[sizeof(out.displayName) - 1] = '\0';
    out.minValue = in.minValue;
    out.maxValue = in.maxValue;
    out.obd2pid = in.obd2pid;
    out.obd2length = in.obd2length;
    out.calibrationType = in.calibrationType;
    memcpy(&out.customCalibration, &in.customCalibration, sizeof(CalibrationOverride));
    out.flagsByte =
        (in.flags.isEnabled ? 0x01 : 0) |
        (in.flags.alarm ? 0x02 : 0) |
        (in.flags.display ? 0x04 : 0) |
        (in.flags.useCustomCalibration ? 0x08 : 0);
    out.outputMask = in.outputMask;
    out.divider_ratio = in.divider_ratio;
    const ApplicationPreset* appPreset = getApplicationByIndex(in.applicationIndex);
    if (appPreset) out.applicationHash = pgm_read_word(&appPreset->nameHash);
    const SensorInfo* sensorInfo = getSensorByIndex(in.sensorIndex);
    if (sensorInfo) out.sensorHash = pgm_read_word(&sensorInfo->nameHash);
    const UnitsInfo* unitsInfo = getUnitsByIndex(in.unitsIndex);
    if (unitsInfo) out.unitsHash = pgm_read_word(&unitsInfo->nameHash);
}

// Deserialize one compact EEPROM record → an Input slot (hashes → indices),
// restoring function pointers + sensor-specific init (can't persist pointers).
static void applyInputRecord(const InputEEPROM& rec, Input& in) {
    in.pin = rec.pin;
    // memcpy (not strncpy) the fixed-size name buffers then force-terminate: the
    // stored strings are already null-terminated within their buffers, and src/dst
    // are equal-sized, so this copies the exact bytes safely. memcpy avoids the
    // -Wstringop-truncation the ARM toolchain raises for strncpy of a same-sized
    // source even when the following byte is explicitly nulled.
    memcpy(in.abbrName, rec.abbrName, sizeof(in.abbrName));
    in.abbrName[sizeof(in.abbrName) - 1] = '\0';
    memcpy(in.displayName, rec.displayName, sizeof(in.displayName));
    in.displayName[sizeof(in.displayName) - 1] = '\0';
    in.minValue = rec.minValue;
    in.maxValue = rec.maxValue;
    in.obd2pid = rec.obd2pid;
    in.obd2length = rec.obd2length;
    in.calibrationType = (CalibrationType)rec.calibrationType;
    memcpy(&in.customCalibration, &rec.customCalibration, sizeof(CalibrationOverride));
    in.flags.isEnabled = (rec.flagsByte & 0x01) != 0;
    in.flags.alarm = (rec.flagsByte & 0x02) != 0;
    in.flags.display = (rec.flagsByte & 0x04) != 0;
    in.flags.useCustomCalibration = (rec.flagsByte & 0x08) != 0;
    in.outputMask = rec.outputMask;
    in.divider_ratio = rec.divider_ratio;  // 0 from older EEPROMs treated as 1.0 by reader
    in.applicationIndex = getApplicationIndexByHash(rec.applicationHash);
    in.sensorIndex = getSensorIndexByHash(rec.sensorHash);
    in.unitsIndex = getUnitsIndexByHash(rec.unitsHash);
    const SensorInfo* flashInfo = getSensorByIndex(in.sensorIndex);
    if (flashInfo) {
        SensorInfo info;
        loadSensorInfo(flashInfo, &info);
        in.readFunction = info.readFunction;
        in.measurementType = info.measurementType;
        in.calibrationType = info.calibrationType;
        if (!in.flags.useCustomCalibration) in.presetCalibration = info.defaultCalibration;
        if (info.initFunction) info.initFunction(&in);
    }
    // (Legacy per-input alarm arming removed — alarms are control-engine Rules,
    // armed from rules[] by controlManagerInit(). The flagsByte alarm bit /
    // min/max are retained as dead fields for layout stability, not evaluated.)
}

bool saveInputConfig() {
    const size_t budget = (size_t)SYSTEM_CONFIG_ADDRESS - EEPROM_HEADER_SIZE;

    // What we WANT to persist in each section.
    uint8_t wantInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++)
        if (inputs[i].pin != 0xFF) wantInputs++;

    // Fit each section into the shared budget, priority inputs > rules > actions
    // (a rule needs its source input to evaluate; an action needs its rule).
    size_t remaining = budget;
    uint8_t persistInputs = fitSection(remaining, wantInputs, EEPROM_INPUT_SIZE, F("inputs"));
    remaining -= (size_t)persistInputs * EEPROM_INPUT_SIZE;
#if INCLUDE_CONTROL_ENGINE
    uint8_t persistRules = fitSection(remaining, controlRuleCount(), sizeof(Rule), F("rules"));
    remaining -= (size_t)persistRules * sizeof(Rule);
    uint8_t persistActions = fitSection(remaining, controlActionCount(), sizeof(Action), F("actions"));
    remaining -= (size_t)persistActions * sizeof(Action);
    uint8_t persistLinks = fitSection(remaining, controlActionLinkCount(), sizeof(ActionLink), F("rule links"));
    remaining -= (size_t)persistLinks * sizeof(ActionLink);
#endif

    // Write records, accumulating a checksum over exactly the bytes stored.
    uint16_t addr = EEPROM_HEADER_SIZE;
    uint8_t checksum = 0;

    uint8_t savedInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS && savedInputs < persistInputs; i++) {
        if (inputs[i].pin == 0xFF) continue;
        InputEEPROM rec;
        buildInputRecord(inputs[i], rec);
        EEPROM.put(addr, rec);
        checksum ^= xorBytes(&rec, sizeof(rec));
        addr += EEPROM_INPUT_SIZE;
        savedInputs++;
    }

    uint8_t savedRules = 0, savedActions = 0, savedLinks = 0;
#if INCLUDE_CONTROL_ENGINE
    for (int i = 0; i < MAX_RULES && savedRules < persistRules; i++) {
        if (rules[i].id == CONTROL_ID_NONE) continue;
        EEPROM.put(addr, rules[i]);
        checksum ^= xorBytes(&rules[i], sizeof(Rule));
        addr += sizeof(Rule);
        savedRules++;
    }
    for (int i = 0; i < MAX_ACTIONS && savedActions < persistActions; i++) {
        if (actions[i].id == CONTROL_ID_NONE) continue;
        EEPROM.put(addr, actions[i]);
        checksum ^= xorBytes(&actions[i], sizeof(Action));
        addr += sizeof(Action);
        savedActions++;
    }
    // Links last: a row whose action didn't fit is dropped by the load-time prune,
    // so an over-full store degrades to "some memberships lost", never a dangling ref.
    for (int i = 0; i < MAX_ACTION_LINKS && savedLinks < persistLinks; i++) {
        if (actionLinks[i].actionId == CONTROL_ID_NONE) continue;
        EEPROM.put(addr, actionLinks[i]);
        checksum ^= xorBytes(&actionLinks[i], sizeof(ActionLink));
        addr += sizeof(ActionLink);
        savedLinks++;
    }
#endif

    ConfigStoreHeader header;
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.numInputs = savedInputs;
    header.numRules = savedRules;
    header.numActions = savedActions;
    header.numLinks = savedLinks;
    header.checksum = checksum;
    memset(header.reserved, 0, sizeof(header.reserved));
    EEPROM.put(0, header);

    msg.control.print(F("✓ Saved "));
    msg.control.print(savedInputs);
    msg.control.print(F(" inputs"));
#if INCLUDE_CONTROL_ENGINE
    msg.control.print(F(", "));
    msg.control.print(savedRules);
    msg.control.print(F(" rules, "));
    msg.control.print(savedActions);
    msg.control.print(F(" actions"));
    if (savedLinks > 0) {
        msg.control.print(F(", "));
        msg.control.print(savedLinks);
        msg.control.print(F(" rule links"));
    }
#endif
    msg.control.println(F(" to EEPROM"));
    msg.debug.debug(TAG_CONFIG, "Checksum: 0x%02X", checksum);
    return true;
}

bool inputHoldsPin(uint8_t pin) {
    // 0xFF is the free-slot marker in inputs[], so it is never "held" — and it
    // is also what bus_defaults reports for a pin absent on this platform.
    if (pin == 0xFF) return false;
    return getInputByPin(pin) != nullptr;
}

uint8_t storedInputBusNeeds() {
    // Read-only pre-scan of the SAME records loadInputConfig() will apply, run
    // before it (and before initLCD()) so the buses those records need are up —
    // and their pins reserved — by the time any stored pin is validated (#358).
    //
    // Header sanity only, no checksum walk: a store that fails the full
    // validation is rejected wholesale by loadInputConfig() a moment later, so
    // the worst case here is bringing up a bus for a config that then does not
    // load — a spare peripheral, not a wrong pin assignment. The counts are
    // bounds-checked because they drive this loop.
    ConfigStoreHeader header;
    EEPROM.get(0, header);
    if (header.magic != EEPROM_MAGIC) return BUS_NEED_NONE;
    if (header.version != EEPROM_VERSION) return BUS_NEED_NONE;
    if ((size_t)EEPROM_HEADER_SIZE + (size_t)header.numInputs * EEPROM_INPUT_SIZE
            > (size_t)SYSTEM_CONFIG_ADDRESS) return BUS_NEED_NONE;

    // Every stored record votes, not just the ones that will fit in RAM: a
    // record this build clamps away still describes hardware wired to the board.
    uint8_t needs = BUS_NEED_NONE;
    for (uint8_t i = 0; i < header.numInputs; i++) {
        InputEEPROM rec;
        EEPROM.get(EEPROM_HEADER_SIZE + (size_t)i * EEPROM_INPUT_SIZE, rec);
        needs |= busNeedsForSensorHash(rec.sensorHash);
    }
    return needs;
}

bool loadInputConfig() {
    // VALIDATE BEFORE MUTATE: read and FULLY verify the header (magic, version,
    // bounds, checksum) against a read-only scan first; touch RAM (inputs[] / the
    // control tables) ONLY once the store is confirmed good. So a failed runtime
    // LOAD — blank, wrong-version, or corrupt EEPROM — is a true no-op: it cannot
    // wipe an unsaved in-RAM config or strand a previously-driven output
    // (review: Codex BLOCKER / Opus SHOULD-FIX). Cold-boot safety for the
    // BSS-zeroed control tables is handled by controlResetTables() in
    // initInputManager(), not here.
    ConfigStoreHeader header;
    EEPROM.get(0, header);

    if (header.magic != EEPROM_MAGIC) return false;
    if (header.version != EEPROM_VERSION) {
        // (debug string kept terse — it's RAM-resident on AVR, see message_api.h)
        msg.debug.warn(TAG_CONFIG, "EEPROM version mismatch (found %d, expected %d)",
                       header.version, EEPROM_VERSION);
        return false;
    }

    // Reject a store whose sections wouldn't fit before the SystemConfig block
    // (e.g. a chip physically moved from a larger-EEPROM board) BEFORE walking it,
    // so the checksum scan below can't read into the SystemConfig region (review
    // nit). The reads are bounded + read-only regardless; this just fails fast.
    // Silent reject — the caller (cmd_load) reports; no RAM-resident log on AVR.
    size_t storedBytes = EEPROM_HEADER_SIZE
                       + (size_t)header.numInputs  * EEPROM_INPUT_SIZE
#if INCLUDE_CONTROL_ENGINE
                       + (size_t)header.numRules   * sizeof(Rule)
                       + (size_t)header.numActions * sizeof(Action)
                       + (size_t)header.numLinks   * sizeof(ActionLink)
#endif
                       ;
    if (storedBytes > SYSTEM_CONFIG_ADDRESS) return false;

    // Verify the checksum over the bytes ACTUALLY stored (the header's own counts),
    // BEFORE clamping into this firmware's RAM caps — a build with smaller caps
    // must still validate a store written by a larger one (#296 S2). Sections are
    // contiguous, so a single running address walks all three. Read-only.
    uint8_t checksum = 0;
    {
        uint16_t a = EEPROM_HEADER_SIZE;
        for (uint8_t i = 0; i < header.numInputs; i++) {
            InputEEPROM rec; EEPROM.get(a, rec);
            checksum ^= xorBytes(&rec, sizeof(rec));
            a += EEPROM_INPUT_SIZE;
        }
#if INCLUDE_CONTROL_ENGINE
        for (uint8_t i = 0; i < header.numRules; i++) {
            Rule r; EEPROM.get(a, r);
            checksum ^= xorBytes(&r, sizeof(Rule));
            a += sizeof(Rule);
        }
        for (uint8_t i = 0; i < header.numActions; i++) {
            Action ac; EEPROM.get(a, ac);
            checksum ^= xorBytes(&ac, sizeof(Action));
            a += sizeof(Action);
        }
        // numLinks is 0 in any store written before #343 — see the layout note.
        for (uint8_t i = 0; i < header.numLinks; i++) {
            ActionLink l; EEPROM.get(a, l);
            checksum ^= xorBytes(&l, sizeof(ActionLink));
            a += sizeof(ActionLink);
        }
#endif
    }
    if (checksum != header.checksum) {
        msg.control.println(F("ERROR: EEPROM checksum mismatch! Configuration corrupted."));
        msg.control.println(F("Please reconfigure and run SAVE."));
        msg.debug.error(TAG_CONFIG, "Store checksum mismatch: stored 0x%02X, calc 0x%02X",
                        header.checksum, checksum);
        return false;  // no RAM mutated — prior in-RAM config left intact
    }

    // ===== store validated — COMMIT (the only point past here that mutates RAM) =====
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) inputs[i].pin = 0xFF;
    numActiveInputs = 0;
#if INCLUDE_CONTROL_ENGINE
    controlResetTables();
#endif

    // --- inputs (hashes → indices) ---
    uint8_t nInputs = header.numInputs;
    if (nInputs > MAX_INPUTS) nInputs = MAX_INPUTS;
    if (nInputs > MAX_EEPROM_INPUTS) nInputs = MAX_EEPROM_INPUTS;
    for (uint8_t i = 0; i < nInputs; i++) {
        InputEEPROM rec;
        EEPROM.get(EEPROM_HEADER_SIZE + (size_t)i * EEPROM_INPUT_SIZE, rec);

        // Re-check the pin against the registry on LOAD, not just on SET. A config
        // saved by firmware that failed to reserve a bus pin (#351: the LCD used to
        // bring up I2C without telling bus_manager) would otherwise be re-applied
        // verbatim every boot — driving an input on SCL, wedging the bus and
        // watchdog-resetting before the user can reach the CLI to fix it. Dropping
        // the offending record makes such a config self-heal instead of reset-loop.
        // Virtual pins (CAN 0xC0+, I2C 0xF0+) have no registry entry — skip them.
        if (rec.pin < 0xC0 && !isPinAvailable(rec.pin)) {
            msg.control.print(F("⚠ Skipping stored input on pin "));
            int8_t analogNum = analogIndexForPin(rec.pin);
            if (analogNum >= 0) {
                msg.control.print(F("A"));
                msg.control.print(analogNum);
            } else {
                msg.control.print(rec.pin);
            }
            msg.control.print(F(" — reserved by "));
            const char* desc = getPinDescription(rec.pin);
            // Not a ternary with F(): the literal would land in RAM on AVR.
            if (desc) msg.control.println(desc);
            else      msg.control.println(F("a bus"));
            msg.control.println(F("  Run SAVE to drop it permanently."));
            continue;  // slot stays pin==0xFF (free)
        }

        applyInputRecord(rec, inputs[i]);
    }
    // numActiveInputs is an enabled-count, not an occupancy bound (see
    // exportInputsToJSON) — now that disabled inputs persist too (#317), recount
    // by the enabled flag rather than trusting the loaded record count.
    numActiveInputs = 0;
    for (uint8_t i = 0; i < nInputs; i++)
        if (inputs[i].flags.isEnabled) numActiveInputs++;

#if INCLUDE_CONTROL_ENGINE
    // Section bases from the STORED counts (not the RAM-clamped ones) so rules /
    // actions are located at the offsets they were actually written to.
    const size_t rulesBase   = EEPROM_HEADER_SIZE + (size_t)header.numInputs * EEPROM_INPUT_SIZE;
    const size_t actionsBase = rulesBase + (size_t)header.numRules * sizeof(Rule);

    uint8_t nRules = header.numRules;
    if (nRules > MAX_RULES) nRules = MAX_RULES;
    for (uint8_t i = 0; i < nRules; i++)
        EEPROM.get(rulesBase + (size_t)i * sizeof(Rule), rules[i]);

    uint8_t nActions = header.numActions;
    if (nActions > MAX_ACTIONS) nActions = MAX_ACTIONS;
    for (uint8_t i = 0; i < nActions; i++)
        EEPROM.get(actionsBase + (size_t)i * sizeof(Action), actions[i]);

    const size_t linksBase = actionsBase + (size_t)header.numActions * sizeof(Action);
    uint8_t nLinks = header.numLinks;
    if (nLinks > MAX_ACTION_LINKS) {
        // A dropped membership is a relay that quietly stops responding to one of
        // its sensors — worse than a dropped rule, and SAVE warns loudly via
        // fitSection while LOAD historically said nothing. Say it here (#343).
        // F()/msg.control, NOT msg.debug: a debug format string is RAM-resident on
        // AVR (message_api.h) and this one cost mega2560 54 B of an 8 KB budget.
        msg.control.print(F("⚠ EEPROM holds more rule links than this board can hold ("));
        msg.control.print((unsigned)MAX_ACTION_LINKS);
        msg.control.println(F("); extra memberships dropped"));
        nLinks = MAX_ACTION_LINKS;
    }
    for (uint8_t i = 0; i < nLinks; i++)
        EEPROM.get(linksBase + (size_t)i * sizeof(ActionLink), actionLinks[i]);

    // Dangling-ref sweep (#296 S3, extended to link rows by #343): a truncated or
    // hand-edited store may leave an Action referencing a Rule that didn't load.
    // controlSweepActionRules() repairs both halves of the membership so the
    // engine never evaluates a dangling ref. (Re-arming is the caller's job — boot
    // does it via controlManagerInit; LOAD via the re-arm trio in cmd_load.)
    controlSweepActionRules();
#endif

    // Terse info string (RAM-resident on AVR); the SAVE confirmation already
    // reports rule/action counts via flash F() strings.
    msg.debug.info(TAG_CONFIG, "Loaded %d inputs from EEPROM", numActiveInputs);
    return true;
}

void resetInputConfig() {
    // Clear the store header (magic zeroed → next load reverts to blank config).
    ConfigStoreHeader header = {};
    EEPROM.put(0, header);

    // Clear inputs
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;
    }
    numActiveInputs = 0;

#if INCLUDE_CONTROL_ENGINE
    controlResetTables();  // empty Rule/Action tables too (#296 B4)
#endif

    msg.control.println(F("Configuration reset"));
}

// ===== HELPER FUNCTIONS =====

/**
 * Find an input configuration by its physical pin number.
 * @param pin  Pin number (use A0, A1, etc. for analog pins)
 * @return Pointer to Input struct, or nullptr if pin not configured
 */
Input* getInputByPin(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return &inputs[i];
        }
    }
    return nullptr;
}

/**
 * Find an input configuration by its array index.
 * @param index  Array index (0 to MAX_INPUTS-1)
 * @return Pointer to Input struct, or nullptr if index out of range
 * @note  Unlike getInputByPin(), this returns even unconfigured slots
 */
Input* getInputByIndex(uint8_t index) {
    if (index < MAX_INPUTS) {
        return &inputs[index];
    }
    return nullptr;
}

/**
 * Get the array index for a given pin number.
 * @param pin  Pin number to search for
 * @return Array index (0 to MAX_INPUTS-1), or 0xFF if not found
 */
uint8_t getInputIndex(uint8_t pin) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == pin) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

/**
 * Find the first unused slot in the inputs array.
 * @return Array index of free slot, or 0xFF if array is full
 */
static uint8_t findFreeSlot() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin == 0xFF) {
            return i;
        }
    }
    return 0xFF;  // No free slots
}

/**
 * Validate a CANDIDATE input configuration.
 *
 * Takes the fields it judges by value rather than an Input* on purpose: it is
 * called BEFORE anything is written to the slot, so a refusal costs nothing to
 * undo. See the setInputApplication() contract in input_manager.h (#361/#364).
 *
 * @param pin           pin the candidate wants
 * @param self          the input already occupying `pin`, or nullptr for a new
 *                      one — skipped in the conflict scan so an existing input
 *                      never collides with itself
 * @param alarmEnabled  candidate flags.alarm
 * @param minValue      candidate alarm minimum (standard units)
 * @param maxValue      candidate alarm maximum (standard units)
 *
 * Checks for:
 * - Pin reserved by a bus (I2C/SPI/CAN)
 * - Pin conflicts (duplicate pin assignments)
 * - Alarm threshold sanity (min < max)
 * Returns true if valid, false if errors found
 */
static bool validateInputConfig(uint8_t pin, const Input* self,
                                bool alarmEnabled, float minValue, float maxValue) {
    // Check if pin is reserved by a bus (I2C, SPI, CAN)
    // Skip this check for virtual pins (CAN 0xC0+, I2C 0xF0+)
    if (pin < 0xC0 && !isPinAvailable(pin)) {
        msg.control.print(F("ERROR: Pin "));
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            msg.control.print(F("A"));
            msg.control.print(analogNum);
        } else {
            msg.control.print(pin);
        }
        msg.control.println(F(" is reserved by a bus (I2C/SPI/CAN)"));
        return false;
    }

    // Check for pin conflicts with other enabled inputs
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        Input* other = &inputs[i];

        // Skip self-comparison and uninitialized slots
        if (other == self || other->pin == 0xFF) {
            continue;
        }

        // Check if another enabled input uses the same pin
        if (other->flags.isEnabled && other->pin == pin) {
            msg.control.print(F("ERROR: Pin "));
            if (pin >= 0xF0) {
                msg.control.print(F("I2C"));
            } else if (pin >= 0xC0 && pin < 0xE0) {
                msg.control.print(F("CAN"));
            } else {
                int8_t analogNum = analogIndexForPin(pin);
                if (analogNum >= 0) {
                    msg.control.print(F("A"));
                    msg.control.print(analogNum);
                } else {
                    msg.control.print(pin);
                }
            }
            msg.control.println(F(" already in use"));
            return false;
        }
    }

    // Check alarm threshold sanity (only if alarms enabled)
    if (alarmEnabled) {
        if (minValue >= maxValue) {
            msg.control.print(F("ERROR: Invalid alarm range ("));
            msg.control.print(minValue);
            msg.control.print(F(" >= "));
            msg.control.print(maxValue);
            msg.control.println(F(")"));
            return false;
        }
    }

    return true;
}

// ===== CONFIGURATION FUNCTIONS =====

bool allocateInputSlot(uint8_t pin) {
    if (getInputByPin(pin) != nullptr) return true;  // already exists
    uint8_t slot = findFreeSlot();
    if (slot == 0xFF) {
        msg.control.print(F("ERROR: No free input slots (max "));
        msg.control.print(MAX_INPUTS);
        msg.control.println(F(" inputs)"));
        msg.control.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
        return false;
    }
    memset(&inputs[slot], 0, sizeof(Input));
    inputs[slot].pin = pin;
    numActiveInputs++;
    return true;
}

void freeInputSlot(uint8_t pin) {
    // Wipe the whole slot, not just the pin: findFreeSlot() recycles on
    // pin == 0xFF alone, so anything left behind here (notably
    // flags.isEnabled and a stale readFunction) would be inherited by the
    // next occupant of the slot. See #361.
    //
    // clearInput() is exactly that wipe, and it *recounts* numActiveInputs
    // rather than decrementing. Recounting is what the count means -- it is
    // an enabled-count, defined that way by loadInputConfig() and the JSON
    // import -- and it keeps this helper correct no matter where a caller
    // fails relative to the increment.
    clearInput(pin);
}

bool setInputApplication(uint8_t pin, uint8_t appIndex) {
    // Find or create input
    Input* input = getInputByPin(pin);
    bool isNewInput = (input == nullptr);

    // ---- ADMISSION PHASE ------------------------------------------------
    // Everything that can say "no" runs here, BEFORE a single byte is written
    // to the input table. That is what makes the "a refusal changes nothing"
    // contract in input_manager.h structural rather than a revert each error
    // path has to remember to perform. Populate-then-validate previously left
    // a new slot enabled with a null readFunction (#361/#353, faulted the
    // board) and an existing input half-rewritten with the new application's
    // units/PID and force-enabled (#364).

    // Resolve the preset BEFORE claiming a slot. Claiming first meant this
    // early return either leaked an uncounted slot or, if it released one,
    // decremented a count that isn't incremented until further below.
    const ApplicationPreset* flashPreset = getApplicationByIndex(appIndex);
    if (flashPreset == nullptr) {
        msg.control.println(F("ERROR: Invalid Type"));
        return false;
    }

    // Load entire preset into RAM (cleaner than scattered pgm_read_* calls)
    ApplicationPreset preset;
    loadApplicationPreset(flashPreset, &preset);

    // The alarm flag the candidate would carry — validateInputConfig only looks
    // at the min/max band when it's armed, so it has to match what the commit
    // phase below writes.
#if INCLUDE_CONTROL_ENGINE
    const bool candidateAlarm = false;
#else
    const bool candidateAlarm = preset.defaultAlarmEnabled;
#endif
    if (!validateInputConfig(pin, input, candidateAlarm,
                             preset.defaultMinValue, preset.defaultMaxValue)) {
        return false;
    }

    // Resolve the preset's sensor now as well. setInputSensor() runs AFTER the
    // commit phase, and an invalid index is its only failure mode once the slot
    // exists — so checking it here is what lets the commit phase be infallible
    // (an existing input has no slot to free and nothing to revert to).
    uint8_t defaultSensor = preset.defaultSensor;
    if (getSensorByIndex(defaultSensor) == nullptr) {
        msg.control.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    if (isNewInput) {
        uint8_t slot = findFreeSlot();
        if (slot == 0xFF) {
            msg.control.print(F("ERROR: No free input slots (max "));
            msg.control.print(MAX_INPUTS);
            msg.control.println(F(" inputs)"));
            msg.control.println(F("  Hint: Use 'CLEAR <pin>' to remove an existing input"));
            return false;
        }
        input = &inputs[slot];
        memset(input, 0, sizeof(Input));
        input->pin = pin;
    }

    // ---- COMMIT PHASE ---------------------------------------------------
    // Past this point nothing may fail; do not add a `return false` below.

    // Apply preset to input
    input->applicationIndex = appIndex;

    // Read abbreviation and label from PROGMEM pointers
    // (loadApplicationPreset copies the struct, but string pointers still point to PROGMEM)
    strncpy_P(input->abbrName, preset.abbreviation, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';

    if (preset.label) {
        strncpy_P(input->displayName, preset.label, sizeof(input->displayName) - 1);
    } else {
        strncpy_P(input->displayName, preset.name, sizeof(input->displayName) - 1);
    }
    input->displayName[sizeof(input->displayName) - 1] = '\0';

    // Don't set sensorIndex yet - let setInputSensor() do it
    // (This allows sensorChanged check to work correctly; defaultSensor was
    // resolved and validated in the admission phase above.)
    input->unitsIndex = preset.defaultUnits;

    // CRITICAL: Store min/max in STANDARD UNITS (no conversion!)
    // Preset already has values in Celsius, bar, volts, etc.
    input->minValue = preset.defaultMinValue;
    input->maxValue = preset.defaultMaxValue;

    input->obd2pid = preset.obd2pid;
    input->obd2length = preset.obd2length;
    // App-seeded alarms are now control-engine Rules (§11.5): the caller
    // (set_application) seeds a default Rule from the template. Don't also arm
    // the legacy per-input alarm, or the threshold would be enforced twice.
    // (candidateAlarm resolves that build-flag choice once, above, so the
    // validator and this assignment can never disagree.)
    input->flags.alarm = candidateAlarm;
    input->flags.display = preset.defaultDisplayEnabled;
    input->flags.isEnabled = true;
    input->flags.useCustomCalibration = false;  // Use preset calibration
    input->outputMask = OUTPUT_MASK_ALL_DATA;   // All data outputs enabled by default
    input->divider_ratio = 1.0f;                // Default to no divider

    // (Legacy alarm context removed — set_application seeds a control-engine
    // Rule from the template instead, §11.5.)

    // Increment count if this is a new input
    if (isNewInput) {
        numActiveInputs++;
    }

    // Set up sensor (function pointers + calibration)
    // This also sets input->sensorIndex. It cannot fail here: the slot exists
    // and defaultSensor was resolved during admission. The guard stays as a
    // tripwire in case a future failure mode is added to setInputSensor() --
    // and it must free the slot, because an enabled slot with a null
    // readFunction is exactly the #361 fault.
    if (!setInputSensor(pin, defaultSensor)) {
        if (isNewInput) freeInputSlot(pin);
        return false;
    }
    return true;
}

bool setInputSensor(uint8_t pin, uint8_t sensorIndex) {
    Input* input = requireInput(pin);
    if (!input) return false;

    // Get Sensor info from flash
    const SensorInfo* flashInfo = getSensorByIndex(sensorIndex);
    if (flashInfo == nullptr) {
        msg.control.println(F("ERROR: Invalid Sensor Type"));
        return false;
    }

    // Load entire sensor info into RAM
    SensorInfo info;
    loadSensorInfo(flashInfo, &info);

    // Check if sensor is actually changing (to avoid redundant init)
    bool sensorChanged = (input->sensorIndex != sensorIndex);

    // Apply sensor info to input
    input->sensorIndex = sensorIndex;
    input->readFunction = info.readFunction;
    input->measurementType = info.measurementType;
    input->calibrationType = info.calibrationType;

    // Point to calibration in PROGMEM (don't copy to RAM unless custom)
    input->presetCalibration = info.defaultCalibration;
    input->flags.useCustomCalibration = false;

    // Call sensor-specific initialization function only if sensor changed
    // (Prevents duplicate init when setting same sensor twice)
    if (sensorChanged && info.initFunction) {
        info.initFunction(input);
    }

    return true;
}

bool setInputAbbr(uint8_t pin, const char* abbr) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->abbrName, abbr, sizeof(input->abbrName) - 1);
    input->abbrName[sizeof(input->abbrName) - 1] = '\0';
    return true;
}

bool setInputDisplayName(uint8_t pin, const char* displayName) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    strncpy(input->displayName, displayName, sizeof(input->displayName) - 1);
    input->displayName[sizeof(input->displayName) - 1] = '\0';
    return true;
}

bool setInputUnits(uint8_t pin, uint8_t unitsIndex) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->unitsIndex = unitsIndex;
    return true;
}

bool setInputAlarmRange(uint8_t pin, float minValue, float maxValue) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    // Validate range (allow both to be 0 for disabled alarms)
    if (minValue >= maxValue && !(minValue == 0 && maxValue == 0)) {
        msg.control.print(F("ERROR: Min alarm ("));
        msg.control.print(minValue);
        msg.control.print(F(") must be less than max alarm ("));
        msg.control.print(maxValue);
        msg.control.println(F(")"));
        return false;
    }

    // Validate against sensor capabilities
    const SensorInfo* sensorInfo = getSensorByIndex(input->sensorIndex);
    if (sensorInfo && input->sensorIndex != 0) {  // Skip validation for SENSOR_NONE
        // Check if alarm range exceeds sensor's physical capabilities
        if (minValue < sensorInfo->minValue || maxValue > sensorInfo->maxValue) {
            msg.control.print(F("WARNING: Alarm range ("));
            msg.control.print(minValue);
            msg.control.print(F(" - "));
            msg.control.print(maxValue);
            msg.control.print(F(") exceeds sensor capability ("));
            msg.control.print(sensorInfo->minValue);
            msg.control.print(F(" - "));
            msg.control.print(sensorInfo->maxValue);

            // Print sensor name for clarity
            char sensorName[32];
            strcpy_P(sensorName, (const char*)sensorInfo->name);
            msg.control.print(F(") for "));
            msg.control.println(sensorName);

            // Don't fail - allow the user to set it, but warn them
            // This is useful for sensors that might be replaced or for edge cases
        }
    }

    // Store in STANDARD UNITS (caller's responsibility to provide correct units)
    input->minValue = minValue;
    input->maxValue = maxValue;
    return true;
}

bool setInputOBD(uint8_t pin, uint8_t pid, uint8_t length) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->obd2pid = pid;
    input->obd2length = length;
    return true;
}

bool enableInput(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.isEnabled = enable;
    return true;
}

bool enableInputAlarm(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    // Idempotent: only (re)arm on an actual flag transition. Re-initializing on
    // every call would reset an already-armed alarm back to ALARM_INIT/warmup
    // and clear faultStartTime/isInAlarm — so a periodic config re-sync that
    // re-issues SET <pin> ALARM ENABLE could keep an alarm perpetually in
    // warmup and prevent it from ever firing (and would silently suppress a
    // live alarm). The EEPROM load path already arms the context on boot, so
    // there is no "flag true but DISABLED" state to recover here.
    if (input->flags.alarm == enable) return true;

    input->flags.alarm = enable;  // dead field (alarms are Rules); retained for json compat
    return true;
}

bool enableInputDisplay(uint8_t pin, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.display = enable;
    return true;
}

bool setInputAlarmWarmup(uint8_t pin, uint16_t warmupTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.warmupTime_ms = warmupTime_ms;
    return true;
}

bool setInputAlarmPersist(uint8_t pin, uint16_t persistTime_ms) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->alarmContext.persistTime_ms = persistTime_ms;
    return true;
}

bool setInputOutputMask(uint8_t pin, uint8_t outputId, bool enable) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    if (outputId > OUTPUT_SD) return false;  // Only data outputs (0-3)

    if (enable) {
        input->outputMask |= (1 << outputId);
    } else {
        input->outputMask &= ~(1 << outputId);
    }
    return true;
}

bool clearInput(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    memset(input, 0, sizeof(Input));
    input->pin = 0xFF;

    // Recount active inputs
    numActiveInputs = 0;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            numActiveInputs++;
        }
    }

    return true;
}

// ===== CALIBRATION OVERRIDE FUNCTIONS =====
bool setInputCalibrationSteinhart(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.steinhart.bias_resistor = bias;
    input->customCalibration.steinhart.steinhart_a = a;
    input->customCalibration.steinhart.steinhart_b = b;
    input->customCalibration.steinhart.steinhart_c = c;

    return true;
}

bool setInputCalibrationLookup(uint8_t pin, float bias) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.lookup.bias_resistor = bias;

    return true;
}

bool setInputCalibrationPressureLinear(uint8_t pin, float vMin, float vMax, float pMin, float pMax) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressureLinear.voltage_min = vMin;
    input->customCalibration.pressureLinear.voltage_max = vMax;
    input->customCalibration.pressureLinear.output_min = pMin;
    input->customCalibration.pressureLinear.output_max = pMax;

    return true;
}

bool setInputCalibrationPressurePolynomial(uint8_t pin, float bias, float a, float b, float c) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = true;
    input->customCalibration.pressurePolynomial.bias_resistor = bias;
    input->customCalibration.pressurePolynomial.poly_a = a;
    input->customCalibration.pressurePolynomial.poly_b = b;
    input->customCalibration.pressurePolynomial.poly_c = c;

    return true;
}

bool clearInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (input == nullptr) return false;

    input->flags.useCustomCalibration = false;
    memset(&input->customCalibration, 0, sizeof(CalibrationOverride));

    return true;
}

// ===== RUNTIME =====

void readAllInputs() {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled && inputs[i].readFunction != nullptr) {
            inputs[i].readFunction(&inputs[i]);
        }
    }
}

// ===== INFO/LISTING =====

// Helper to print pin name (A0, 1, I2C:0, CAN:0, etc)
static void printPin(uint8_t pin) {
    if (pin >= 0xF0) {
        msg.control.print(F("I2C:"));
        msg.control.print(pin - 0xF0);
    } else if (pin >= 0xC0 && pin < 0xE0) {
        msg.control.print(F("CAN:"));
        msg.control.print(pin - 0xC0);
    } else {
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            msg.control.print(F("A"));
            msg.control.print(analogNum);
        } else {
            msg.control.print(pin);
        }
    }
}

void printInputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Input Info ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  Enabled: "));
    msg.control.println(input->flags.isEnabled ? F("YES") : F("NO"));

    msg.control.print(F("  Application: "));
    msg.control.println(getApplicationNameByIndex(input->applicationIndex));

    msg.control.print(F("  Sensor: "));
    msg.control.println(getSensorNameByIndex(input->sensorIndex));

    msg.control.print(F("  Display Name: '"));
    msg.control.print(input->displayName);
    msg.control.println(F("'"));

    msg.control.print(F("  Short Name: '"));
    msg.control.print(input->abbrName);
    msg.control.println(F("'"));

    msg.control.print(F("  Units: "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.println(F("  Alarms: managed by Rules — see RULE LIST"));

    msg.control.print(F("  Output Mask: 0x"));
    if (input->outputMask < 0x10) msg.control.print('0');
    msg.control.println(input->outputMask, HEX);

    msg.control.print(F("  Current Value: "));
    if (isnan(input->value)) {
        msg.control.print(F("ERROR"));
    } else {
        msg.control.print(convertFromBaseUnits(input->value, input->unitsIndex), 2);
    }
    msg.control.print(F(" "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.println();
    msg.control.println(F("To see alarm config:  INFO <pin> ALARM"));
    msg.control.println(F("To see calibration:   INFO <pin> CALIBRATION"));
    msg.control.println(F("To see output routing: INFO <pin> OUTPUT"));
    msg.control.println();
}

void printInputOutputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Output Routing ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  CAN:      "));
    msg.control.println((input->outputMask & (1 << OUTPUT_CAN)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  RealDash: "));
    msg.control.println((input->outputMask & (1 << OUTPUT_REALDASH)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  Serial:   "));
    msg.control.println((input->outputMask & (1 << OUTPUT_SERIAL)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  SD_Log:   "));
    msg.control.println((input->outputMask & (1 << OUTPUT_SD)) ? F("ENABLED") : F("DISABLED"));

    msg.control.println();
}

void printInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Calibration ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();
    msg.control.print(F("  Type: "));
    if (!input->flags.useCustomCalibration) {
        msg.control.println(F("Preset (PROGMEM)"));
    } else if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
        msg.control.println(F("Steinhart-Hart Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.steinhart.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_c, 10);
    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
        msg.control.println(F("Beta Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.beta.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  Beta: "));
        msg.control.println(input->customCalibration.beta.beta, 1);
        msg.control.print(F("  R0: "));
        msg.control.print(input->customCalibration.beta.r0, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  T0: "));
        msg.control.print(input->customCalibration.beta.t0, 2);
        msg.control.println(F(" K"));
    } else if (input->calibrationType == CAL_LINEAR) {
        msg.control.println(F("Linear Custom"));
        msg.control.print(F("  Voltage Range: "));
        msg.control.print(input->customCalibration.pressureLinear.voltage_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.voltage_max, 2);
        msg.control.println(F(" V"));
        msg.control.print(F("  Output Range: "));
        msg.control.print(input->customCalibration.pressureLinear.output_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.output_max, 2);
        msg.control.println();
    } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        msg.control.println(F("Polynomial Custom (VDO)"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.pressurePolynomial.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_c, 10);
    } else if (input->calibrationType == CAL_RPM) {
        msg.control.println(F("RPM Custom"));
        msg.control.print(F("  Poles: "));
        msg.control.println(input->customCalibration.rpm.poles);
        msg.control.print(F("  Pulley Ratio: "));
        msg.control.println(input->customCalibration.rpm.pulley_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.rpm.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.rpm.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  RPM Range: "));
        msg.control.print(input->customCalibration.rpm.min_rpm);
        msg.control.print(F("-"));
        msg.control.println(input->customCalibration.rpm.max_rpm);
    } else if (input->calibrationType == CAL_SPEED) {
        msg.control.println(F("Speed Custom"));
        msg.control.print(F("  Pulses/Rev: "));
        msg.control.println(input->customCalibration.speed.pulses_per_rev);
        msg.control.print(F("  Tire Circumference: "));
        msg.control.print(input->customCalibration.speed.tire_circumference_mm);
        msg.control.println(F(" mm"));
        msg.control.print(F("  Drive Ratio: "));
        msg.control.println(input->customCalibration.speed.final_drive_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.speed.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.speed.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  Max Speed: "));
        msg.control.print(input->customCalibration.speed.max_speed_kph);
        msg.control.println(F(" km/h"));
    } else if (input->calibrationType == CAL_CAN_IMPORT) {
        CANSensorCalibration cal;
        if (input->flags.useCustomCalibration) {
            memcpy(&cal, &input->customCalibration.can, sizeof(cal));
        } else {
            memcpy_P(&cal, input->presetCalibration, sizeof(cal));
        }
        uint16_t resolved_timeout = cal.timeout_ms;
        if (resolved_timeout < 100 || resolved_timeout > 30000) resolved_timeout = CAN_DEFAULT_TIMEOUT_MS;

        msg.control.println(input->flags.useCustomCalibration ? F("CAN Import Custom") : F("CAN Import Preset"));
        msg.control.print(F("  CAN ID: 0x"));
        msg.control.println(cal.source_can_id, HEX);
        msg.control.print(F("  PID: 0x"));
        if (cal.source_pid < 0x10) msg.control.print('0');
        msg.control.println(cal.source_pid, HEX);
        msg.control.print(F("  Offset/Length: "));
        msg.control.print(cal.data_offset);
        msg.control.print(F("/"));
        msg.control.print(cal.data_length);
        msg.control.println(cal.is_big_endian ? F(" bytes (big-endian)") : F(" bytes (little-endian)"));
        msg.control.print(F("  Scale: "));
        msg.control.println(cal.scale_factor, 4);
        msg.control.print(F("  Offset: "));
        msg.control.println(cal.offset, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(resolved_timeout);
        msg.control.println(F(" ms"));
    }
    msg.control.println();
}

#ifndef FLASHMEM
#define FLASHMEM
#endif

FLASHMEM void formatPinLabel(uint8_t pin, char* buf, size_t bufLen) {
    // snprintf_P + PSTR: the format strings stay in flash on AVR (plain
    // string literals there are RAM-resident and count against the 8 KB
    // budget); both are passthroughs on ARM.
    if (pin >= 0xF0) {
        snprintf_P(buf, bufLen, PSTR("I2C:%u"), (unsigned)(pin - 0xF0));
    } else if (pin >= 0xC0 && pin < 0xE0) {
        snprintf_P(buf, bufLen, PSTR("CAN:%u"), (unsigned)(pin - 0xC0));
    } else {
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            snprintf_P(buf, bufLen, PSTR("A%d"), analogNum);
        } else {
            snprintf_P(buf, bufLen, PSTR("%u"), (unsigned)pin);
        }
    }
}

void listAllInputs() {
    msg.control.println(F("Active Inputs:"));
    bool found = false;

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            found = true;
            char pinLabel[8];
            formatPinLabel(inputs[i].pin, pinLabel, sizeof(pinLabel));
            msg.control.print(F("  "));
            msg.control.print(pinLabel);
            msg.control.print(F(": "));
            msg.control.print(inputs[i].abbrName);
            msg.control.print(F(" ("));
            msg.control.print(inputs[i].displayName);
            msg.control.print(F(") = "));
            if (isnan(inputs[i].value)) {
                msg.control.print(F("ERROR"));
            } else {
                msg.control.print(convertFromBaseUnits(inputs[i].value, inputs[i].unitsIndex));
            }
            msg.control.print(F(" "));
            msg.control.print((__FlashStringHelper*)getUnitStringByIndex(inputs[i].unitsIndex));
            if (inputs[i].obd2pid != 0) {
                msg.control.print(F(" [OBD PID 0x"));
                if (inputs[i].obd2pid < 0x10) msg.control.print('0');
                msg.control.print(inputs[i].obd2pid, HEX);
                msg.control.print(F(" len "));
                msg.control.print(inputs[i].obd2length);
                msg.control.print(F("]"));
            }
            msg.control.println();
        }
    }

    if (!found) {
        msg.control.println(F("  (none)"));
    }
}

void listApplicationPresets() {
    msg.control.println(F("Available Application Presets:"));

    // Group by measurement type
    msg.control.println(F("Temperature:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_TEMPERATURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            // Pad to 20 chars for alignment
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Pressure:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_PRESSURE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Electrical:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type == MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }

    msg.control.println(F("Other:"));
    for (uint8_t i = 1; i < NUM_APPLICATION_PRESETS; i++) {
        const ApplicationPreset* preset = &APPLICATION_PRESETS[i];
        MeasurementType type = (MeasurementType)pgm_read_byte(&preset->expectedMeasurementType);
        if (type != MEASURE_TEMPERATURE && type != MEASURE_PRESSURE && type != MEASURE_VOLTAGE) {
            const char* name = READ_APP_NAME(preset);
            const char* label = READ_APP_LABEL(preset);
            msg.control.print(F("  "));
            msg.control.print((__FlashStringHelper*)name);
            uint8_t nameLen = strlen_P(name);
            for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
            msg.control.print(F("- "));
            msg.control.println((__FlashStringHelper*)label);
        }
    }
}

/**
 * List sensors - supports three modes:
 *   1. No filter: Show category summary with sensor counts
 *   2. Category filter: Show sensors in that category (e.g., "NTC_THERMISTOR", "NTC")
 *   3. Measurement filter: Show all sensors of that type (e.g., "TEMPERATURE", "PRESSURE")
 */
void listSensors(const char* filter) {
    // Helper lambda to print a single sensor
    auto printSensor = [](const SensorInfo* sensor) {
        const char* name = READ_SENSOR_NAME(sensor);
        const char* label = (const char*)pgm_read_ptr(&sensor->label);
        msg.control.print(F("  "));
        msg.control.print((__FlashStringHelper*)name);
        uint8_t nameLen = strlen_P(name);
        for (uint8_t j = nameLen; j < 24; j++) msg.control.write(' ');
        msg.control.print(F("- "));
        if (label) msg.control.println((__FlashStringHelper*)label);
        else msg.control.println();
    };

    // No filter: Show category summary
    if (filter == nullptr) {
        msg.control.println(F("Sensor Categories:"));
        msg.control.println();

        for (uint8_t cat = 0; cat < CAT_COUNT; cat++) {
            uint8_t count = countSensorsInCategory((SensorCategory)cat);
            if (count > 0) {
                const SensorCategoryInfo* catInfo = getCategoryInfo((SensorCategory)cat);
                const char* catName = READ_CATEGORY_NAME(catInfo);
                const char* catLabel = READ_CATEGORY_LABEL(catInfo);

                msg.control.print(F("  "));
                msg.control.print((__FlashStringHelper*)catName);
                uint8_t nameLen = strlen_P(catName);
                for (uint8_t j = nameLen; j < 20; j++) msg.control.write(' ');
                msg.control.print(F("- "));
                msg.control.print((__FlashStringHelper*)catLabel);
                msg.control.print(F(" ("));
                msg.control.print(count);
                msg.control.println(F(")"));
            }
        }

        msg.control.println();
        msg.control.println(F("Measurement Type Filters:"));
        msg.control.print(F("  TEMPERATURE           - All temperature sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_TEMPERATURE));
        msg.control.println(F(")"));
        msg.control.print(F("  PRESSURE              - All pressure sensors ("));
        msg.control.print(countSensorsByMeasurementType(MEASURE_PRESSURE));
        msg.control.println(F(")"));

        msg.control.println();
        msg.control.println(F("Usage: LIST SENSORS <category>    - Show sensors in category"));
        msg.control.println(F("       LIST SENSORS TEMPERATURE   - Show all temperature sensors"));
        msg.control.println(F("       SET <pin> SENSOR <category> <preset>"));
        msg.control.println();
        msg.control.println(F("Aliases: NTC -> THERMISTOR"));
        msg.control.println(F("         TC -> THERMOCOUPLE"));
        msg.control.println(F("         RPM, SPEED -> FREQUENCY"));
        return;
    }

    // Check if filter is a measurement type (TEMPERATURE, PRESSURE)
    int8_t measFilter = getMeasurementTypeFilter(filter);
    if (measFilter >= 0) {
        MeasurementType measType = (MeasurementType)measFilter;
        const char* typeName = (measType == MEASURE_TEMPERATURE) ? "Temperature" : "Pressure";

        msg.control.print(F("All "));
        msg.control.print(typeName);
        msg.control.println(F(" Sensors:"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            MeasurementType sensorMeasType = (MeasurementType)pgm_read_byte(&sensor->measurementType);
            if (sensorMeasType == measType) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        return;
    }

    // Check if filter is a category name or alias
    SensorCategory cat = getCategoryByName(filter);
    if (cat < CAT_COUNT) {
        const SensorCategoryInfo* catInfo = getCategoryInfo(cat);
        const char* catLabel = READ_CATEGORY_LABEL(catInfo);

        msg.control.print((__FlashStringHelper*)catLabel);
        msg.control.println(F(":"));
        msg.control.println();

        for (uint8_t i = 1; i < NUM_SENSORS; i++) {
            const SensorInfo* sensor = &SENSOR_LIBRARY[i];
            if (pgm_read_ptr(&sensor->label) == nullptr) continue;

            if (getSensorCategory(i) == cat) {
                printSensor(sensor);
            }
        }

        msg.control.println();
        msg.control.println(F("Usage: SET <pin> SENSOR <category> <preset>"));
        if (cat == CAT_ENVIRONMENTAL) {
            msg.control.println(F("Note: Use 'I2C' for pin, e.g., SET I2C AMBIENT_TEMP BME280_TEMP"));
        } else if (cat == CAT_PRESSURE || cat == CAT_THERMISTOR) {
            msg.control.println(F("IMPORTANT: 5V sensors (0.5-4.5V) require voltage dividers for 3.3V systems!"));
        }
        return;
    }

    // Unknown filter
    msg.control.print(F("ERROR: Unknown category or filter '"));
    msg.control.print(filter);
    msg.control.println(F("'"));
    msg.control.println(F("Use: LIST SENSORS to see available categories"));
}
