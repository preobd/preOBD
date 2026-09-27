# Board configuration

Each supported board owns one folder here. A folder is the *whole* board:

```
boards/
  <board>/
    board.h              # board definition: SUPPORTS_* capabilities, CEILING
                         #   vetoes, sizing, intrinsic pins (tracked)
    board.ini            # the PlatformIO [env:<board>] fragment (tracked)
    wiring.ini.example   # template for your physical wiring (tracked)
    wiring.ini           # YOUR pins, as -D flags (gitignored)
  resolve.h              # shared resolver — derives the INCLUDE_* flags src/ tests
```

`platformio.ini` stays thin: shared `[env]` base + library bundles, and
`extra_configs = boards/*/board.ini, boards/*/wiring.ini` merges every board
folder in. To add a board, drop a new folder — nothing shared is edited.

## The model: three layers, three prefixes

Borrowed from Kconfig/devicetree (and Marlin's header-only take). The prefix
tells you which question a macro answers — keeping them distinct is the point:

| Prefix | Question | Lives in | Set by |
|---|---|---|---|
| `SUPPORTS_X` | *Can this board do X?* | `board.h` | you, by **presence** — a board that can't simply omits the line. Never `0`. |
| `INCLUDE_X` | *Is X's code in the image?* | `resolve.h` (only) | derived: capability **and** wiring **and** no veto |
| — | *Is X actually on?* | EEPROM / runtime | `outputEnabled[]`, `displayType`, `framed_enabled_mask`, an I²C probe |

`src/` tests `INCLUDE_*` for feature code, and no macro anywhere answers the
third question — the compiler cannot know it.

**One exception, worth knowing before you grep:** SD and JSON predate this split
and are still gated directly on `SUPPORTS_SD`, `SUPPORTS_JSON_CONFIG`,
`SUPPORTS_JSON_EXPORT` and `SUPPORTS_JSON_IMPORT_STREAM` — about 55 sites,
including `sd_manager.h:13` and the whole of `json_config.cpp`. Those names are
doing `INCLUDE_`-layer work. They aren't renamed because they are simultaneously
real ceilings a board sets to `0` (`mega2560` does, for JSON), so the two roles
have to be separated before the names can be.

```
INCLUDE_X  ⟺  SUPPORTS_X  AND  (wired, if X needs a pin)  AND  (no PLATFORM_NO_X)
```

That third row is not a technicality. `#if INCLUDE_LCD` means *the driver is
linked in*, *not* that a screen is fitted; `#if INCLUDE_REALDASH` means the encoder
is linked in, while `outputEnabled[OUTPUT_REALDASH]` — which **defaults to 0** —
is what decides whether frames go out. Reading a build-time flag as if it
answered the runtime question is exactly the bug behind #351: `ENABLE_LCD` was
taken to mean "a display is fitted", so every capable board reserved SDA/SCL and
denied those pins to inputs.

Vetoes stay negative-free: a capability a board lacks has **no line**, rather
than a `PLATFORM_NO_*` you'd have to know to invent. (`PLATFORM_NO_SD_LOGGING`
survives because SD capability is implied by `HAS_BUILTIN_SD`, a positive that
has to be overridable.)

## Reclaiming flash and RAM

**This is how you shrink a build.** Comment a `SUPPORTS_*` line out of
`board.h`, and that feature's code leaves the image:

```c
// #define SUPPORTS_REALDASH  1   ← reclaims 222 B flash / 36 B RAM on mega2560
```

`docs/development/FEATURE_COSTS.md` has the measured cost of every flag on mega2560, the one
board where the 8 KB budget makes the numbers decision-relevant; `mega2560/board.h`
repeats them inline per flag.

Two ways to drop a feature, with different blast radius:

| Want | Edit | Applies to | Tracked? |
|---|---|---|---|
| permanent decision for this board | comment out `SUPPORTS_X` in `board.h` | everyone building it | yes |
| just your own unit | `-D INCLUDE_X=0` in `wiring.ini` | your machine only | no (gitignored) |

The second works because every `resolve.h` derivation is `#ifndef`-guarded and
GCC processes all `-D` flags before any `-include` file, so your value is already
defined by the time the resolver runs and it declines to override you.

## Why pins live in a gitignored overlay

A pin number is a fact about *your* physical unit, not about the chip — there's
no correct default for `RGB_PIN_R` or `BUZZER_PIN`. So `board.h` never names a
wired pin; you put yours in `wiring.ini` (copy `wiring.ini.example`). A fresh
clone builds with wired peripherals **off** until you fill in the overlay — the
firmware boots fine, just without LED/buzzer/button/SD until wired. This is what
keeps personal wiring out of the repo (and stops bench edits colliding with it).

`resolve.h` derivations are `#ifndef`-guarded, so an explicit override still wins
(e.g. `-D INCLUDE_LED=0` to force a wired feature off).

**Bus instances are WIRED too, but the rule is "grants, never vetoes" rather
than "off until wired" (#377).** `-D I2C_WIRED_BUS=<n>` / `SPI_WIRED_BUS=<n>` /
`CAN_WIRED_BUS=<n>` in `wiring.ini` (or `board.h` for a purpose-built PCB) tell
the firmware which bus INSTANCE is physically wired on this unit, so it may be
brought up without an explicit `BUS I2C <n>` selection first. Omit one and
nothing is guessed — the firmware refuses and tells you what to run, rather
than falling back to a compiled-in default (the bug this closed: a stock build
used to silently claim Wire, or CAN1's pins, whether or not anything was
actually attached). Declaring a bus is a permission, not a lock: an explicit
`BUS I2C <n>` selection always wins over the declaration, and declaring bus 1
does not forbid selecting bus 0. See `src/pinctrl/bus_wiring.h` for the resolver.

## Adding / editing

- **New board:** `cp -r boards/teensy41 boards/<new>`, edit `board.h` (facts) and
  `board.ini` (`[env:<new>]`, platform/board/lib_deps, and the `${<new>.wiring}`
  injection + empty `[<new>] wiring =` default), and adjust `wiring.ini.example`.
- **Your wiring:** edit `boards/<board>/wiring.ini` only. Never the tracked files.
- **Core-TU caveat:** macros consumed by framework core TUs (e.g.
  `SERIAL_*_BUFFER_SIZE`) must be real `-D` flags in `board.ini` — `-include`
  doesn't reach those TUs, so they can't live in `board.h`.

## Verifying a change

PlatformIO does **not** track `-include`d headers as build dependencies, so always
clean-build when checking sizes (`pio run -e <env> -t clean && pio run -e <env>`).
`board.h` + `resolve.h` must resolve to the same `INCLUDE_*`/`MAX_*` set as
before for capability-only features; WIRED features (LED, mode button, SD, bus instances) are
intentionally gated on their overlay pins.
