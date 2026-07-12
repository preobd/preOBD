# Control Engine v1 — Implementation Plan (Option A)

> **⚠️ PRIVATE REPO ONLY** — like `ARCHITECTURE.md` / `TODO.md`, excluded from the public snapshot.
>
> **Status:** pre-implementation. This is the concrete v1 plan that realizes the locked model in **`ARCHITECTURE.md` §11**. Read §11 first — this document does not restate the vocabulary, cardinality, failsafe taxonomy, or end-goal; it commits the *physical* decisions §11 left open (EEPROM layout, struct byte widths, migration mechanics, init wiring) so the config-version bump lands right the first time.

---

## 0. Scope

v1 = **N:1** control engine per §11.6 (each Action triggered by ≤1 Rule; a Rule fans out to many Actions). Clean EEPROM break with a **clean reset** (no preserving migration — see §3, decided 2026-06-20), **`RELAY` deprecated** in favor of `RULE`/`ACTION` + an `ActionBroadcaster` (revised 2026-06-21 — see §5; not a facade), built-in Annunciator. M:N + compound conditions are explicitly **out of scope** (§11.6 end-goal) but the layout must keep them *additive* (stable ids, no second EEPROM break).

---

## 1. EEPROM layout decision (Option A — LOCKED)

The two legacy sources the migration consumes live in **two separate EEPROM regions with two independent version bytes**:

| Legacy source | Region | Version byte |
|---|---|---|
| Alarm thresholds `minValue`/`maxValue` (in `InputEEPROM`) | Input block, grows **up** from addr 0 | `EEPROMHeader.version` = `EEPROM_VERSION` **5** (`src/version.h:50`) |
| `RelayConfig relays[]` | `SystemConfig`, anchored at **end** of EEPROM | `SystemConfig.version` = `SYSTEM_CONFIG_VERSION` **11** |

**Decision: both new tables (`rules[]`, `actions[]`) live inside `SystemConfig`**, replacing `RelayConfig relays[]`. Consequences:

- **One version byte.** Bump `SYSTEM_CONFIG_VERSION 11 → 12`. On boot, old v11 config hits the existing version-mismatch path and resets to defaults (§3) — no migration, no address-shift hazard.
- **Input region is byte-for-byte unchanged and PRESERVED.** `InputEEPROM` keeps `minValue`/`maxValue` as **dead fields** in v1; `EEPROM_VERSION` stays **5**. Because only `SystemConfig` bumps, **users keep all input/sensor/calibration config** across the upgrade — they only re-create what were alarms/relays (now Rules/Actions). Stripping the dead bytes is a deferred cosmetic `EEPROM_VERSION` bump. The runtime consumers that read `Input.minValue/maxValue` directly (§6.5 finding) are redirected to the canonical Rule.
- Actions already lived here (`RelayConfig` was in `SystemConfig`), so the clean break is localized to one struct.

### 1.1 EEPROM budget (the open sizing concern — flagged for review)

Teensy 4.0/4.1 expose only **1080 bytes** of emulated EEPROM. `MAX_EEPROM_INPUTS = (SYSTEM_CONFIG_ADDRESS − 8) / sizeof(InputEEPROM)` (~84 B/input on ARM), and `SYSTEM_CONFIG_ADDRESS = EEPROM_TOTAL_BYTES − sizeof(SystemConfig)`. So **every byte added to `SystemConfig` directly reduces input persistence capacity** on the binding-constraint platform.

Delta with the §2 struct widths (NO name fields — decided 2026-06-20), caps 8/8:

```
removed:  relays[2]            =  32 B
added:    rules[8]   (~24 B ea)= 192 B
          actions[8] (~ 6 B ea)=  48 B
net SystemConfig growth        = +208 B
```

On Teensy 4.x (1080 B) that drops `MAX_EEPROM_INPUTS` from ~11 to ~9 — acceptable. Dropping the `name[]` fields (~160 B) is what keeps **8/8 caps affordable on the small-EEPROM boards**, so no cap reduction is needed: §11.9's defaults (Teensy/STM32 = 8/8, Mega = 2/2 to match `MAX_RELAYS` + AVR RAM) stand. Caps remain per-profile; `static_assert(MAX_EEPROM_INPUTS >= 1)` is the hard floor.

