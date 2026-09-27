# EEPROM Structure and Versioning

**Technical guide to preOBD EEPROM layout and version management**

---

## Table of Contents

1. [Overview](#overview)
2. [EEPROM Layout](#eeprom-layout)
3. [Version History](#version-history)
4. [Input Configuration Storage](#input-configuration-storage)
5. [Control-Engine Tables (Rule / Action)](#control-engine-tables-rule--action)
6. [System Configuration Storage](#system-configuration-storage)
7. [Checksum Validation](#checksum-validation)
8. [Version Migration](#version-migration)
9. [EEPROM Wear Management](#eeprom-wear-management)
10. [Troubleshooting](#troubleshooting)

---

## Overview

preOBD uses EEPROM (Electrically Erasable Programmable Read-Only Memory) to persist configuration across power cycles. The EEPROM structure uses **hash-based storage** for portability across firmware versions.

**Key Concepts:**
- **Versioned storage:** Each EEPROM write includes version number
- **Hash-based:** Sensor/application names stored as hashes, not indices
- **Checksum validation:** Detects corruption from power loss or wear
- **Auto-reset:** Incompatible versions trigger clean reset
- **Config-store arena:** inputs + control-engine Rules + Actions share one front-growing region behind a single header; `SystemConfig` is a separate block anchored at the end

---

## EEPROM Layout

### Two-Block Model

preOBD splits EEPROM into two blocks that grow toward each other:

- **Config-store arena** grows up from address 0: one `ConfigStoreHeader` followed by three packed record sections — `InputEEPROM[]`, then control-engine `Rule[]`, then `Action[]` (the latter two present on `INCLUDE_RULES_ENGINE` builds). All three sections draw from one shared budget.
- **SystemConfig** is anchored at the **end** of EEPROM, so its address adapts to the platform automatically.

```
┌──────────────────────────────────────────────────────────────┐
│ 0x0000          ConfigStoreHeader (16 bytes)                 │
│ 0x0010 …        InputEEPROM[0 … numInputs−1]                 │
│                 Rule[0 … numRules−1]                         │
│                 Action[0 … numActions−1]                     │
│                 (free space)                                 │
│ END − sizeof(SystemConfig)                                   │
│                 SystemConfig (~140 bytes, grows with schema) │
│ E2END                                                        │
└──────────────────────────────────────────────────────────────┘
```

```cpp
constexpr size_t SYSTEM_CONFIG_ADDRESS =
    EEPROM_TOTAL_BYTES - sizeof(SystemConfig);

// Worst-case input ceiling (if inputs were the only section). Rules and actions
// share the same budget, so the live input limit at save time may be lower.
constexpr size_t MAX_EEPROM_INPUTS =
    (SYSTEM_CONFIG_ADDRESS - sizeof(ConfigStoreHeader)) / sizeof(InputEEPROM);
```

A compile-time `static_assert` prevents the two regions from overlapping.

**Shared budget + loud truncation.** Because inputs, rules and actions compete for the same space, `saveInputConfig()` fits the sections in priority order **inputs > rules > actions** (a rule needs its source input to evaluate; an action needs its rule) and warns on the control plane for any section it has to truncate — never a silent drop. This generalizes the original input-only truncation to the whole store.

**Why anchored at the end?** Earlier firmware used a hardcoded `SYSTEM_CONFIG_ADDRESS`. On platforms with smaller EEPROM than expected (Teensy 4.0, 1080 bytes) the hardcoded address fell past `E2END` and writes silently failed. Anchoring SystemConfig at the end makes the layout self-adjust per platform.

> **Why are the Rule/Action tables in the arena rather than in `SystemConfig`?** They used to be fixed-size arrays embedded in `SystemConfig`, sized by an arbitrary `MAX_RULES`/`MAX_ACTIONS` cap. That capped the table far below `MAX_INPUTS` and made auto-seeded alarm Rules fail silently when full. As standalone RAM globals persisted in the shared arena, the persistence limit is the EEPROM budget (loud on overflow), and the RAM cap is decoupled from input count — see [Control-Engine Tables](#control-engine-tables-rule--action).

### Platform Capacity

`MAX_INPUTS` is a **RAM** limit (number of `Input` structs allocated). `MAX_EEPROM_INPUTS` is a **persistence** limit derived from EEPROM size. They can differ:

| Platform | EEPROM | `MAX_INPUTS` (RAM) | `MAX_EEPROM_INPUTS` (persists) |
|----------|--------|--------------------|--------------------------------|
| Teensy 4.1 | 4284 bytes | 40 | ~40 (full RAM set persists) |
| Teensy 4.0 | 1080 bytes | 40 | ~10 (extras truncated on save) |
| Arduino Mega | 4096 bytes | 40 | ~40 |
| ESP32 | Emulated, configurable | 40 | depends on partition |

When `numActiveInputs > MAX_EEPROM_INPUTS`, `saveInputConfig()` writes the first `MAX_EEPROM_INPUTS` inputs and prints a warning to the control plane:

```
⚠ EEPROM holds 10 inputs; truncating 14 — extras will not persist across reboot
```

The extra inputs continue to function until reboot — they're configured in RAM, just not written to EEPROM.

---

## Version History

There are two independent version numbers:

- **`EEPROM_VERSION`** (currently `7`) — config-store arena schema (`ConfigStoreHeader` + `InputEEPROM[]` + `Rule[]` + `Action[]`).
- **`SYSTEM_CONFIG_VERSION`** (currently `13`) — `SystemConfig` schema. Bumped whenever the struct's layout or address changes.

A version mismatch on either block is non-fatal: the affected block resets to defaults on next boot, the user re-saves, and operation continues. The two blocks version independently — bumping `SYSTEM_CONFIG_VERSION` does not invalidate the arena and vice versa.

### Config-Store (Input Block) Version Progression

| Version | Firmware | Key Changes |
|---------|----------|-------------|
| **v1** | v0.3.x | Enum-based system; stored sensor/app indices |
| **v2** | v0.4.0+ | Registry architecture; switched to hash-based storage |
| **v3** | — | Internal layout iterations |
| **v4** | — | Added `divider_ratio` to the per-input record |
| **v5** | — | `CalibrationOverride` AVR union size corrected (18→16 bytes) |
| **v6** | — | Header became `ConfigStoreHeader`; the arena now also packs control-engine `Rule[]` + `Action[]` after the inputs (single checksum over all records) |
| **v7** | current | `Rule` grows `warnLo`/`warnHi` (per-side configurable warning/"approach tier" edge, `RuleWarnMode` packed into `Rule.flags`) |

### SystemConfig Version Progression

| Version | Key Changes |
|---------|-------------|
| **v3** | Earlier baseline (output enables, display, units, intervals, pins, sea-level) |
| **v4** | Added transport router config |
| **v5** | Added relay config (`INCLUDE_RELAY_OUTPUT` builds) |
| **v6** | Added log filter config |
| **…**  | Bus config, serial port config |
| **v10** | Layout shift — `SystemConfig` now anchored at the end of EEPROM (per-platform address) |
| **v11** | Removed system pin fields (`modeButtonPin`, `buzzerPin`, `canCSPin`, `canIntPin`, `sdCSPin`, `testModePin`) — pins are compile-time constants in the board profile, not runtime state |
| **v12** | Control engine — embedded `Rule rules[]` / `Action actions[]` tables (replaced `RelayConfig relays[]`) |
| **v13** | Removed those tables again — Rule/Action moved OUT to the front config-store arena; `SystemConfig` now holds only scalar settings |

> **Note:** v9→v10 is a pure address change, not a struct change. T4.1 users who upgrade past v10 will see SystemConfig reset to defaults on first boot and need to re-`SAVE`. Input configs are unaffected.
>
> **v10→v11:** Struct change (pin fields removed). SystemConfig resets to defaults on first boot. Re-`SAVE` to persist transport routing and output settings. Input configs are unaffected.
>
> **v12→v13:** Struct shrank (Rule/Action tables moved to the arena). SystemConfig resets to defaults on first boot; the arena also bumps `EEPROM_VERSION` 5→6 and resets, so configured Rules/Actions need re-creating. Re-`SAVE` once after upgrade.
>
> **`EEPROM_VERSION` v6→v7:** `Rule` grew `warnLo`/`warnHi` (24→32 B ARM, 21→29 B AVR) to carry a per-side configurable warning threshold. Clean arena reset on upgrade — inputs, Rules and Actions all need re-creating. **Back up first with `SYSTEM DUMP JSON`, then restore with `JSON IMPORT` after upgrading** (a pre-v7 dump imports cleanly; every warn mode comes back `AUTO`, matching pre-upgrade behaviour).

### Detail: Version 1 (v0.3.x - Deprecated)

**Characteristics:**
- Sensor and application stored as **enum indices**
- No hash-based lookups
- Fragile: Adding sensors mid-enum broke existing EEPROMs

**Problem:**
If you inserted a new sensor into the enum:
```cpp
enum Sensor {
    SENSOR_NONE = 0,
    SENSOR_NEW_SENSOR = 1,  // INSERTED HERE!
    SENSOR_MAX6675 = 2,     // Was 1, now 2 - BREAKS EEPROM!
};
```

All saved configurations became invalid.

---

### Detail: Version 2 (v0.4.0+; superseded — see the tables above for the current layout)

**Introduced:** Registry architecture (hash-based storage)

**Key Changes:**
- Replaced enum indices with **16-bit hash values**
- Hash computed from sensor/application **name strings**
- Portable across firmware versions (hash stays constant)
- Added `unitsHash` for unit conversion

**Benefits:**
- Adding sensor doesn't break EEPROM (hash remains same)
- Can reorder registry without affecting saved configs
- Foundation for future JSON-based custom sensors

---

## Input Configuration Storage

### Config-Store Header (16 bytes @ 0x0000)

```cpp
struct ConfigStoreHeader {
    uint32_t magic;       // 0x4F454D53 ("OEMS" in ASCII)
    uint16_t version;     // EEPROM_VERSION (currently 7)
    uint8_t  numInputs;   // records persisted in each section…
    uint8_t  numRules;
    uint8_t  numActions;
    uint8_t  checksum;    // XOR over every persisted record byte (all 3 sections)
    uint8_t  reserved[6]; // future record-type counts; pads the header to exactly 16 bytes
};
```

**Purpose:**
- **Magic:** Detect uninitialized EEPROM (0xFFFFFFFF on new chips)
- **Version:** Trigger reset if mismatch
- **numInputs / numRules / numActions:** How many records of each type follow, in that order. These are the *stored* counts — load verifies the checksum over them **before** clamping into the running firmware's RAM caps, so a build with smaller caps still validates a store written by a larger one.
- **checksum:** Single XOR over all persisted record bytes (was per-input-only in v5).
- **reserved[6]:** Brings the struct to exactly 16 bytes on every platform (no indeterminate padding) and reserves room for future record-type counts.

### InputEEPROM Structure (~84 bytes each, depends on alignment)

```cpp
struct InputEEPROM {
    // Hardware (1 byte)
    uint8_t pin;

    // User Configuration - stored as hashes (46 bytes)
    char abbrName[8];              // "CHT", "OIL"
    char displayName[32];          // "Cylinder Head Temp"
    uint16_t applicationHash;      // djb2_hash of application name
    uint16_t sensorHash;           // djb2_hash of sensor name
    uint16_t unitsHash;            // djb2_hash of units name

    // Alarm Thresholds (8 bytes)
    float minValue;
    float maxValue;

    // OBDII (2 bytes)
    uint8_t obd2pid;
    uint8_t obd2length;

    // Flags (1 byte)
    uint8_t flagsByte;             // Packed: enabled, alarm, display, useCustomCal

    // Calibration (17 bytes)
    uint8_t calibrationType;
    CalibrationOverride customCalibration;  // 16-byte union
};
```

**Size:** ~84 bytes per input on Teensy (ARM alignment); slightly less on AVR. Use `sizeof(InputEEPROM)` at runtime — the layout is alignment-sensitive.

### Input Slots Layout

Slots are packed back-to-back starting at `EEPROM_HEADER_SIZE` (0x0010). The number that fits is bounded by `MAX_EEPROM_INPUTS` (the gap between the header and `SYSTEM_CONFIG_ADDRESS`, shared with the rule/action sections — see [Two-Block Model](#two-block-model)).

```
Address                                  | Content
-----------------------------------------|---------------------------
0x0010                                   | InputEEPROM[0]
0x0010 + 1·sizeof(InputEEPROM)           | InputEEPROM[1]
…                                        | …
(then Rule[0..], then Action[0..])       | control-engine sections
```

### Flags Byte Packing

```cpp
flagsByte =
    (isEnabled           ? 0x01 : 0) |
    (alarm               ? 0x02 : 0) |
    (display             ? 0x04 : 0) |
    (useCustomCalibration? 0x08 : 0);
```

### Calibration Override Union

```cpp
union CalibrationOverride {
    ThermistorSteinhartCalibration steinhart;
    PressureLinearCalibration pressureLinear;
    PressurePolynomialCalibration pressurePolynomial;
    RPMCalibration rpm;
    byte raw[16];  // 16-byte union
};
```

**Size:** 16 bytes per input (always allocated, even if unused)

---

## Control-Engine Tables (Rule / Action)

On `INCLUDE_RULES_ENGINE` builds, the `Rule[]` and `Action[]` sections follow the inputs in the same arena. The records are stored **directly** (no hash mirror like inputs): a `Rule` keys off the source input's stable **pin**, and Actions reference Rules by stable **id**, neither of which shifts across firmware versions.

- **Live tables are RAM globals** — `rules[MAX_RULES]` / `actions[MAX_ACTIONS]` in `rules_manager.cpp` (mirroring `Input inputs[]`), **not** members of `SystemConfig`.
- **RAM caps** come from `RULES_RAM_{RULES,ACTIONS}_MAX` (`rules_types.h`): a uniform **64** on 32-bit boards, overridden **down to 2** on AVR (Mega), with an absolute **254** id-space ceiling (ids are `uint8_t`, `0xFF` = empty-slot sentinel). Decoupled from `MAX_INPUTS`, so the engine is N:N (many rules per input, many actions per rule).
- **Empty slot = `id == 0xFF`.** The tables are BSS-zeroed, and `id == 0` is a *valid* id, so `rulesResetTables()` stamps every slot empty before they're trusted (cold boot, factory reset, any failed load) — otherwise a blank table would look full of phantom record #0.
- **Persistence is bounded by the EEPROM budget, not the RAM cap.** Only non-empty records are written (compacted), and the arena truncates loudly if rules/actions don't fit after the inputs.
- On load, a **dangling-`ruleRef` sweep** reverts any Action whose Rule didn't load to manual-only (mirrors the `RULE DEL` invariant and the JSON import path).

`Rule` is 29 B (AVR) / 32 B (ARM) as of `EEPROM_VERSION` v7 (`warnLo`/`warnHi`; was 21 B / 24 B at v6); `Action` is 6 B. As with `InputEEPROM`, use `sizeof()` — layout is alignment-sensitive and each device only ever reads its own EEPROM.

### Validate-before-mutate load

`loadInputConfig()` verifies the header (magic, version, arena bounds, checksum) via a **read-only** scan and only touches RAM (`inputs[]` and the control tables) once the store is confirmed good. A failed runtime `LOAD` (blank / wrong-version / corrupt EEPROM) is therefore a true no-op — it cannot wipe an unsaved in-RAM config or strand a driven output. `cmd_load` gates the control re-arm trio (`rulesManagerInit` + `rulesActionsInit` + `rulesSyncActionPins`) on a successful load and reports honestly rather than always claiming success.

---

## System Configuration Storage

### SystemConfig Structure (~140 bytes, anchored at end of EEPROM)

```cpp
struct SystemConfig {
    // Header (4 bytes)
    uint16_t magic;              // 0x5343 ("SC")
    uint8_t version;             // SYSTEM_CONFIG_VERSION (currently 13)
    uint8_t checksum;            // XOR checksum

    // Output Modules (NUM_OUTPUTS = 7: CAN, RealDash, Serial, SD, Alarm, Relay, ELM327)
    uint8_t outputEnabled[NUM_OUTPUTS];
    uint16_t outputInterval[NUM_OUTPUTS];

    // Display, units, intervals, sea-level pressure
    // (see src/storage/system_config.h for the full layout)
    // Note: hardware pin assignments are NOT stored here — they are compile-time
    // constants defined in the board definition (boards/<board>/board.h)

    // Transport router (v4+) — control/data/debug primary+secondary, BT
    struct { /* ... */ } router;

    // NB (v13): the control-engine Rule/Action tables are NO LONGER here —
    // they live in the front config-store arena (see Control-Engine Tables
    // above). v12 embedded them; v13 moved them out so the table size is
    // bounded by the EEPROM budget, not an arbitrary cap.

    // Bus + serial port config — which I2C/SPI/CAN bus, which serial ports
    BusConfig buses;
    SerialPortConfig serial;

    // Log filter (v6+) — per-plane log levels and tag bitmap
    struct { /* ... */ } logFilter;
};
```

The struct above is a sketch — `SystemConfig` has grown across versions and may grow again. Treat [`src/storage/system_config.h`](../../../src/storage/system_config.h) as authoritative.

**Location:** `EEPROM_TOTAL_BYTES − sizeof(SystemConfig)` (per platform). On Teensy 4.1 (4284-byte EEPROM) this is roughly `0x1080`; on Teensy 4.0 (1080-byte EEPROM) it's roughly `0x03B0`. The exact address is computed at compile time from `E2END`.

**Default Values:**
Set from board profile and `config.h` at first boot:
- Feature flags (`INCLUDE_CAN`, etc.) determine which output slots default ON/OFF
- `DEFAULT_TEMPERATURE_UNITS`, `DEFAULT_PRESSURE_UNITS` etc. from `config.h`
- Hardware pin assignments are NOT stored — they are always read directly from the board profile at compile time

---

## Checksum Validation

### Config-Store Checksum

**Algorithm:** a single XOR over every persisted record byte across all three sections (inputs, then rules, then actions), stored in `ConfigStoreHeader.checksum`. Save XORs the exact RAM image handed to `EEPROM.put`; load XORs the bytes read back via `EEPROM.get` — byte-for-byte symmetric, including struct padding (inputs are zeroed before serialization; rules/actions round-trip as fixed-size PODs).

```cpp
// Save: accumulate as each record is written.
checksum ^= xorBytes(&rec, sizeof(rec));   // per InputEEPROM / Rule / Action
// …then header.checksum = checksum; EEPROM.put(0, header);
```

**Validation on load** is computed over the header's **stored** counts *before* clamping into this firmware's RAM caps (so a smaller-cap build still accepts a valid store):

```cpp
// Walk inputs → rules → actions contiguously, XOR every byte, compare.
if (computed != header.checksum) {
    msg.control.println(F("ERROR: EEPROM checksum mismatch! Configuration corrupted."));
    return false;  // validate-before-mutate: no RAM touched, prior config intact
}
```

### SystemConfig Checksum

**Algorithm:** XOR of all SystemConfig fields (excluding checksum itself)
```cpp
uint8_t calculateChecksum(SystemConfig* cfg) {
    uint8_t checksum = 0;
    byte* ptr = (byte*)cfg;

    // XOR all bytes except the checksum field
    for (size_t i = 0; i < offsetof(SystemConfig, checksum); i++) {
        checksum ^= ptr[i];
    }
    for (size_t i = offsetof(SystemConfig, checksum) + 1;
         i < sizeof(SystemConfig); i++) {
        checksum ^= ptr[i];
    }

    return checksum;
}
```

**Why Checksums?**
- **Power loss during write:** Partial write detected
- **EEPROM wear:** Bits flipping detected
- **Memory corruption:** Random bit errors caught

---

## Version Migration

### Boot-Time Version Check

**Sequence:**
1. Read EEPROM Header
2. Check magic number (0x4F454D53)
3. Compare version to `EEPROM_VERSION` constant
4. If mismatch → Reset to defaults

**Code Flow** (validate-before-mutate — no RAM is touched until the header *and* checksum pass):
```cpp
ConfigStoreHeader header;
EEPROM.get(0, header);

if (header.magic != EEPROM_MAGIC) return false;        // uninitialized → defaults
if (header.version != EEPROM_VERSION) return false;    // mismatch → defaults
if (storedBytes(header) > SYSTEM_CONFIG_ADDRESS) return false;  // won't fit this arena
// … read-only checksum walk over header's stored counts …
if (computed != header.checksum) return false;         // corrupt → prior config kept

// Validated: NOW commit — blank inputs + rulesResetTables(), then load records.
```

Cold-boot safety for the BSS-zeroed control tables comes from `rulesResetTables()` in `initInputManager()` (not the loader), so a blank/invalid EEPROM still ends with empty — not phantom — Rule/Action tables.

### SystemConfig Version Check

**Similar process:**
```cpp
SystemConfig temp;
EEPROM.get(SYSTEM_CONFIG_ADDRESS, temp);

if (temp.magic != SYSTEM_CONFIG_MAGIC ||
    temp.version != SYSTEM_CONFIG_VERSION) {
    Serial.println("SystemConfig version mismatch");
    resetSystemConfig();  // Load compile-time defaults
    saveSystemConfig();   // Write to EEPROM
}
```

### Why No Auto-Migration?

**Design decision:** Reset instead of migrate

**Reasons:**
1. **Simplicity:** Migration code complex and error-prone
2. **Safety:** Partial migration worse than clean slate
3. **Infrequency:** Firmware updates rare, re-config acceptable
4. **Clarity:** User knows config was reset, not silently altered

---

## EEPROM Wear Management

### Write Endurance

**Typical EEPROM specs:**
- 100,000 write cycles per cell (AVR)
- 10,000-100,000 cycles (ESP32 flash emulation)

### Write Patterns

**Low-frequency writes (safe):**
- User issues `SAVE` command (manual)
- Firmware update (version bump resets anyway)

**preOBD does NOT:**
- Auto-save on every change
- Write sensor readings to EEPROM
- Use EEPROM for data logging

### Best Practices

1. **Batch changes:** Configure all inputs, then `SAVE` once
2. **Use SD for logging:** EEPROM is for config only
3. **Avoid power cycling:** Complete `SAVE` before power-off

---

## Troubleshooting

### "EEPROM uninitialized"

**Cause:** First boot or EEPROM erased
**Solution:** Normal - configure via serial commands, then `SAVE`

### "EEPROM version mismatch"

**Cause:** Firmware updated with new EEPROM format
**Solution:** Re-configure via serial commands, then `SAVE`

### "EEPROM checksum mismatch"

**Cause:** Corruption (power loss during write, EEPROM wear)
**Solution:**
1. Run `RESET` to clear corrupted data
2. Re-configure via serial commands
3. `SAVE` to write fresh config

### Configuration Lost After Power Cycle

**Possible causes:**
1. Forgot to run `SAVE` command
2. Power lost during `SAVE`
3. EEPROM wear (rare)
4. Firmware upgrade bumped `EEPROM_VERSION` or `SYSTEM_CONFIG_VERSION` — affected block resets to defaults; re-`SAVE` once.
5. **Small-EEPROM platform truncation** — on Teensy 4.0 only the first `MAX_EEPROM_INPUTS` (~10) inputs persist. If `SAVE` printed `⚠ EEPROM holds N inputs; truncating M`, inputs past slot N are not written and disappear on reboot.

**Solution:**
1. Always run `SAVE` after configuring
2. Verify with `SYSTEM DUMP` before power-off
3. On T4.0, keep input count ≤ `MAX_EEPROM_INPUTS` or accept that extras are RAM-only

### Output enable flags reset to defaults each boot

Symptom: `OUTPUT SERIAL ENABLE` + `SAVE` works for the session, but the output reverts to its default state on reboot.

This was a real bug fixed in `SYSTEM_CONFIG_VERSION` 9 and 10:

- **v9**: hardcoded `SYSTEM_CONFIG_ADDRESS` overlapped the input block once ≥12 inputs were configured; saving inputs corrupted SystemConfig's magic number, and load fell back to defaults every boot.
- **v10**: on Teensy 4.0 the hardcoded address was past `E2END` entirely, so SystemConfig writes silently failed. Anchoring at the end of EEPROM fixed both.

If you're seeing this on current firmware, run `SAVE` once after upgrade to migrate SystemConfig to its new (per-platform) address.

---

## See Also

- [Registry System](REGISTRY_SYSTEM.md) - Hash-based lookup architecture
- [Adding Sensors](../ADDING_SENSORS.md) - Adding new sensor types
- [Serial Commands](../../reference/SERIAL_COMMANDS.md) - SAVE, LOAD, RESET commands

