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

## The model: three kinds of knob

Every feature is exactly one of these, and that decides where it lives:

| Kind | Meaning | Lives in |
|---|---|---|
| **CEILING** | the board genuinely *can't* (RAM/flash) or has fixed capacity | `board.h` — `PLATFORM_NO_*`, `MAX_*`, `SUPPORTS_JSON_CONFIG=0` |
| **WIRED** | capable; on *iff* the hardware is wired | a pin in `wiring.ini` → flag derived in `resolve.h` |
| **CHOICE** | capable; on/off is a preference | `board.h` default |

Resolution rule (`resolve.h`):

```
feature ON  ⟺  (intrinsic OR wired OR chosen)  AND  (board doesn't veto it)
```

A kind is **per (board, feature)** — `FRAMED_TRANSPORT` is a CHOICE on Teensy
but a CEILING on the Mega (8 KB RAM). Measured costs are in
[`docs/feature_costs.md`](../../feature_costs.md).

## Configuration layers

### `boards/<board>/board.h` — board definition

`-include`d before every translation unit. Holds only board *facts* and feature
*choices* — **never a wired pin number**:

- Capabilities: `PROFILE_HAS_NATIVE_CAN`, `HAS_BUILTIN_SD`, `EEPROM_TOTAL_BYTES`
- Ceilings: `PLATFORM_NO_SD_LOGGING`, `SUPPORTS_JSON_CONFIG=0`, `MAX_INPUTS`, CLI buffer sizes
- CHOICE feature defaults: `ENABLE_CAN`, `ENABLE_ELM327`, `ENABLE_TEST_MODE`, …
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
`#error`s on impossible combos (e.g. `ENABLE_LED` without RGB pins, or SD on a
board that vetoes it). `#ifndef`-guarded, so an explicit override still wins
(`-D ENABLE_LED=0` forces a wired feature off).

### `src/config.h` — application constants

Logic-level behavior, identical across boards: timing intervals, alarm behavior
(`SILENCE_DURATION`, `WARNING_THRESHOLD_PERCENT`), calibration defaults, default
display units. **No hardware pins.**

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
- **Turn a wired feature off** even though its pins are present: `-D ENABLE_LED=0` in `wiring.ini`.
- **A CHOICE feature** (e.g. RealDash): toggle its `#define ENABLE_X` in `board.h`.

## Adding a new board

Drop a folder — nothing shared is edited:

```bash
cp -r boards/teensy41 boards/my_board
```
1. Edit `boards/my_board/board.h` — capabilities, ceilings, sizing, CHOICE defaults.
2. Edit `boards/my_board/board.ini` — rename `[env:teensy41]`→`[env:my_board]`, set platform/board/lib_deps, and update the `${teensy41.wiring}` reference + the `[teensy41] wiring =` default to `my_board`.
3. Edit `boards/my_board/wiring.ini.example` with the reference wiring.

`extra_configs = boards/*/board.ini, boards/*/wiring.ini` picks it up automatically.

> **Core-TU caveat:** macros read by framework core TUs (e.g.
> `SERIAL_*_BUFFER_SIZE` on AVR) must be real `-D` flags in `board.ini` —
> `-include` doesn't reach those TUs, so they can't live in `board.h`.

### Hybrid CAN

`ENABLE_CAN_HYBRID` + the `CAN_BUS_*_TYPE` controller map are board facts in
`board.h`:

```cpp
#define ENABLE_CAN_HYBRID  1
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
- **`#error "ENABLE_LED set without all of RGB_PIN_R/G/B"`** → set all three RGB pins (or drop the `-D ENABLE_LED`).
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