---

## 2. Struct definitions (concrete byte layout — for review)

Gated behind a new `ENABLE_CONTROL_ENGINE` (implied by `ENABLE_ALARMS || ENABLE_RELAY_OUTPUT`). All EEPROM-resident.

```cpp
// --- Rule (EEPROM-resident; ~24 B aligned) ---
// NO name field (decided 2026-06-20): managed by stable `id`, mirroring the
// existing index-based RELAY CLI. RULE LIST renders ground truth from the
// source input's abbrName + condition + severity — no per-rule name needed.
struct Rule {
    uint8_t  id;                     // stable per-record id; 0xFF reserved (§8 Q9). NOT an array slot.
    uint8_t  sourceInput;            // INPUT PIN (existing stable identity), not inputs[] slot — resolves §8 Q9 axis A
    uint8_t  condition;              // RULE_ABOVE | RULE_BELOW | RULE_OUTSIDE_BAND  (INSIDE_BAND deferred)
    uint8_t  severity;               // SEV_NONE | SEV_WARNING | SEV_ALARM
    uint8_t  flags;                  // bit0 enabled; rest reserved
    float    limitLo;                // ABOVE/BELOW: the limit; OUTSIDE_BAND: low edge
    float    limitHi;                // OUTSIDE_BAND: high edge; unused otherwise
    float    hysteresis;             // clear-band, applied inward from each limit
    uint16_t debounceMs;             // = legacy persistTime_ms
    uint16_t warmupMs;               // = legacy warmupTime_ms
};

// --- Action (EEPROM-resident; ~6 B) ---  (no name field — managed by `id`)
struct Action {
    uint8_t id;                      // stable per-record id (§8 Q9)
    uint8_t type;                    // ACT_RELAY | ACT_BEEPER | ACT_LED | ACT_DISPLAY
    uint8_t target;                  // pin / relay channel / LED channel
    uint8_t ruleRef;                 // Rule.id that triggers it; 0xFF = manual-only (N:1 in v1)
    uint8_t behavior;                // active-high/low, blink/beep pattern, color
    uint8_t failsafe;               // RELAY only: state when source reading is untrustworthy (§11.3)
};

// --- Runtime (NOT persisted) — lifted verbatim from AlarmState/AlarmContext ---
enum RuleState : uint8_t { RULE_DISABLED, RULE_INIT, RULE_WARMUP, RULE_READY, RULE_ACTIVE };
struct RuleContext {                 // == today's AlarmContext, renamed
    RuleState state;
    uint32_t  stateEntryTime;
    uint32_t  faultStartTime;        // debounce anchor (0 = no violation)
    uint16_t  warmupTime_ms;
    uint16_t  persistTime_ms;
};
```

`RuleState`/`RuleContext` are a **rename** of the existing `AlarmState`/`AlarmContext` (`src/inputs/input.h:138-160`) — they already model warmup gating + debounce-via-`faultStartTime`. `evaluateSeverity()` / `updateInputAlarmState()` (`src/inputs/alarm_logic.cpp`) generalize to operate on a Rule's condition shape instead of the hardwired `minValue<999 / maxValue>-999` pair, but the state machine transitions are unchanged.

The **warning tier stays derived** (§11.2): today `evaluateSeverity()` computes a warning band as `WARNING_THRESHOLD_PERCENT` of the limit (`alarm_logic.cpp:67,77`). A Rule stores one limit; the Annunciator reproduces the two-tier warn/alarm behavior. No stored warning threshold, nothing to migrate.

---

## 3. Migration: clean reset (DECIDED 2026-06-20)

The planning review (§6.5) established that **no EEPROM region has ever preserved config across a version bump** — `loadSystemConfig()` resets to defaults on any version/magic mismatch (`system_config.cpp:344-353, 104-107`), and the input region does the same. A *preserving* migration (§11.7's original shape-selecting design) would be net-new infrastructure fighting the end-anchored address shift and the boot-order constraint (SystemConfig must load before `router.begin()`, so `inputs[]` aren't in RAM yet). **Decision: do not build it for v1.** Embrace the existing, well-understood behavior:

