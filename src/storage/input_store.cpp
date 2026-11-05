/*
 * input_store.cpp - Input/Rule/Action EEPROM persistence (shared config-store arena)
 *
 * Split out of src/inputs/input_manager.cpp by #402 step 2: the EEPROM
 * persistence band (records, checksum, save/load/reset, the stored-config
 * probes #385/#358 added) is coupled to the STORAGE FORMAT, not the Input
 * model, so it lives beside system_config/json_config/sd_manager/bus_needs
 * rather than in src/inputs/. See ARCHITECTURE.md §11.10 for the arena
 * layout and §12.2 for the inputHoldsPin()/storedInputHoldsPin() contract
 * this file implements (source #2 of the three pin-ownership sources).
 *
 * Zero behaviour change from the code this replaces: same format, same
 * EEPROM_VERSION, same validation, same truncation policy.
 */

#include "eeprom_store.h"
#include <string.h>
#include "../config.h"
#include "../version.h"
#include "input_store.h"
#include "../core/watchdog.h"
#include "../inputs/input_manager.h"   // inputs[] / numActiveInputs / Input, inputManagerTableIsLive()
#include "system_config.h"
#include "../sensors/units_registry.h"
#include "../inputs/measurement_compat.h"  // units repair on load (#458)
#include "../sensors/can_sensor_library/obd_output_pids.h"  // obdLengthFor (#516)
#include "../core/message_router.h"   // For msg.control
#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../pinctrl/pin_owner.h"
#include "bus_needs.h"
#include "../rules/rules_manager.h"    // rulesResetTables + rule/action table helpers

// input_store.cpp is a COLD path: saveInputConfig/loadInputConfig/
// resetInputConfig/storedInputBusNeeds/storedInputHoldsPin run only at boot,
// on SAVE, or on RESET — never in the sensor loop. On Teensy 4.x every
// function lands in ITCM (RAM1) by default, allocated in 32 KB blocks, so
// crossing a block boundary costs 32 KB of RAM1 local-variable budget, not
// the few hundred bytes the code occupies. FLASHMEM keeps this module in
// flash where it belongs — same macro json_config.cpp/json_registry.cpp/
// pin_label.cpp already use; a no-op off Teensy. inputHoldsPin() itself is
// NOT marked: it runs during bus acquisition at boot, once per candidate
// pin, and is the keystone §12.2 dispatch — kept in fast RAM1 rather than
// traded away with the rest of this cold-path module.
#ifndef FLASHMEM
#define FLASHMEM
#endif

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
                           // the INCLUDE_RULES_ENGINE build flag — a store written with
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

// ===== EEPROM PERSISTENCE (shared config-store arena) =====

// XOR a record's raw bytes into a checksum. Called over the SAME bytes on save
// (the RAM image handed to EEPROM.put) and load (read back via EEPROM.get), so
// the two always agree byte-for-byte (padding included).
static FLASHMEM uint8_t xorBytes(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) c ^= b[i];
    return c;
}

