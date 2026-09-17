# Build Configuration Guide

How preOBD separates compile-time configuration from runtime configuration, and
how to customize a build for your hardware. The canonical reference for the
board layout is [`boards/README.md`](../../../boards/README.md); this guide adds
the user-facing how-to.

## Quick Reference

**One folder per board, plus clear layers:**
- **`boards/<board>/board.h`** = board definition: capabilities, ceilings, sizing, and which *features* are on
- **`boards/<board>/wiring.ini`** = *your* physical pin assignments (gitignored; copy `wiring.ini.example`)
- **`boards/resolve.h`** = derives wired feature flags from the pins you set
- **`boards/<board>/board.ini`** = the PlatformIO `[env:<board>]` fragment
- **`src/config.h`** = application constants: timing, thresholds, calibration defaults
- **EEPROM** = runtime user config: sensor inputs, output settings, unit preferences

## The model: three layers, three prefixes

The prefix on a macro tells you which question it answers:

| Prefix | Question | Lives in | How it's set |
|---|---|---|---|
| `SUPPORTS_X` | *Can this board do X?* | `board.h` | by **presence** — a board that can't omits the line. Never `0`. |
| `INCLUDE_X` | *Is X's code in the image?* | `resolve.h` only | derived: capability **and** wiring **and** no veto |
| — | *Is X on right now?* | EEPROM / runtime | `outputEnabled[]`, `displayType`, an I²C probe — never a macro |

```
INCLUDE_X  ⟺  SUPPORTS_X  AND  (wired, if X needs a pin)  AND  (no PLATFORM_NO_X)
```

`src/` tests only `INCLUDE_*`. It is worth being precise about what that means:
`#if INCLUDE_REALDASH` says the encoder is **linked in**, while
`outputEnabled[OUTPUT_REALDASH]` — which defaults to 0 — decides whether frames
actually go out. A build-time macro cannot answer a runtime question, and
treating one as if it could is the #351 bug.

**To make a build smaller, comment a `SUPPORTS_*` line out of `board.h`.** That
is the intended lever, and measured per-feature costs are in
[`docs/feature_costs.md`](../../feature_costs.md). A board that can't support
something at all simply has no line for it — there is no negative to declare.

## Configuration layers

### `boards/<board>/board.h` — board definition

`-include`d before every translation unit. Holds only board *facts* —
**never a wired pin number**:

- Capabilities: `SUPPORTS_CAN`, `SUPPORTS_ELM327`, `SUPPORTS_TEST_MODE`, … plus
  `PROFILE_HAS_NATIVE_CAN`, `HAS_BUILTIN_SD`, `EEPROM_TOTAL_BYTES`
- Ceilings: `PLATFORM_NO_SD_LOGGING`, `SUPPORTS_JSON_CONFIG=0`, `MAX_INPUTS`, CLI buffer sizes
- Intrinsic pins only: SPI-CAN `CAN_CS_*`/`CAN_INT_*`, hybrid `CAN_BUS_*_TYPE`

### `boards/<board>/wiring.ini` — your wiring (gitignored)

Discrete pins are facts about *your* unit, so they live in a gitignored overlay,
not a tracked file. Copy the template and uncomment what you wired:

```bash
cp boards/teensy41/wiring.ini.example boards/teensy41/wiring.ini
```
```ini
[teensy41]
wiring =
    -D RGB_PIN_R=6        ; wiring the RGB pins turns the status LED on
    -D RGB_PIN_G=5
    -D RGB_PIN_B=4
    -D BUZZER_PIN=3       ; alarm buzzer
    -D MODE_BUTTON_PIN=2  ; CONFIG-on-boot / silence button
    -D SD_PIN=10          ; external SD CS (boards without a built-in slot)
```

Omit a pin and that peripheral is simply off. A fresh clone with no `wiring.ini`
builds and boots — just without LED/buzzer/button/external-SD until you wire them.

### `boards/resolve.h` — the resolver

Derives the wired `ENABLE_*` flags from pin presence + board capability and
`#error`s on impossible combos (e.g. `INCLUDE_LED` without RGB pins, or SD on a
board that vetoes it). `#ifndef`-guarded, so an explicit override still wins
(`-D INCLUDE_LED=0` forces a wired feature off).

### `src/config.h` — application constants