1. Bump `SYSTEM_CONFIG_VERSION 11 → 12`. `SystemConfig` now carries `rules[]`/`actions[]` in place of `relays[]`.
2. On first boot of v1 firmware, the stored v11 config fails the version check → `resetSystemConfig()` → defaults (exactly as every prior `SystemConfig` bump). No migration code, no `SystemConfigV11` legacy struct, no shape-selecting table, **none of the §6.5 hazards**.
3. **Input region untouched (v5):** input/sensor/calibration config is preserved. Old alarm flags + `minValue`/`maxValue` still load into `inputs[]` RAM but are **ignored** by the new engine (no Rules synthesized from them). `minValue`/`maxValue` are dead fields; runtime consumers that read them (§6.5) are redirected to the canonical Rule.

**User-facing impact:** after upgrading, a user re-creates their Rules/Actions (what were alarms + relays) and re-sets any non-default `SystemConfig` scalars (output intervals, etc.) — the same one-time reconfigure every prior version bump already required. Alarm thresholds re-seed automatically from application presets on the next `SET <pin> APPLICATION` (§4 path a). **Release-note this.**

A *preserving* migration (A1) is documented in §6.5 as a clean, deferrable enhancement if users later ask for it — it slots in without changing the v1 layout.

---

## 4. Init paths (the #251 / #254 regression guards — §11.5)

Three **distinct** entry points, must not share a re-init:

