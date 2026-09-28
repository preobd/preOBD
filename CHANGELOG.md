# Changelog

All notable changes to preOBD will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `CLEAR <pin> SAVE` (and `CLEAR <pin> CALIBRATION SAVE`) clears and saves to EEPROM in one command; `CLEAR` now reminds you to `SAVE`, and rejects unrecognised trailing words instead of clearing the whole input (#320)
- `RULE <id> SEVERITY INFO|WARNING|ALARM|NONE` changes a rule's severity in place, keeping its id, limits and the relays/actions linked to it (previously only possible by deleting and re-adding the rule) (#470)
- `RULE <id> LIMIT ABOVE|BELOW <limit>` / `OUTSIDE <lo> <hi>` moves any rule's limits in place, keeping its id, severity and the relays/actions linked to it (previously only possible by deleting and re-adding the rule, which dropped those links) (#457)
- Alarm acknowledgement is now per rule and reachable remotely: `ALARM SILENCE` (or the mode button) mutes only the alarms sounding at that moment, `ALARM DURATION <secs|CLEAR>` and `ALARM SCOPE <BUZZER|ALL>` set how long and what it mutes, and `ALARM STATUS` shows what is acknowledged (#433)
- Per-rule configurable warning threshold: `WARN` on `RULE ADD`, `RULE <id> WARN`, and `SET <pin> ALARM WARN` set each side of an alarm rule's pre-trip warning band to AUTO (derived), NONE (skip it), or an explicit value (#425)
- A disconnected/faulted sensor now announces itself on the alarm channel as an INFO-tier FAULT event instead of silently clearing the alarm (#443)
- Webapp: an Acknowledge button on the health headline (the device's `ALARM SILENCE`, with a `SILENCED · buzzer[ + LED]` chip and the device's remaining time read back from `ALARM STATUS`), and alarm values shown in the input's display units at the LCD's precision (`230°F exceeded 220°F`, `1.3 bar`) instead of raw base units to one decimal (#441, #442)
- BLE status now tells connected clients the moment an alarm is acknowledged (at the dash button or from another phone) or the configuration is changed by another client, so the webapp and other Bluetooth clients update immediately instead of polling; needs both the main board and the nRF52840 updated, and older firmware on either side keeps working as before
- The board now reports why it last reset (watchdog, CPU fault, brown-out, reboot, power-on) in `SYSTEM STATUS` and, with register detail, in `VERSION`; an unexpected reset also prints a one-line warning ahead of the boot banner. Teensy 3.x/4.x and ESP32 only (#454)

### Changed
- An acknowledged alarm now stays silent until that alarm clears by default, instead of re-sounding every 30 s; a timed window is still available with `ALARM DURATION <secs>` and a second alarm always sounds regardless (#433 follow-up)
- **Breaking: EEPROM configuration is reset on upgrade.** The stored config layout changed (`EEPROM_VERSION` 6 → 7) to carry the Rule record's new warning-threshold fields; the firmware detects the old version and starts from defaults. Back up your setup with `SYSTEM DUMP JSON` before flashing and restore it afterwards with `JSON IMPORT`.

### Fixed
- The one-line `SET <pin> <application> <sensor>` form (e.g. `SET 6 CHT MAX6675`) now creates the application's default alarm, like `SET <pin> APPLICATION`; inputs configured that way had no alarm at all (#487)
- On-device `HELP` and the CONFIG banner no longer advertise removed commands (`RELOAD`, top-level `RESET`, `RELAY` subcommands, `SET <pin> ALARM WARMUP|PERSIST`) and now say CONFIG keeps sensors running; `DISPLAY TYPE OLED` is refused since no OLED driver exists (#474)
- CAN import now receives OBD-II PIDs with 1-, 3-, 4- or 5-byte replies (coolant temp `0x05`, speed `0x0D`, …); previously only 2-byte PIDs such as RPM were matched and the rest never updated (#473)
- The LCD now shows a reading of exactly 0 (0 oil pressure, 0 RPM, a low float switch, 0 °C) instead of `---`, which now means only that the input hasn't been read yet (#481)
- The default low-coolant / low-oil alarm (`COOLANT_LEVEL`, `OIL_LEVEL` on a float switch) now sounds when the level is low; it previously could never trip and showed a constant warning while the level was fine. A rule restored from an older `SYSTEM DUMP JSON` backup keeps the old band; `RULE RESET <id>` rebuilds it (#477)
- Teensy uploads (`pio run -t upload`) retry the loader once instead of failing on its first write, and CONFIG mode now says the watchdog is still armed on Teensy 4.x instead of claiming it was disabled (#461)
- `SET <pin> CAL PRESSURE_LINEAR` now accepts sensor voltages up to the supply divided by the input's divider ratio, so a 5 V 0.5–4.5 V sender behind a divider can be calibrated on a 3.3 V board (it was capped at 3.3 V), accepts a negative value at the low end (vacuum, sub-zero temperature), and `SET <pin> DIVIDER` refuses a ratio the input's calibration no longer fits under (#467)
- Inputs on the generic NTC thermistor sensors (`GENERIC_NTC_STEINHART`, `GENERIC_NTC_BETA`) are now actually read — they had no read function, so a custom-calibrated generic thermistor never produced a value (#464)
- `SET <pin> CAL <profile>` now refuses a calibration the input's sensor can't read (e.g. STEINHART on a linear pressure sensor), which was accepted and then either ignored or, on RPM/speed/CAN inputs, misread; `CAL PRESET` now really restores the sensor's preset (it could leave a linear sensor on a generic 0.5–4.5 V curve), and such a saved mismatch is dropped at boot (#460)
- `SET <pin> UNITS` and `SYSTEM UNITS` now refuse an unknown unit name instead of silently switching to Celsius, and `SET <pin> UNITS` refuses a unit of the wrong kind (e.g. °F on a pressure input); a configuration import with bad or missing units falls back to the application's default units with a note, and a saved input left with mismatched units by older firmware is repaired at boot (#458)
- `SET <pin> DIVIDER` now refuses a ratio on sensors that ignore it (resistive senders, thermistors, battery-voltage inputs) instead of accepting a setting that has no effect; only linear voltage-output sensors use a divider, and `1.0` still clears it on any input
- Webapp: importing a configuration file over Bluetooth now uses the device's byte-counted import, so the payload is no longer echoed back over the same link — large configs import reliably in about half a second instead of stalling until the device's 45-second timeout (#450)
- Framed BLE link: a large configuration import no longer corrupts the connection — previously a bulk `JSON IMPORT` could lose bytes mid-frame, after which the board stopped answering any Bluetooth command and reconnecting filled the terminal with binary garbage until the board was rebooted (#449)
- Changing an alarm's shape (`SET <pin> ALARM` between MIN / MAX / band) now keeps an explicit `WARN` edge on the side it was set for instead of silently carrying it to the other side, and a mistyped `WARN` value is rejected rather than accepted as 0 (#427, #428)
- BLE alarm events: deleting or disabling a firing alarm now sends a CLEAR that names that rule (clients no longer show it as firing until reconnect), and a client that connects (or reconnects after a drop) while an alarm is already active receives the held alarm on connect instead of after the next transition; `SYSTEM DUMP JSON` reports the alarm rule's `ruleId` per input (#429, #430, #431)
- Webapp: a tripped standalone WARNING-severity rule now reads "exceeded"/"below" instead of "approaching", which is reserved for an alarm rule's pre-trip band (#432)
- nRF52840 coprocessor: a client that subscribes to alarm, action or live-value notifications now receives the current state right away — including a second phone or browser joining while an alarm is already active, which previously showed "all normal" until the next alarm transition (#435)
- `SYSTEM DUMP JSON` / `JSON IMPORT` now carry an input's `SET <pin> DIVIDER` ratio (`"divider"` key); a profile capture or JSON restore previously dropped it silently, leaving the restored input reading wrong by the ratio (#445)

## [0.13.0-beta] - 2026-09-13

### Added
- `SYSTEM DIAG HW` now reports the RGB LED's colour, effect and winning priority, so the LED's state can be read over a serial link instead of only by looking at the board (#419)
- `tools/check_layering.py` reports `src/`'s cross-directory dependencies against a tracked exceptions list, wired into a tracked `.githooks/pre-commit` (#401)

### Changed
- Flash usage drops sharply: the sensor, application and unit registries and their calibration data are now defined once instead of being duplicated into every translation unit that includes them — mega2560 goes from 85.6% to 69.0% of flash (−42 KB), teensy41 −76 KB (#400)
- The CLI says "rules engine" where it used to say "control engine"; the `RULE` and `ACTION` commands, the BLE protocol and all stored configuration are unchanged (#401)
- `src/` is reorganised into purpose-named directories (`core/`, `comms/`, `storage/`, `sensors/`, `pinctrl/`, `drivers/`, `rules/`, `cli/`, …) and the catch-all `src/lib/` is gone — no functional change, but anyone building from source or carrying local patches will see a different tree (#401, #402, #411, #418)

### Fixed
- The CONFIG-mode LED now lights: the normal-state green was claiming a higher priority than the mode indication and had been masking it permanently (#419)

## [0.12.0-beta] - 2026-09-11

### Added
- `SYSTEM DIAG PINS` (and its JSON twin) now annotates an unclaimed platform-default I2C/SPI/CAN pin with its hardware role, so e.g. a Teensy's A4/A5 reading "pulled high" with no bus running shows as the I2C SDA/SCL pull-ups instead of a mystery reading (#304)
- BLE: the Action state frame now reports whether a member Rule is demanding the output, so a client can tell an operator force that is countermanding a live demand (a fan the rules want on, forced off) from one holding against nothing — previously only Warning/Alarm rules were visible to clients, hiding exactly the notify-free control rules that drive fans and shift lights (#348)

### Fixed
- A pin conflict during boot (a stale `wiring.ini`, a build-time collision, or an unstartable serial port) is no longer silently discarded for lack of a transport to print it to — the refusal is now buffered and replayed once the router comes up, so the reason a feature isn't working is actually visible (#396)
- A declared `*_WIRED_BUS` no longer reserves its pins when nothing in the stored configuration needs that bus, so a wired-but-idle I2C/SPI declaration no longer holds SDA/SCL or MOSI/MISO/SCK hostage against inputs and Actions that could otherwise use them (#394)
- `BUS SERIAL <n> ENABLE` no longer persists the port as enabled when the firmware itself refuses it (pin conflict): the config write now happens only after the port actually starts, so a later `SAVE` can't silently reclaim pins from a working Action or bus at the next boot (#390)
- Sensor/application measurement-type compatibility (e.g. refusing a pressure sensor on a temperature application) and custom-calibration validation now live in shared functions every writer calls, closing gaps where JSON import and the CLI's two-step `SET APPLICATION`/`SET SENSOR` form could accept a mismatched or partially-validated configuration the combined-syntax form already refused; `CAL_SPEED` and CAN-imported calibrations now survive a `SYSTEM DUMP JSON` → `JSON IMPORT` round trip instead of exporting as an unimportable record, and system-config import now validates into a staged copy before committing, fixing a null-pointer crash risk and an absent-key-zeroes-the-interval bug (#383, #382)
- Unified the `inputs[]` pin encoding (I2C virtual pins now cap at 13 slots everywhere, matching what could actually be allocated) and closed the pre-config window where a broadcaster could read the sensor/rule/action tables before they were loaded, with a debug-build assert and boot-order test fixtures to catch a future regression at its source instead of on the wire (#368, #369)
- Pin ownership is now resolved from the stored configuration rather than the live tables, so a configured input is visible to the boot-time bus claim instead of only to the CLI (every input was silently invisible before `inputs[]` loaded, and pin 0 looked permanently taken), a bus you have selected but not yet rebooted into can defend its pins against a later `BUS SERIAL` command, enabling a serial port over a configured sensor's pin is refused instead of dropping that input at the next boot, a `*_WIRED_BUS` declaration now outranks a serial port on the copper it names, and a build-time pin collision in `wiring.ini` is reported instead of silently discarding the second pin (#385, #384, #379)
- `pio run -e esp32s3dev`/`esp32s3_hybrid` no longer fail on a clean checkout: `avinabmalla/ESP32_BleSerial` was withdrawn from the PlatformIO registry, so both boards now pin it to a git tag the same way the Teensy boards already pin `FlexCAN_T4`/`WDT_T4` (#380)
- A bus now comes up only if the stored config explicitly selects it or a build-time `I2C_WIRED_BUS`/`SPI_WIRED_BUS`/`CAN_WIRED_BUS` declaration says it's physically wired — closing the last spot where the firmware still guessed a bus instance instead of asking, which used to silently claim Wire (or CAN1's pins on teensy40) out from under configured inputs; a need that goes unmet is now reported with what to run instead (#377)
- A hand-tuned sensor calibration now survives a config save/load instead of silently reverting to the sensor's preset curve: `JSON IMPORT` parsed no calibration parameters at all and reported success, so a mobile-app profile round trip left the input reading wrong; imported calibrations now go through the same validating setters `SET <pin> CAL` uses, a record whose calibration is refused leaves nothing behind instead of an enabled input the summary claims it did not import, and `SYSTEM DUMP JSON` writes coefficients with enough digits to come back bit-exact (#373)
- I2C and SPI sensors now bring their own bus up: the buses a saved configuration needs are resolved from its stored records and the display together, and acquired in one pass before anything loads, so a BME280 or a MAX6675 no longer works only when a display happens to be configured — and every bus, not just I2C, now refuses to start on a pin a configured input already holds (#358)
- BLE: the Action heartbeat now re-sends only the Actions you have configured instead of every slot in the table, so a five-Action board stops spending ~92% of that traffic announcing empty slots to no purpose; deleting an Action still pushes clearing frames for a few seconds afterwards so a connected client drops its tile (#365)
- Importing a config no longer finishes building an input the firmware just refused, and a rejected `SET` on an existing input no longer leaves it rewritten with the new application's units and OBD-II PID or silently re-enabled: `setInputApplication()` populated the slot before validating it, and the JSON import path claimed the pin itself and then discarded the refusal, installing a live sensor read on a bus-reserved pin (#363, #364)
- BLE: slots that never held an input no longer emit a stray "this input is gone" live-value frame — the broadcaster snapshotted slot occupancy before the configuration was loaded, so every still-blank slot looked like an input that had just been removed and was granted the post-removal repeat window (#352)
- BLE: a client subscribing mid-session now catches up on *every* configured Action and input instead of only the first three — the periodic re-send emitted the whole table in one burst, which overran the UART's 64-byte transmit buffer after three frames, and the slots it dropped were never retried because nothing about them had changed; the re-send now resumes where the backpressure stopped it (#350)
- `SET` on a pin a bus has reserved no longer resets the board a couple of seconds after printing its (correct) refusal: the rejected input left a half-built slot marked enabled with no read function installed, which the sensor loop then called into, so probing which pins are free cost the user CONFIG mode and every unsaved change (#361, #353)
- I2C pins are no longer left unreserved when the LCD is fitted: the display used to bring the bus up itself without telling the bus manager, so `SET A5` on a Teensy was accepted for a pin the LCD was clocking and wedged the bus until the watchdog reset the board — configs already saved with such a pin are now dropped on load instead of reset-looping, and `BUS I2C <n>` and `DISPLAY TYPE LCD` now refuse a bus whose SDA/SCL an input already holds instead of silently dropping that input at the next boot (#351, #354)
- A bus no longer comes up on a pin another function already holds: I2C printed a warning and started anyway while SPI and CAN discarded the reservation result outright, so on a Teensy 4.x with Serial4 configured `BUS I2C 1` brought Wire1 up on the UART's own pins (16/17) and reported success — all three now claim their pins as a group *before* the peripheral is started, refuse to start when the claim is denied, and leave nothing half-reserved behind (#359)

### Changed
- Internal: slot occupancy and the post-removal repeat window, which the live-value and Action broadcasters each carried an identical private copy of, now live in the shared broadcaster template, so the four transport bugs that had to be fixed one broadcaster at a time (#337, #349, #350, #352) are ruled out by construction for every broadcaster — nothing changes on the wire
- Build flags now say what they mean: `board.h` declares `SUPPORTS_*` ("this board can"), `boards/resolve.h` derives the `INCLUDE_*` that the firmware tests ("this code is in the image"), and whether a feature is actually *on* stays runtime state — commenting a `SUPPORTS_*` line out of `board.h` is now the documented way to reclaim a feature's flash and RAM (#357)
- A display is no longer assumed just because the board can drive one: boards with no saved configuration now default to no display and declare one with `DISPLAY TYPE LCD`, so a board without a screen keeps its I2C pins (18/19 = A4/A5 on Teensy 4.x) free for inputs; existing saved configurations are unaffected (#351)
- The LCD can now live on any I2C bus (`BUS I2C <n>`) instead of being pinned to bus 0, freeing SDA/SCL on the default bus for use as inputs; this replaces the unmaintained `marcoschwartz/LiquidCrystal_I2C` with `mathertel/LiquidCrystal_PCF8574` (#351)
- BLE: live-value catch-up (cold start, reconnect resync, and the 5 s heartbeat) now covers only configured inputs instead of every slot in the table, so the burst scales with how many inputs you have rather than with the board's maximum; clearing an input still pushes frames for a few seconds afterwards so a connected client drops its tile promptly, and live frames now distinguish a removed input from a merely disabled one (#337)

## [0.11.0-beta] - 2026-08-25

### Added
- Control engine: `RULE <id> ENABLE|DISABLE` parks or re-arms a Rule without deleting it — the enabled flag was previously reachable only through `JSON IMPORT`, so from a terminal the only choices were "delete it" or "leave it armed"; a parked Rule keeps its Action memberships, shows as `PARKED` in `RULE LIST`, and can neither fire nor force an Action's failsafe (#346)
- Control engine: an Action can now be driven by several Rules and runs while any of them is active (`ACTION <id> RULE ADD|REMOVE <ruleId>`), so one cooling fan can be demanded by coolant temp, trans temp and an A/C request at once — a live Rule's demand outranks a faulted sibling sensor, and existing configs load unchanged (#343)
- Registry: every application preset now declares a car-area group (Engine, Drivetrain, Induction, Exhaust, Electrical, Environment, Other) exported as `"group"` in `LIST APPLICATIONS JSON`, and `SYSTEM DIAG PINS` gains a `JSON` variant
- BLE: live per-input sensor values are now pushed over a new Extended Service characteristic (16-byte `PreobdLiveValue`, frame 0x06, display units, 500 ms change scan + 5 s heartbeat), and `SYSTEM DUMP JSON` gains per-input `pinLabel` and `unitsFactor`/`unitsOffset` and now includes disabled inputs
- Control engine: the per-input alarm facade (`SET <pin> ALARM`, webapp Inputs panel) now supports one-sided alarms — a Min-only floor (`SET <pin> ALARM MIN <v>`) or Max-only ceiling (`SET <pin> ALARM MAX <v>`), not just a Min+Max band — and one-sided alarms round-trip through `SYSTEM DUMP JSON` (#312)
- `JSON IMPORT` gains a byte-counted form (`JSON IMPORT <byte-count>`) for programmatic senders that already know their payload length, alongside the existing paste-and-`END JSON` sentinel form for interactive use; `SYSTEM DUMP JSON`'s `firmware` object gains `capabilities[]`
- nRF BLE firmware now advertises a unique per-device name (`preOBD-XXXX`, suffixed with the last two octets of the device's BLE MAC) instead of a fixed `preOBD`, so multiple units are distinguishable in a scan
- Control engine: a rule-driven Action can now be forced by hand without unlinking its Rule — `ACTION <id> ON|OFF` sets an operator override that outranks both the rule and the sensor-loss failsafe (flagged and reported when held against a faulted source), `ACTION <id> AUTO` releases it, and the same commands work over BLE; the override is RAM-only and reverts to AUTO on reboot (#329)
- `OUTPUT SD_Log FORMAT LONG|WIDE` selects the SD log layout — `WIDE` (default) is one row per sample with a column per input, `LONG` is the original one-row-per-reading `Time,Sensor,Value,Units` layout, kept so existing log-parsing scripts keep working; the setting persists and round-trips through `SYSTEM DUMP JSON` as `outputs.sd.format`

### Changed
- **Breaking: EEPROM configuration is reset on upgrade.** The stored config layout changed (`EEPROM_VERSION` 5 → 6) to carry the control-engine Rule/Action arena and the per-input flag fixes below; the firmware detects the old version and starts from defaults. Back up your setup with `SYSTEM DUMP JSON` before flashing and restore it afterwards with `JSON IMPORT`.
- SD logging now writes one CSV row per sample with a column per input (`millis,CLT_C,OILP_PSI,…`) to sequentially numbered `LOG####.CSV` files, honours `OUTPUT SD_Log INTERVAL`, records faulted sensors as `ERROR`, and reports whether rows are actually landing via `SYSTEM DIAG HW` — replacing a row-per-reading format that could silently log nothing after a filename collision, a full card, or a removed card

### Fixed
- Control engine: three CLI gaps closed — `ACTION <id> NAME` with no text now clears an Action's label (there was no reachable spelling of "unnamed", so a client's blank-name save just errored), `INFO <pin> ALARM` now reports the input's live alarm Rule instead of the retired legacy alarm fields (it read `Enabled: NO / Min 0 / Max 0` even with an alarm firing), and `CLEAR <pin>` now removes the Rules sourced from that pin instead of orphaning them — an orphaned Rule could hold a `FAILSAFE ON` relay energized indefinitely, and a Rule whose input is missing is now inert rather than faulted (#336, #338, #345)
- Control engine: an Action no longer snaps its relay ON with no operator command — driving an unlinked Action with `ACTION <id> ON`, linking it to a Rule, then later deleting or unlinking that Rule left the manual state armed and dormant until the link went away; an Action whose driver changes now always starts de-asserted, and `RULE DEL` / `SET <pin> ALARM OFF` report any relay they de-energize instead of dropping it silently (#329)
- SD logging: pulling the card mid-drive no longer resets the board on Teensy — removal is now detected before the write, every blocking card operation widens the watchdog the way `initSD()` always has, and the logger latches a card-removed fault (reported by `SYSTEM DIAG HW`, which also probes the slot instead of echoing the boot-time mount) instead of stalling the engine loop; boards whose SD library exposes no presence probe are unchanged (#331)
- Boot: the Teensy no longer risks a watchdog reset loop during startup — `initSD()` arms the 2 s watchdog and Teensy 4.x cannot disarm it, but nothing fed it again until the end of `setup()`, leaving the sensor-stabilise `delay(1000)` and everything around it running on roughly 100 ms of margin
- `SYSTEM DUMP JSON`/`JSON IMPORT` round-trips per-input `enabled`/`alarmEnabled`/`displayEnabled` correctly — the flags were exported as raw integers (0/1) instead of JSON booleans, so `JSON IMPORT` silently discarded the actual value and reset every input to its hardcoded default (e.g. a disabled input always came back enabled) on a profile load and a manual paste
- `RULE LIST` now shows alarm limits and hysteresis converted to the source input's display units with a unit suffix instead of a bare base-unit number, and a faulted/disconnected sensor shows `ERROR` instead of a raw NaN token in `RULE LIST`, `LIST INPUTS`, and `INFO <pin>` (#324, #325)
- Disabled inputs now survive an EEPROM save/reboot, `LIST INPUTS`/`INFO <pin>` show the value converted to its display units instead of raw base units, and the webapp's error-detection regex now catches indented `ERROR:` banners (#317, #318, #319)
- BLE alarm notifications now carry a rule id (`PreobdAlarmEvent` 18→19 bytes, protocol version 2), so the webapp keys firing state by rule and a CLEAR on one rule no longer wipes a still-firing rule on the same input (#310)
- `JSON IMPORT` now emits its completion prompt — previously nothing ever printed `preOBD> ` after an import finished, silently hanging every client waiting on it (the reference webapp's file-import feature included); a sentinel-mode buffer overflow no longer lets the rest of an in-flight payload spill into the next CLI command, and an abandoned import now times out instead of holding its buffer forever

## [0.10.0-beta] - 2026-07-12

### Added
- Control engine: a new `INFO` Rule severity below `WARNING` — a Rule set `INFO` notifies (webapp log breadcrumb via the alarm channel) and still drives its Actions, but never lights the LED/buzzer or raises the system alarm state; the webapp "Alarms" panel is now "Notifications" (EEPROM clean reset) (#308)
- Webapp: first-class Rules and Actions management — a new tabbed panel (below Alarms) to create, edit, and delete threshold Rules and relay/GPIO Actions (name, pin, polarity, failsafe, and rule-linking) with live Action on/off and manual override, replacing the read-only Actions card list (#279)
- Control engine: Actions now carry an optional operator label (`ACTION <id> NAME <text>`, shown in `ACTION LIST` and round-tripped through `SYSTEM DUMP JSON`/`JSON IMPORT`), so a name like "Cooling Fan" set in the webapp travels to the CLI and other clients instead of showing as a bare id (EEPROM clean reset)
- `SYSTEM DIAG PINS`: a live analog-pin sweep that reads every analog-capable pin — configured or not — and flags floating vs driven pins, so you can find which input a sensor is wired to during setup (#280)
- `docs/feature_costs.md` + `tools/measure_feature_costs.py`: a per-flag flash/RAM cost matrix for the memory-constrained mega2560 build, so a configuration can be sized before compiling (#145)
- Control engine: a unified Rules → Actions + built-in Annunciator pipeline replacing the separate per-input alarm and relay subsystems — define threshold Rules (`RULE`: ABOVE/BELOW/OUTSIDE-band with hysteresis, debounce, warmup, and a derived two-tier warning/alarm severity), drive relay/GPIO outputs with sensor-loss failsafe and manual override (`ACTION`), and let applications seed a default Rule on `SET <pin> APPLICATION`; alarms and relays are now Rules/Actions, so `SET ALARM` and `RELAY` are deprecated and the BLE relay panel is replaced by an Action broadcaster (`PreobdActionState`). Clean EEPROM break (SystemConfig v12).
- Control engine Rule + Action tables are now included in `SYSTEM DUMP JSON` and the SD/`CONFIG` save-load round-trip (top-level `rules`/`actions` arrays), so they survive a config backup-restore (#281 follow-up)
- Control engine: Action target pins now participate in pin-conflict detection — `ACTION ADD` rejects a target already claimed by a bus, system pin, RGB output, configured input, or another action, and the pin shows up in `SYSTEM PINS` (#281 follow-up)

### Fixed
- Webapp: the input editor now pre-fills the OBD-II PID/length from the selected application's defaults (as it already does for the alarm band), so an application with a default PID (e.g. Boost Pressure → `0x6F`) shows its assignment in the form and inputs table immediately instead of only after a full config re-read
- Free-text CLI arguments typed after a **lowercase** command (e.g. `action 0 name "Cooling Fan"`) are no longer force-uppercased and split at spaces — the case-insensitive command fallback now tokenizes args exactly like the primary path (quote-aware, case-preserving), so a quoted multi-word value keeps its case and spaces (`"Cooling Fan"`, not `COOLING`); command keywords stay case-insensitive as before, and the fix also frees ~160 B of AVR RAM by dropping a redundant tokenizer buffer
- `LOG TAG <tag>` now matches the tag name case-insensitively (like `LOG LEVEL` and every other CLI lookup) — previously a tag only resolved when upper-cased, which happened by accident only for lowercase-typed commands; `LOG TAG sd DISABLE` now works regardless of command case
- Analog `A<n>` pins now map correctly on Teensy, where the core's A-names are non-contiguous — `SET A14` on a Teensy 4.1 silently configured pin 28 instead of 38, and pins were relabelled wrongly on output (pin 38 shown as "A24", digital pins ≥14 shown as analog). Both directions now resolve through the core `A*` constants via a single shared map, so `SET`, `SYSTEM PINS`, `LIST INPUTS`, and diagnostics agree (#302)
- Auto-seeding an alarm Rule on `SET <pin> APPLICATION` no longer fails silently when the table fills: the control-engine Rule/Action tables moved out of `SystemConfig` into a shared front-growing EEPROM config-store arena (inputs + rules + actions share one budget, truncating loudly on overflow), with RAM caps raised to a uniform 64 (8× the old limit; AVR stays at 2) so the engine supports many rules per input and many actions per rule (#296)
- Per-input alarm Min/Max now drives the control-engine alarm Rule end to end — `SET <pin> ALARM <min> <max>` / `DISABLE` upserts (or removes) the input's annunciator Rule in place (without re-arming an already-warmed rule), the config dump reads the band back from that Rule, the input editor gains an explicit "Alarm enabled" toggle pre-filled from the application defaults, and the webapp's alarm banners now distinguish a WARNING (approaching, yellow) from a fired ALARM (exceeded, red) instead of labelling every event an exceeded alarm — replacing the deprecated `SET ALARM` no-op the webapp was silently writing to (#295)

### Changed
- Webapp: Rules/Actions panel polish — the Add button moves to the footer beside Save to EEPROM (matching the Inputs panel), Action-less rules now read "Built-in" in the Fires column, and the input alarm Min/Max is labelled as the two-sided band it is, pointing users to the Rules panel for one-sided ABOVE/BELOW alarms (firmware follow-up #312)
- Webapp: the Relays panel is now an **Actions** panel that reads the control-engine `PreobdActionState` wire format (type / target pin / source Rule / failsafe / driven state) with a manual ON/OFF toggle for manual Actions; Rule-driven Actions show as read-only status, and the System Status tile now reports the Action count (#281 follow-up)
- Build configuration restructured into per-board folders (`boards/<board>/` with `board.h`, `board.ini`, and a gitignored `wiring.ini` overlay) plus a shared `resolve.h`: hardware pins now live in your own wiring overlay rather than a tracked profile, wired feature flags are derived from capability + pin presence, and the buzzer pin macro `ALARMS_PIN` is renamed `BUZZER_PIN` (#283)

## [0.9.0-beta] - 2026-06-18

### Added
- Webapp: OBD-II PID assigned to each input is now always shown in the inputs table, and the input editor lets you set/clear `OBD_PID` and `OBD_LENGTH` directly
- `SET <pin> OBD_PID <0xNN>` / `SET <pin> OBD_LENGTH <1-5>` to assign or clear the OBD-II Mode 01 PID an input reports as, with the assignment now shown in `LIST INPUTS` / `SYSTEM DUMP`
- nRF52840 firmware: two-central BLE concurrency — a webapp (Control Service) and a phone OBD app (HM-10 `FFE1`) can connect and run at the same time (e.g. live config while Torque streams), with per-connection notify fan-out, connection ref-counted advertising/IRQ/framing lifecycle, and dual-link radio tuning; bench-verified at ~16 PID/s concurrent
- Webapp build-time version stamp: `tools/inject_webapp_version.py` rewrites a webapp-scoped git id (`b<count> @<sha>`) into the footer and the service-worker cache name during the GitHub Pages deploy, replacing the `document.lastModified` stopgap; local working copies show a `DEV` marker with a live last-modified timestamp (#226)
- Host-side ELM327 routing over the framed transport (type `0x30`): the main MCU answers ELM327 AT/OBD queries arriving as `0x30` frames using the existing emulator, so a future concurrent BLE client (Torque/OBD Fusion) can stream alongside the webapp's text channel
- nRF52840 firmware: ELM327-over-BLE for phone OBD apps via an HM-10 (FFE0/FFE1) GATT channel that wraps their traffic into framed type `0x30` and unwraps responses, plus a Nordic UART Service (NUS) terminal channel — OBD apps and the webapp/terminal now ride separate BLE characteristics (single connected central this cycle; concurrent two-central support is a follow-up)
- nRF52840 firmware (Phase 1): BLE↔UART transparent bridge on Adafruit ItsyBitsy nRF52840, enabling phone-based control of the host MCU's serial CLI over BLE; includes Nordic Secure DFU for nRF self-update
- nRF52840 firmware (Phase 2): Tier 2 framed UART protocol with auto-detect on BLE connect, falling back to Tier 1 transparent passthrough if the host doesn't speak the framing — keeps current Teensy behavior unchanged while enabling future multi-channel transports
- nRF52840 firmware (Phase 3): software-paced flow control on the host-MCU UART link via two GPIOs (`PIN_NRF_RTS_OUT` / `PIN_NRF_CTS_IN` in platformio.ini build flags); the nRF asserts its RTS when its Serial1 RX ring fills past a high-water mark so the host stops transmitting, unblocking reliable multi-KB bulk responses like `SYSTEM DUMP JSON` at 115200 baud. Both nRF and host must opt in for the throttle to engage; users who haven't wired CTS/RTS can instead drop the link baud below the BLE drain ceiling via `BUS SERIAL <port> BAUDRATE 9600` (also 19200/38400 work) to avoid overflow.
- Host-side MuxTransport with per-port framed-mode opt-in via `BUS SERIAL <port> FRAMED ENABLE|DISABLE`. When enabled, the chosen serial port runs the binary framing protocol used by the nRF coprocessor (text payload wrapped in `[0xAA][TYPE][LEN][PAYLOAD][CRC8]` frames per ARCHITECTURE.md §3); when disabled (default) it acts as a transparent passthrough. Backwards-compatible with HM-10, stock Bluefruit, and pre-framed nRF firmware. MVP scope is text channel only (types 0x01/0x02 + 0xFF ping); Status/Alarm/Relay/OTA broadcasters come in follow-up PRs.
- Live System Status push from host MCU to BLE clients via framed transport (type 0x03); the host packs a 32-byte `PreobdSystemStatus` snapshot (mode, active input count, relay count, uptime, firmware version) every loop tick and forwards it whenever state changes or a 5s heartbeat fires, replacing the webapp's polling read with real-time notifications
- Live alarm push from host MCU to BLE clients via framed transport (type 0x04) on the Alarm Notify characteristic (`...0030`); per-input severity/kind transitions emit 18-byte `PreobdAlarmEvent` notifications including return-to-normal, replacing webapp polling with real-time alarm notifications
- Binary relay control over BLE via framed transport (type 0x05) on the Relay Control characteristic (`...0031`): the host MCU pushes 16-byte `PreobdRelayState` notifications on every mode/state/threshold change, and clients can write the same struct to remotely set mode (DISABLED/AUTO_HIGH/AUTO_LOW/MANUAL_ON/MANUAL_OFF) and thresholds; existing CLI `RELAY` commands continue to work in parallel
- Webapp framed-mode UI: discovers the Extended Service when present, subscribes to live System Status updates, surfaces alarm events in a real-time panel with a top firing banner (severity color coding, friendly "exceeded N°F" messages, "Firing for Nm Ns" duration), and renders a binary Relay Control panel with per-row mode dropdown + threshold edits + Apply that writes the 16-byte `PreobdRelayState` directly to the GATT characteristic; vocabulary and visual tones (ok / warn / bad) aligned with the in-progress mobile companion app; degrades cleanly to the existing transparent-mode UI on HM-10 / stock Bluefruit / framed-disabled setups
- Native `pio test -e native` unit-test harness (host toolchain + Arduino stub layer) covering the framed-mode wire codec, the per-input alarm state machine, DJB2 registry hashing, and the Alarm/Relay broadcaster payloads (emit + inbound relay writes) — automating the protocol-correctness half of #221 without hardware
- `SYSTEM DIAG` command — on-device diagnostics in three sections: `INTEGRITY` (dispatch-table self-check; replaces the standalone `SELFTEST` command), `HW` (peripheral presence probes — LCD/BME280 I²C ACK, SD mount), and `INPUTS` (per-input live reading sanity); bare `SYSTEM DIAG` runs all three, each with its own pass/fail verdict
- `TEST INJECT <pin> <value>` / `TEST CLEAR` — hold an input at a fixed value for deterministic bench testing (e.g. driving an input past its alarm threshold), then restore live readings; independent of the time-based TEST scenarios
- `BUS CAN NONE` — disable CAN entirely in one command (mirrors `BUS I2C NONE` / `BUS SPI NONE`), freeing the CAN pins on next reboot
- `TEST OUTPUT` — pulse each output (RGB LED, buzzer, configured relays) in turn for wiring verification (scan-tool "output test" pattern); CONFIG-mode only, feeds the watchdog through its blocking sequence
- Native dispatch-routing tests (`test/test_dispatch`): command/subcommand routing logic extracted into `command_dispatch.cpp` and exercised on the host against a synthetic table — covers case-folding, the configModeOnly/runModeAllowed mode gates, unknown-token handling, and argc/argv/tokenIndex forwarding (#192)
- `SYSTEM DIAG INPUTS` now classifies each input's electrical state from a raw, un-clamped read (OPEN / SHORTED / OUT OF RANGE / NEAR LIMIT / IN RANGE), with `SYSTEM DIAG INPUTS VERBOSE` showing the raw ADC → resistance → value evidence chain — distinguishing a disconnected sensor from a believable value the display path would otherwise clamp (#238)

### Changed
- CONFIG mode no longer freezes the engine: sensors, alarm evaluation, and live outputs (OBD/ELM, CAN broadcast, RealDash, serial-bound ELM327) keep running in CONFIG so no connected client sees stale data while another is configuring — only physical actuation (relay switching, alarm buzzer) and the recording outputs (serial CSV, SD logging) stay suppressed in CONFIG; CONFIG is now purely a write-gate (#268)
- Webapp no longer puts the device into CONFIG mode on connect: it loads configuration in RUN (SYSTEM DUMP / LIST are RUN-legal) and enters CONFIG only for the brief window each write batch needs (Apply / Save / Revert / Import / Remove), restoring the prior mode afterward so an explicit manual CONFIG choice is left untouched — live data keeps streaming to the webapp and any concurrent OBD app instead of freezing while a dashboard sits connected (#268)
- Webapp System Status now uses a single combined Firmware tile (clean host version + full nRF build string) with a one-glance alarm-count health headline, and the terminal absorbed the Quick Actions buttons while dropping the misleading fixed "9600 BAUD" label
- nRF CMD_RX notifications now chunk to the negotiated ATT MTU (up to 244 bytes) instead of a fixed 20, cutting notify calls ~12× on bulk text responses like `SYSTEM DUMP JSON`
- `RELAY MODE MANUAL_ON|MANUAL_OFF` now actuates the pin immediately (instead of only on the next RUN-mode loop) and is usable in RUN mode as a live operator override, alongside `RELAY STATUS`/`LIST`; the configuration subcommands (`PIN`/`INPUT`/`THRESHOLD`, `MODE AUTO_*`/`DISABLED`) remain CONFIG-only (#250)
- `TEST_MODE_PIN` is now optional: a profile can enable `ENABLE_TEST_MODE` for CLI-driven injection (`TEST INJECT`/`CLEAR`/etc.) without dedicating a GPIO to the boot-time hardware trigger (#249)
- `BUS SERIAL` list view now tags framed-mode ports with `[FRAMED]` (mirroring the existing `[ELM327]` tag), so the multi-port summary matches the per-port detail view
- Renamed `MEASURE_LEVEL` measurement type to `MEASURE_PERCENT` to reflect its actual usage covering normalized 0–100% values (TPS, fuel level, engine load, accelerator pedal) rather than fluid levels (#209)

### Fixed
- Relay panel no longer shows "No relays configured" when the webapp connects some time after boot: the relay broadcaster now re-sends every relay slot on a 5s heartbeat (and immediately on a client's connect-time ping), so a late client gets the full relay state instead of just the one stale snapshot cached on the nRF (#271)
- Hall-effect speed sensor could momentarily read 0 km/h: its pulse-timeout check compared a cached `millis()` against the ISR-written pulse timestamp in a way that could underflow (and mixed the millis/micros timebases); now snapshots the ISR timestamp first and compares on a single micros() timebase, matching the W-phase RPM sensor (#275)
- Zombie BLE central eviction: a webapp on a sleeping host keeps its link alive at the OS level but stops consuming, squatting one of two connection slots so another device can't connect; the webapp now sends a periodic liveness keepalive and the nRF disconnects a control central that goes silent past a timeout (~2 min), freeing the slot and re-arming advertising (#266)
- Two-central bulk-traffic fairness: a webapp bulk burst (e.g. `SYSTEM DUMP JSON`) no longer starves a concurrently streaming OBD app's ELM327 round-trips until it disconnects — the nRF drains text and ELM327 notifies from separate stashes and the host services ELM327 requests during long framed writes (#263)
- Alarms now arm when enabled at runtime (`SET <pin> ALARM ENABLE`) and when restored from EEPROM at boot, instead of staying stuck at state `DISABLED` until the input's application was re-applied; re-issuing the enable on an already-armed alarm is idempotent (no longer resets its warmup/active state)
- `SET CAL BIAS` on a preset Steinhart/Beta/Polynomial sensor no longer zeroes the curve coefficients (which made the reading return INF); the lookup-table read now also honors a custom bias override (#247)
- Feed the watchdog in the `AT` command's serial response-drain loop (a chatty peer could extend it past the 2s timeout and reset the MCU) (#243)
- `SYSTEM DUMP JSON` now emits per-port `framed` and `elm327` fields, completing round-trip config visibility for framed-mode and ELM327 port binding (#230)
- Gate the nRF framed-transport subsystem (MuxTransport + Status/Alarm/Relay broadcasters) behind `ENABLE_FRAMED_TRANSPORT`; the Mega2560 omits it and runs a plain transparent `Serial1`, recovering ~1.6 KB SRAM to fit the 8 KB budget

## [0.8.1-beta] - 2026-05-11

### Added
- `SET <pin> DISPLAY ENABLE/DISABLE` serial command to toggle per-input LCD display flag (#142)

### Changed
- Aligned all application abbreviations and display labels with OBD2/SAE J1979 scanner conventions; establishes consistent xFT suffix for drivetrain fluid temps (TFT/DFT/GFT/TCFT) and E-prefix for engine fluids (ECT/EOT/EOP); synced standard PID import table to match (#208)
- **BREAKING CLI**: grammar redesign per #189 phase 3 — calibration profiles regrouped under `SET <pin> CAL <profile>` (STEINHART/BETA/PRESSURE_LINEAR/PRESSURE_POLY/RPM/SPEED/BIAS/PRESET); top-level `ENABLE`/`DISABLE` replaced by `SET <pin> INPUT ENABLE/DISABLE`; `SET <pin> NAME` now sets the 32-char display label and the 8-char abbreviation moves to new `SET <pin> ABBR`; `SYSTEM PINS <pin>` retired in favor of `INFO <pin>`; `SET <pin> CALIBRATION PRESET` retired in favor of `CLEAR <pin> CALIBRATION` (or `SET <pin> CAL PRESET`); `H` added as a HELP alias and a configuration hint is appended after "Input not configured" errors. Old verbs are removed without deprecation aliases. JSON config schema is unchanged. Webapp updated in lockstep
- `CalibrationOverride` union members `rpm`, `speed`, and `can` now use `RPMCalibration`, `SpeedCalibration`, and `CANSensorCalibration` typedefs directly, eliminating duplicate field definitions and padding drift risk; EEPROM version bumped to 5 to invalidate stale AVR layouts (#175, #72)
- Restructured `command_table.cpp` (3,453 → 1,419 lines) around a generic subcommand dispatch primitive; `cmd_set`, `cmd_bus`, `cmd_system` split into focused per-command files with PROGMEM-resident dispatch tables; adds `SELFTEST` command for runtime dispatch-table validation (#118, #186, #191)
- CLI plumbing pass: shared `requireInput`/`printInputFieldSet`/`printSaveReminder` helpers eliminate ~45 lines of leaf boilerplate, unify five different SAVE-reminder wordings, and ensure every config-mutating leaf prints the reminder; `HELP SET`/`BUS`/`SYSTEM` now generated from the dispatch tables, closing the BUS/SERIAL ELM327 help drift (#189)

### Fixed
- `SET <pin> NAME` and `SET <pin> ABBR` now route through `requireInput`, producing the standard "Input not configured" error on unknown pins instead of silently failing (#205)
- Pre-existing bugs in `cmd_set` surfaced during the command_table refactor: RPM/SPEED arity check no longer accepts arbitrary excess parameters; `SET CAN <pid>` rejects non-numeric arguments instead of allocating phantom CAN sensors; BIAS bounds inlined to avoid file-scope macro leak (#187, #188, #190)
- RUN-mode safety: destructive `SYSTEM` and `LOG` subcommands (RESET, REBOOT, SEA_LEVEL, UNITS, INTERVAL, LEVEL, TAG) are now blocked in RUN mode while read-only verbs (STATUS, PINS, DUMP, TAGS) remain available; mode gating now lives entirely in `COMMANDS[].configModeOnly` and per-`Subcommand` `runModeAllowed` flags rather than a hardcoded allowlist (#194)
- `parsePin("CAN")` and `parsePin("I2C")` no longer leak virtual pin slots when a command fails after parsing; the per-keyword static counters were replaced with a scan of `inputs[]` for the highest allocated slot, so failed commands no longer falsely exhaust the virtual pin range (#196)
- AVR RAM optimization: leaf-handler string literals in `cmd_set`, `cmd_bus`, `cmd_system`, `cmd_log`, and other command handlers wrapped in `PSTR()`/`streq_P()` so they live in flash instead of being copied to `.data` on boot — saves ~296 bytes RAM on mega2560 (98.4% → 94.8%) (#199)
- Missing trailing newline added to `src/lib/sensor_types.h` (#178)
- `allocateInputSlot` now zeroes the slot before use, eliminating stale flags, calibration data, and alarm thresholds from previously-cleared inputs (#180)
- Input slots are now freed on partial-failure during configuration (e.g. `setInputSensor` failure after slot allocation), preventing orphaned slots (#179)
- SPI and I2C buses now default to NONE and only initialize when explicitly selected via `BUS SPI/I2C <N>`; fixes spurious pin reservation conflicts at boot (e.g. RGB LED blocked by unconfigured SPI0) (#168)
- CAN HAL `write()` clamps `len` to 8 before `memcpy` to prevent buffer overflow in frame construction (#112)
- RPM ISR reads on AVR are now atomic, and timeout check uses `micros()` consistently with the ISR (#111, #116)
- CAN-imported sensors now support a configurable per-sensor stale-data timeout via `SET <pin> CAN_TIMEOUT <ms>` (100–30000ms), replacing a hardcoded 2000ms for all sensors (#173)
- `readCANSensor` now copies preset `CANSensorCalibration` out of PROGMEM via `memcpy_P` before access, fixing CAN sensor import on AVR (Mega2560) where direct dereference returned RAM garbage (#177)

## [0.8.0-beta] - 2026-04-30

### Added
- Web Bluetooth webapp for browser-based device configuration and diagnostics, installable as a PWA via GitHub Pages
- BLE GATT profile specification (Tier 1 text command service + Tier 2 binary protocol spec) with shared UUID definitions for firmware and clients
- HM-10 BLE module support in webapp with auto-detection alongside preOBD GATT and Nordic UART services
- `AT <port> <command>` serial command for sending raw AT commands to attached BLE/serial modules
- 8 new classic-car monitoring application presets
- Disconnect detection for linear sensors: out-of-range voltages return NAN, optional pin pull-up for low-Z signal-conditioned sensors, and per-input `divider_ratio` (set via `SET <pin> DIVIDER <ratio>`) for running 5V sensors on 3.3V ADCs through a voltage divider (#155, #157)

### Changed
- Board profiles (`src/profiles/`) now own all feature flags and hardware pin assignments; `config.h` is application constants only; system pin fields removed from SystemConfig/EEPROM; JSON config decoupled from SD logging via new `SUPPORTS_SD` hardware capability flag (#163, #165, #166)
- Watchdog kick batched across `WatchdogKickingPrint` writes and kick interval raised; watchdog now fed during JSON serialization to survive slow BLE UART bridges
- Documentation accuracy pass: fixed broken intra-doc links, refreshed README and docs index to cover BLE, ESP32-S3, and recently-added guides

### Fixed
- `esp32s3_hybrid` and `teensy41_hybrid` build envs now compile cleanly (#146)
- Correctness and safety fixes in sensor interpolation and resistance calculation (#110, #113, #114)
- SystemConfig persistence on platforms with small EEPROM (Teensy 4.0): config address is now derived per-platform and decoupled from MAX_INPUTS

## [0.7.6-beta] - 2026-04-24

### Changed
- Replaced scattered per-platform `#ifdef` sizing constants with per-env profile headers (`src/profiles/`)

### Removed
- Dropped `USE_STATIC_CONFIG` build flag, `uno_static` env, and `tools/configure.py` (#148)

## [0.7.5-beta] - 2026-04-22

### Added
- Added Smiths, Stewart-Warner, AC Delco, and Bosch NTC sensors to the sensor library

## [0.7.4-beta] - 2026-04-16

### Added
- JSON IMPORT command for streaming bulk sensor configuration over serial CLI
- JSON export of firmware static catalogs via `LIST ... JSON` and `SYSTEM DUMP REGISTRY JSON`

### Fixed
- Control-plane responses now return to the transport that sent the command; unsolicited messages (alarms, etc.) still multi-cast to all configured transports

## [0.7.3-beta] - 2026-04-13

### Added
- ELM327 AT command emulator output module for direct BLE OBD-II app connectivity without hardware adapter
- Added Jeep 4.0L sensor family (gauge temp sender, Renix ECU CTS, oil pressure sender)
- Extended OBD-II PID discovery chain (0x00→0x20→0x40…) so apps find all configured PIDs automatically
- TRANSPORT command SECONDARY keyword for simultaneous multi-port control (e.g. USB + Bluetooth)

## [0.7.0-beta] - 2026-02-07

### Added
- Per-input output routing to selectively route inputs to CAN, RealDash, Serial, or SD
- Pin allocation status reporting via SYSTEM PINS command
- CAN sensor import from external ECUs (OBD-II, J1939, custom protocols)
- CAN controller hardware abstraction layer (FlexCAN, TWAI, MCP2515)
- Hybrid CAN controller mode for mixing native and SPI controllers
- Per-bus CAN baud rate configuration
- CAN input listen-only mode for passive bus monitoring
- Platform-specific watchdog HAL drivers
- RGB LED status indicator with priority system and configurable effects

### Changed
- Watchdog and CAN bus refactored to use HAL with platform-specific drivers
- Teensy 3.6 migrated from external MCP2515 to native FlexCAN

### Fixed
- Shared CAN bus validation when input/output use the same physical bus

## [0.6.5-beta] - 2025-01-27

### Added
- Enhanced logging system with levels and subsystem tags
- Vehicle speed sensing with hall effect sensor support
- OBD-II request/response for ELM327/Torque compatibility
- Hierarchical help system organized by category
- Relay output control with threshold-based automation and hysteresis
- Test mode serial command
- ESP32-S3 BLE transport support
- Bus configuration system with pin registry
- Modular category-based sensor library

### Changed
- CLI library migrated from microrl to embedded-cli
- CAN output supports both broadcast and request/response modes
- Serial output migrated to message bus API

### Fixed
- Static build compatibility for pinTypeRequirement field
- SD card initialization centralized to prevent reboot on save
- HELP display corruption on AVR platform
- Various pin registry and serial port configuration fixes

## [0.6.0-beta] - 2025-12-31

### Added
- Transport abstraction layer with MessageRouter for multi-transport communication
- ESP32 Bluetooth Classic and UART Bluetooth module support
- ESP32 native CAN (TWAI) implementation

### Changed
- Serial commands and output routing migrated to MessageRouter API

## [0.5.0-alpha] - 2025-12-22

### Added
- Beta equation thermistor calibration
- Runtime custom calibration for pressure and thermistor sensors
- JSON configuration system with SD card backup/restore
- Registry-based architecture with hash-based sensor/application lookups
- String-based unit system replacing hardcoded enums
- Runtime display, output, and system configuration commands
- RPM pulley ratio calibration

### Changed
- Alarm system rewritten with AlarmContext state machine
- EEPROM storage uses hashes instead of indices for portability
- LCD display driven by registry pattern matching

### Removed
- Legacy enum-based sensor/application system
- Deprecated example configs and setup scripts

### Fixed
- Compilation warnings on Arduino Mega
- Button press false detection with proper INPUT_PULLUP
- PROGMEM string handling in INFO command
- Alarm threshold min/max validation

## [0.4.0-alpha] - 2025-12-03

### Added
- First public release
- 17+ sensor types with 30+ pre-calibrated configurations
- Multiple outputs: LCD, CAN bus (OBD-II), serial CSV, SD logging, RealDash
- Runtime serial configuration with EEPROM persistence
- Static configuration mode for memory-constrained boards
- Test mode with predefined scenarios

### Fixed
- Watchdog boot loop in CONFIG mode
- EEPROM checksum calculation order
- LCD garbage display on checksum mismatch

## [0.3.4-alpha] - 2025-11-26
## [0.3.2-alpha] - 2025-11-15
## [0.3.0-alpha] - 2025-11-05
## [0.2.0-alpha] - 2025-10-28

Previous development releases. See git history for details.

---

[Unreleased]: https://github.com/preobd/preOBD/compare/v0.11.0-beta...HEAD
[0.11.0-beta]: https://github.com/preobd/preOBD/compare/v0.10.0-beta...v0.11.0-beta
[0.10.0-beta]: https://github.com/preobd/preOBD/compare/v0.9.0-beta...v0.10.0-beta
[0.9.0-beta]: https://github.com/preobd/preOBD/compare/v0.8.1-beta...v0.9.0-beta
[0.8.1-beta]: https://github.com/preobd/preOBD/compare/v0.8.0-beta...v0.8.1-beta
[0.8.0-beta]: https://github.com/preobd/preOBD/compare/v0.7.6-beta...v0.8.0-beta
[0.7.6-beta]: https://github.com/preobd/preOBD/compare/v0.7.5-beta...v0.7.6-beta
[0.7.5-beta]: https://github.com/preobd/preOBD/compare/v0.7.4-beta...v0.7.5-beta
[0.7.4-beta]: https://github.com/preobd/preOBD/compare/v0.7.3-beta...v0.7.4-beta
[0.7.3-beta]: https://github.com/preobd/preOBD/compare/v0.7.0-beta...v0.7.3-beta
[0.7.0-beta]: https://github.com/preobd/preOBD/compare/v0.6.5-beta...v0.7.0-beta
[0.6.5-beta]: https://github.com/preobd/preOBD/compare/v0.6.0-beta...v0.6.5-beta
[0.6.0-beta]: https://github.com/preobd/preOBD/compare/v0.5.0-alpha...v0.6.0-beta
[0.5.0-alpha]: https://github.com/preobd/preOBD/compare/v0.4.0-alpha...v0.5.0-alpha
[0.4.0-alpha]: https://github.com/preobd/preOBD/releases/tag/v0.4.0-alpha
