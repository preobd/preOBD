# Board configuration

Each supported board owns one folder here. A folder is the *whole* board:

```
boards/
  <board>/
    board.h              # board definition: capabilities, CEILING vetoes, sizing,
                         #   CHOICE feature defaults, intrinsic pins (tracked)
    board.ini            # the PlatformIO [env:<board>] fragment (tracked)
    wiring.ini.example   # template for your physical wiring (tracked)
    wiring.ini           # YOUR pins, as -D flags (gitignored)
  resolve.h              # shared resolver — derives wired ENABLE_* flags
```

`platformio.ini` stays thin: shared `[env]` base + library bundles, and
`extra_configs = boards/*/board.ini, boards/*/wiring.ini` merges every board
folder in. To add a board, drop a new folder — nothing shared is edited.

## The model: three kinds of knob

Borrowed from Kconfig/devicetree (and Marlin's header-only take). Every feature
is exactly one of:

| Kind | Meaning | Lives in |
|---|---|---|
| **CEILING** | the board genuinely *can't* (RAM/flash) or has fixed capacity | `board.h` — `PLATFORM_NO_*` veto, `MAX_*`, `SUPPORTS_JSON_CONFIG=0` |
| **WIRED** | capable; active *iff* the hardware is wired | a pin in `wiring.ini`; the `ENABLE_*` is derived in `resolve.h` |
| **CHOICE** | capable; on/off is a preference | `board.h` default |

Resolution rule (in `resolve.h`):

```
feature ON  ⟺  (intrinsic OR wired OR chosen)  AND  (board doesn't veto it)
```

A kind is **per (board, feature)** — e.g. `FRAMED_TRANSPORT` is a CHOICE on
Teensy but a CEILING on the Mega (8 KB RAM). See `docs/feature_costs.md` for the
measured per-feature costs that justify the Mega ceilings.

## Why pins live in a gitignored overlay

A pin number is a fact about *your* physical unit, not about the chip — there's
no correct default for `RGB_PIN_R` or `BUZZER_PIN`. So `board.h` never names a
wired pin; you put yours in `wiring.ini` (copy `wiring.ini.example`). A fresh
clone builds with wired peripherals **off** until you fill in the overlay — the
firmware boots fine, just without LED/buzzer/button/SD until wired. This is what
keeps personal wiring out of the repo (and stops bench edits colliding with it).

`resolve.h` derivations are `#ifndef`-guarded, so an explicit override still wins
(e.g. `-D ENABLE_LED=0` to force a wired feature off).

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
`board.h` + `resolve.h` must resolve to the same `ENABLE_*`/`MAX_*`/`SUPPORTS_*`
set as before for CEILING/CHOICE features; WIRED features are intentionally
gated on their overlay pins.