- **(a) create-from-template** — `SET <pin> APPLICATION <app>` seeds one canonical Rule from the application default-Rule template, then arms (`INIT→WARMUP→READY`). Enable-when-already-armed is **idempotent** (the #254 fix: early-out on unchanged enabled flag — no re-arm).
- **(b) boot-load** — restores each persisted Rule's config **and** deterministically (re-)arms its `RuleContext`. Uses persisted timings, **not** template re-derivation. This is the #251 fix (boot must arm). Distinct code path from (a).
- **(c) `RULE RESET <id>`** — rewrites config to the application template; **default: re-arm** (operator-initiated config change warrants a fresh warmup).

`Action.failsafe` is an Action field, untouched by (a) and (c) — fail-ON/fail-OFF is sticky across threshold edits (§11.3).

---

## 5. Annunciator, RELAY deprecation, ActionBroadcaster, CLI

- **Annunciator** (§11.4): built-in, maps Rule severity → effect (`NONE`=control-only; `WARNING`=banner+log+`AlarmBroadcaster`, no buzzer; `ALARM`=+buzzer). `getSystemSeverity()` = max severity over Rules in `ACTIVE`; survives. Feeds existing LCD banner + buzzer + `AlarmBroadcaster` (`…0030`).
- **Failsafe verdict taxonomy** (§11.3, locked): only `OPEN`/`SHORTED`/`NO READING`(NaN)/rail-pegged fail safe; `OUT OF RANGE`/`NEAR LIMIT` drive the Rule normally. Aligns with the existing `isnan()→SEVERITY_NORMAL` guard (`alarm_logic.cpp:51`).
- **`RELAY` deprecated + `ActionBroadcaster`** (§11.7, §8 Q11 — revised 2026-06-21): a relay is an Action of type `RELAY`, so there is **no facade**. The `RELAY` CLI config verbs print a pointer to `RULE`/`ACTION`; `relays[]` + the redundant `output_relay` threshold engine are removed (this is the commit that lands the final v12 layout). The BLE `…0031` characteristic / frame `0x05` payload is repurposed `PreobdRelayState` → `PreobdActionState` (id, type, target, triggering rule, on/off, failsafe) with a symmetric inbound manual-toggle write; the webapp relay panel becomes an Actions panel. The Rule-side BLE view is deferred to a later `AlarmBroadcaster` generalization.
- **`RULE` / `ACTION` CLI** (§11.8 grammar, id-based — no names): `ADD` prints the assigned id; `LIST` renders ground truth (Rule: input abbrName + condition + severity; Action: type + target + linked rule id + state). Structural edits CONFIG-gated; manual `ACTION <id> ON|OFF` + all `LIST`/status RUN-allowed.

---

## 6. Testing & build verification

- **Native (`pio test -e native`):** Rule evaluation per condition shape; hysteresis; debounce; warmup state machine; the three init paths (#251/#254 regression guards); annunciator severity rollup; `ActionBroadcaster` `PreobdActionState` pack/inbound round-trip. (No migration suite — clean-reset has no mapping table; a version-bump → reset-to-defaults check belongs with the existing `SystemConfig` tests.) Green before merge.
- **Build verify:** `pio run -e teensy41` + `pio run -e mega2560` (AVR caps 2/2). Confirm `static_assert(MAX_EEPROM_INPUTS >= 1)` holds and the resulting input count is usable on teensy40 (the 1080-byte case, §1.1).

---

## 6.5 Planning review findings (2026-06-20, Codex; Gemini CLI unavailable — free tier deprecated)

The review **rejected §3 as written**. Three compounding problems:

- **BLOCKER — boot order.** `initSystemConfig()` (`main.cpp:232`) runs ~96 lines before `initInputManager()` (`:328`), and SystemConfig load is required *before* `router.begin()` (`:292`, see the `:231` comment). So `inputs[]` are **not** in RAM when SystemConfig loads/migrates. The "input loader runs first" premise is false and cannot be trivially reordered.
- **End-anchor address shift.** `SYSTEM_CONFIG_ADDRESS = EEPROM_TOTAL_BYTES − sizeof(SystemConfig)` (`system_config.h:125`). Growing the struct moves its base address; legacy v11 data sits at the old address, new firmware reads the new one and finds no magic.
- **No migration infrastructure exists.** `loadSystemConfig()` resets to defaults on any version/magic mismatch (`system_config.cpp:344-353, 104-107`); the input region does likewise. **Every prior version bump has silently reset config to defaults.** §11.7's *preserving* migration is therefore net-new for both regions.
- EEPROM budget refined: ~11→~7 inputs on Teensy 4.x (still material) → drop `name[]` and/or lower ARM caps.
- Shape-selecting table validated against real `evaluateSeverity`/`setInputAlarmRange` semantics — correct **provided** migration never routes legacy values through the setter (which would normalize/reject the ±999 one-sided shapes). Three-init-path design validated against current `#251`/`#254` behavior.

### Decision (RESOLVED 2026-06-20): **A2 clean-reset** — see §3.

Because no bump has ever preserved config, there were two honest paths:

- **A1 — preserve (build real migration).** Defer migration to a dedicated phase *after* both regions are in RAM; keep frozen `SystemConfigV11` + legacy `RelayConfig` definitions read at the explicit *old* end-address; resolve relay `inputIndex`→pin via loaded `inputs[]`; write new layout + flip version last (single commit preserved). Materially more code; the only path that keeps a user's custom alarm thresholds + relay config across the upgrade.
- **A2 — clean reset (embrace existing behavior).** Bump versions, reset to defaults, document "reconfigure after upgrade" in release notes. No migration code, no shape-selecting table, none of the ordering/address hazards above. Consistent with every prior bump. Custom thresholds are lost but re-seed from application presets on the next `SET <pin> APPLICATION`. Option A's *layout* is unchanged either way.

§11.7 assumed A1. The review showed A1 costs more than §11.7 implied and A2 is consistent with shipped UX — **resolved to A2** (§3).

## 7. Open items after review

- **[RESOLVED] §1.1 budget.** Decided 2026-06-20: **no `name[]` fields on Rule or Action** — managed by stable `id`, mirroring the existing index-based `RELAY` CLI; `LIST` renders ground truth (input abbrName + condition + severity for Rules; type/target/linked-rule/state for Actions). This keeps 8/8 caps affordable on Teensy 4.x (~11→~9 inputs); §11.9 cap defaults stand.
- **[RESOLVED] `sourceInput = pin`** — the review didn't object; pin is the existing stable input identity. Cross-version *export* portability (djb2 hash) stays deferred (§8 Q9 axis B).
- **[MOOT under A2] migration boot-ordering** — no migration runs, so the `initSystemConfig`-before-`initInputManager` ordering is irrelevant to v1.
- **[CONFIRMED — implementation task] dead `minValue`/`maxValue` consumers.** Codex found live direct readers in `json_config.cpp`, `alarm_broadcaster.cpp`, `test_mode.cpp`, `input_manager.cpp`. These must be redirected to the canonical Rule as part of v1 (not migration — they're runtime reads).
- **[CLEARED] hot-path locality** — no consumer needs to reach the end region on a hot path; Rules are evaluated from RAM.