Logic-level behavior, identical across boards: timing intervals, alarm behavior
(`SILENCE_DURATION`, `WARNING_THRESHOLD_PERCENT` — the `AUTO` default for a
rule's per-side warning threshold; override per rule with `WARN`, #425),
calibration defaults, default display units. **No hardware pins.**

### EEPROM — runtime

User-configurable state that survives reboots: sensor inputs, output enable/rate,
unit preferences. Set via serial commands, `SAVE`d to EEPROM.

## Shipped boards

| Board folder / env | Platform | CAN | SD |
|---|---|---|---|
| `boards/teensy41` (`teensy41`, `debug`) | Teensy 4.1 | FlexCAN | built-in |
| `boards/teensy40` (`teensy40`) | Teensy 4.0 | FlexCAN | external (wire `SD_PIN`) |
| `boards/teensy36` (`teensy36`) | Teensy 3.6 | FlexCAN | built-in |
| `boards/esp32s3` (`esp32s3dev`) | ESP32-S3 | TWAI | external |
| `boards/mega2560` (`mega2560`) | Arduino Mega | MCP2515 (SPI) | vetoed (RAM) |
| `boards/teensy41_hybrid` (`teensy41_hybrid`) | Teensy 4.1 | 3× FlexCAN + MCP2515 | built-in |
| `boards/esp32s3_hybrid` (`esp32s3_hybrid`) | ESP32-S3 | TWAI + MCP2515 | external |

```bash
pio run -e teensy41        # build
pio run -e teensy41 -t upload
```

## Customizing for your hardware

- **Your pins:** edit `boards/<board>/wiring.ini` only. Never a tracked file.
- **Turn a wired feature off** even though its pins are present: `-D INCLUDE_LED=0` in `wiring.ini`.
- **A capability feature** (e.g. RealDash): comment its `#define SUPPORTS_X` in/out of `board.h`.

## Adding a new board

Drop a folder — nothing shared is edited:

```bash
cp -r boards/teensy41 boards/my_board
```
1. Edit `boards/my_board/board.h` — `SUPPORTS_*` capabilities, ceilings, sizing.
2. Edit `boards/my_board/board.ini` — rename `[env:teensy41]`→`[env:my_board]`, set platform/board/lib_deps, and update the `${teensy41.wiring}` reference + the `[teensy41] wiring =` default to `my_board`.
3. Edit `boards/my_board/wiring.ini.example` with the reference wiring.

`extra_configs = boards/*/board.ini, boards/*/wiring.ini` picks it up automatically.

> **Core-TU caveat:** macros read by framework core TUs (e.g.
> `SERIAL_*_BUFFER_SIZE` on AVR) must be real `-D` flags in `board.ini` —
> `-include` doesn't reach those TUs, so they can't live in `board.h`.

### Hybrid CAN

`INCLUDE_CAN_HYBRID` + the `CAN_BUS_*_TYPE` controller map are board facts in
`board.h`:

```cpp
#define SUPPORTS_CAN_HYBRID  1
#define CAN_BUS_0_TYPE     CanControllerType::FLEXCAN
#define CAN_BUS_3_TYPE     CanControllerType::MCP2515
#define CAN_CS_0           9    // MCP2515 SPI pins (intrinsic — board.h)
#define CAN_INT_0          2
```

## Library dependencies

Modular groups in `platformio.ini` (`[display_libs]`, `[can_libs]`,
`[sensor_libs]`, `[eeprom_libs]`, `[cli_libs]`), referenced from each board's
`board.ini` `lib_deps`. Platform CAN libs: FlexCAN_T4 (Teensy), ESP32-TWAI-CAN
(ESP32), autowp-mcp2515 (SPI).

## Output availability: compile-time vs runtime

A feature flag controls whether output code is *compiled in*; once in, outputs
toggle at runtime via EEPROM:

```
SET OUTPUT CAN ENABLED       # runtime enable
SET OUTPUT CAN INTERVAL 100
SAVE
```

`LIST OUTPUTS` shows what's compiled in.

## Troubleshooting

- **Wrong pins / a peripheral is off** → set its pin in `boards/<board>/wiring.ini` (not a tracked file).
- **`#error "boards/resolve.h included without a board definition"`** → the env's `build_flags` must `-include boards/<board>/board.h` *before* `boards/resolve.h`.
- **`#error "INCLUDE_LED set without all of RGB_PIN_R/G/B"`** → set all three RGB pins (or drop the `-D INCLUDE_LED`).
- **A size diff after editing a profile shows no change** → PlatformIO doesn't track `-include`d headers as deps; `pio run -e <env> -t clean` first.
- **`undefined reference`** → library missing from that board's `board.ini` `lib_deps`.

## Summary

- **`board.h`** = board facts + feature choices (no wired pins)
- **`wiring.ini`** = your pins (gitignored)
- **`resolve.h`** = derives wired flags, fails loud on bad combos
- **`board.ini`** = the PlatformIO env
- **`config.h`** = app behavior constants · **EEPROM** = runtime settings

For most users: pick your board's env, copy its `wiring.ini.example` to
`wiring.ini`, set your pins, build and flash.