// How many records of one section fit in the remaining arena budget. Warns
// loudly on truncation (generalizes the legacy input-only message to any
// section), so a user who overfills the store sees exactly what won't persist.
static FLASHMEM uint8_t fitSection(size_t remaining, uint8_t want, size_t recSize,
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
static FLASHMEM void buildInputRecord(const Input& in, InputEEPROM& out) {
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
static FLASHMEM void applyInputRecord(const InputEEPROM& rec, Input& in) {
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
        // A custom override of another type than the sensor reads was
        // accepted before #460; its bytes would now be read as this type's
        // layout. Drop it back to the preset.
        if (in.flags.useCustomCalibration && (CalibrationType)rec.calibrationType != info.calibrationType) {
            in.flags.useCustomCalibration = false;
            memset(&in.customCalibration, 0, sizeof(CalibrationOverride));
        }
        in.calibrationType = info.calibrationType;
        // Units stored before #458 could be of another quantity (°F on a
        // pressure input) and would keep converting wrongly: repair to the
        // default (SYSTEM UNITS, else the application's), as JSON import does.
        // An imported OBD-II PID measures what the PID table says, not the
        // CAN_IMPORT preset's TEMPERATURE (#496); the OBD output conversion
        // reads measurementType, so it must survive a reboot too.
        in.measurementType = inputMeasurementTypeOf(in);
        if (!unitsMatchMeasurement(in.unitsIndex, in.measurementType)) {
            const ApplicationPreset* app = getApplicationByIndex(in.applicationIndex);
            in.unitsIndex = preferredUnitsFor(in.measurementType,
                                              app != nullptr ? READ_APP_DEFAULT_UNITS(app) : 0);
        }
        // A table-defined OBD PID replies with the table's length (#516:
        // EGT 0x78 is 9 bytes, stored as 2 before)
        if (in.obd2pid != 0) in.obd2length = obdLengthFor(in.obd2pid, in.obd2length);
        if (!in.flags.useCustomCalibration) in.presetCalibration = info.defaultCalibration;
        if (info.initFunction) info.initFunction(&in);
    }
    // (Legacy per-input alarm arming removed — alarms are control-engine Rules,
    // armed from rules[] by rulesManagerInit(). The flagsByte alarm bit /
    // min/max are retained as dead fields for layout stability, not evaluated.)
}

FLASHMEM bool saveInputConfig() {
    // EEPROM writes are slow on AVR (~3.3 ms per changed byte) and the watchdog
    // runs in CONFIG now (#541): one scope for the save, plus a feed per record
    // below, so a growing config can't creep up on the AVR's 8 s ceiling.
    WatchdogScope wd(EEPROM_WDT_WINDOW_MS, "EEPROM");
    EepromBatch batch;   // flash-backed stores write here (eeprom_store.h)
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
#if INCLUDE_RULES_ENGINE
    uint8_t persistRules = fitSection(remaining, rulesRuleCount(), sizeof(Rule), F("rules"));
    remaining -= (size_t)persistRules * sizeof(Rule);
    uint8_t persistActions = fitSection(remaining, rulesActionCount(), sizeof(Action), F("actions"));
    remaining -= (size_t)persistActions * sizeof(Action);
    uint8_t persistLinks = fitSection(remaining, rulesActionLinkCount(), sizeof(ActionLink), F("rule links"));
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
        watchdogReset();
        checksum ^= xorBytes(&rec, sizeof(rec));
        addr += EEPROM_INPUT_SIZE;
        savedInputs++;
    }

    uint8_t savedRules = 0, savedActions = 0, savedLinks = 0;
#if INCLUDE_RULES_ENGINE
    for (int i = 0; i < MAX_RULES && savedRules < persistRules; i++) {
        if (rules[i].id == RULES_ID_NONE) continue;
        EEPROM.put(addr, rules[i]);
        watchdogReset();
        checksum ^= xorBytes(&rules[i], sizeof(Rule));
        addr += sizeof(Rule);
        savedRules++;
    }
    for (int i = 0; i < MAX_ACTIONS && savedActions < persistActions; i++) {
        if (actions[i].id == RULES_ID_NONE) continue;
        EEPROM.put(addr, actions[i]);
        watchdogReset();
        checksum ^= xorBytes(&actions[i], sizeof(Action));
        addr += sizeof(Action);
        savedActions++;
    }
    // Links last: a row whose action didn't fit is dropped by the load-time prune,
    // so an over-full store degrades to "some memberships lost", never a dangling ref.
    for (int i = 0; i < MAX_ACTION_LINKS && savedLinks < persistLinks; i++) {
        if (actionLinks[i].actionId == RULES_ID_NONE) continue;
        EEPROM.put(addr, actionLinks[i]);
        watchdogReset();
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
#if INCLUDE_RULES_ENGINE
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

// Read-only pre-scan of the SAME stored records inputHoldsPin() will answer
// from once inputs[] loads, for the WINDOW BEFORE THAT: registerSystemPins()
// (main.cpp, before serial/buses) and the bus bring-up that follows it both
// need a true answer, but loadInputConfig() has not populated inputs[] yet at
// that point in setup() (#385). A direct sibling of storedInputBusNeeds() —
// same header sanity, same bounded record walk, no checksum verification (a
// store that fails full validation is rejected wholesale by loadInputConfig()
// moments later; the worst case here is a false "held" for a config that then
// doesn't load, never a false "free").
//
// One record read into a single stack local, same as storedInputBusNeeds() —
// no RAM buffer, no cache: mega2560's 97%-full budget cannot pay for one.
//
// `excludeRecordIndex` (default 0xFF, "exclude nothing") is loadInputConfig()'s
// own self-exclusion (see its call site): that function validates record i by
// asking this SAME store whether the pin is already held, WHILE record i's own
// bytes are still sitting right there in EEPROM waiting to be applied — with no
// exclusion, every legitimate record would "conflict" with its own not-yet-
// applied self, and get dropped on every single boot. Covered by
// test_loadInputConfig_does_not_self_conflict_its_own_records (test_bus_needs),
// which needs a properly checksummed store to reach the failure — a stub that
// skips the checksum never gets far enough into loadInputConfig() to see it.
// Register/bus bring-up calls before ANY record has been examined pass 0xFF:
// nothing has a "self" to exclude there.
static FLASHMEM bool storedInputHoldsPin(uint8_t pin, uint8_t excludeRecordIndex = 0xFF) {
    ConfigStoreHeader header;
    EEPROM.get(0, header);
    if (header.magic != EEPROM_MAGIC) return false;
    if (header.version != EEPROM_VERSION) return false;
    if ((size_t)EEPROM_HEADER_SIZE + (size_t)header.numInputs * EEPROM_INPUT_SIZE
            > (size_t)SYSTEM_CONFIG_ADDRESS) return false;

    for (uint8_t i = 0; i < header.numInputs; i++) {
        if (i == excludeRecordIndex) continue;
        InputEEPROM rec;
        EEPROM.get(EEPROM_HEADER_SIZE + (size_t)i * EEPROM_INPUT_SIZE, rec);
        if (rec.pin == pin) return true;
    }
    return false;
}

// Set by validateInputConfig() (input_manager.cpp) immediately before calling
// pinsConflict(), and cleared immediately after — see inputStoreSetProbeExclusion()
// (input_store.h) and that call site. Excludes ONE specific input slot from
// inputHoldsPin() below, by IDENTITY — not by pin value. A pin-keyed exclusion
// cannot tell "this pin is only mine" from "this pin is mine AND a squatter's":
// test_refused_on_pin_conflict_leaves_target_intact re-validates slot 0's pin
// while a second, different slot (the squatter) sits on that same pin, and
// that second slot's conflict must still be caught. Only the identity of
// `self` disambiguates that, so the exclusion has to live here, behind the
// probe, rather than as a pin value threaded through pin_owner.cpp (see its
// header comment).
static const Input* excludedFromProbe = nullptr;

void inputStoreSetProbeExclusion(const Input* self) {
    excludedFromProbe = self;
}

// loadInputConfig()'s counterpart to excludedFromProbe above, for the
// boot-window (storedInputHoldsPin) path — see that function's comment.
static uint8_t excludedStoredRecordIndex = 0xFF;

bool inputHoldsPin(uint8_t pin) {
    // 0xFF is the free-slot marker in inputs[], so it is never "held" — and it
    // is also what bus_defaults reports for a pin absent on this platform.
    if (pin == 0xFF) return false;
    // Before inputs[] is live, the STORED configuration is the only truth
    // available (#385) — see the design principle in ARCHITECTURE.md. After,
    // inputs[] is a faster, equally-true cache of the same records.
    if (!inputManagerTableIsLive()) return storedInputHoldsPin(pin, excludedStoredRecordIndex);
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (&inputs[i] == excludedFromProbe) continue;
        if (inputs[i].pin == pin) return true;
    }
    return false;
}

FLASHMEM uint8_t storedInputBusNeeds() {
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

FLASHMEM bool loadInputConfig() {
    // VALIDATE BEFORE MUTATE: read and FULLY verify the header (magic, version,
    // bounds, checksum) against a read-only scan first; touch RAM (inputs[] / the
    // control tables) ONLY once the store is confirmed good. So a failed runtime
    // LOAD — blank, wrong-version, or corrupt EEPROM — is a true no-op: it cannot
    // wipe an unsaved in-RAM config or strand a previously-driven output
    // (review: Codex BLOCKER / Opus SHOULD-FIX). Cold-boot safety for the
    // BSS-zeroed control tables is handled by rulesResetTables() in
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
#if INCLUDE_RULES_ENGINE
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
#if INCLUDE_RULES_ENGINE
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
#if INCLUDE_RULES_ENGINE
    rulesResetTables();
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
        // Routed through the one door (#384): this now also catches a stored
        // pin that a PRECEDING record in this same load already claimed, not
        // just a registry/bus conflict.
        //
        // inputs[] is not live yet (#385), so the input-probe half of that
        // check answers from THIS SAME store via storedInputHoldsPin() — which
        // means record i's own bytes are sitting right there in EEPROM while
        // it is being asked "does anything hold this pin". Exclude index i
        // (this record only, not the whole probe) so it can't self-conflict;
        // an EARLIER record (index < i) that legitimately wants the same pin
        // is still caught, exactly as inputs[]-based comparison would once
        // live.
        excludedStoredRecordIndex = i;
        const bool storedConflict = rec.pin < 0xC0 &&
            pinsConflict(&rec.pin, 1, PIN_RANK_INPUT, "stored input");
        excludedStoredRecordIndex = 0xFF;
        if (storedConflict) {
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

#if INCLUDE_RULES_ENGINE
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
    // rulesSweepActionRules() repairs both halves of the membership so the
    // engine never evaluates a dangling ref. (Re-arming is the caller's job — boot
    // does it via rulesManagerInit; LOAD via the re-arm trio in cmd_load.)
    rulesSweepActionRules();
#endif

    // Terse info string (RAM-resident on AVR); the SAVE confirmation already
    // reports rule/action counts via flash F() strings.
    msg.debug.info(TAG_CONFIG, "Loaded %d inputs from EEPROM", numActiveInputs);
    return true;
}

FLASHMEM void resetInputConfig() {
    // Clear the store header (magic zeroed → next load reverts to blank config).
    ConfigStoreHeader header = {};
    {
        EepromBatch batch;
        EEPROM.put(0, header);
    }

    // Clear inputs
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        inputs[i].pin = 0xFF;
    }
    numActiveInputs = 0;

#if INCLUDE_RULES_ENGINE
    rulesResetTables();  // empty Rule/Action tables too (#296 B4)
#endif

    msg.control.println(F("Configuration reset"));
}
