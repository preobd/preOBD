# preOBD Developer Tools

Python helpers for working on the preOBD codebase.

Configuration is done at runtime via the serial CLI; there are no compile-time
configuration generators.

## Scripts

### `generate_registry_enums.py`

Auto-generates `src/sensors/generated/registry_enums.h` from the sensor library,
application presets, and units registry. Run after adding or reordering entries
in any registry.

```bash
python3 tools/generate_registry_enums.py
```

### `validate_registries.py`

Checks the sensor, application, and units registries for hash collisions,
broken cross-references, and inconsistent measurement types.

```bash
python3 tools/validate_registries.py
python3 tools/validate_registries.py --ci   # exit non-zero on errors
```

### `check_docs.py`

Checks `README.md` and `docs/` against the firmware: every serial command,
subcommand, `SET` field, `CAL` profile, sensor and application name used in the
docs must exist, sensors must suit their application, and retired commands
(`RELAY`, `SELFTEST`) are flagged. Everything it checks against is read from
`src/` each run. Runs from the pre-commit hook.

```bash
python3 tools/check_docs.py              # all docs; exit 1 on problems
python3 tools/check_docs.py docs/x.md    # specific files
```

Mark an intentional non-command (a placeholder in a tutorial) with
`<!-- docs-check: ignore -->` on the line before its code block.

## Module: `preobd_config/`

- `registry_parser.py` — parses C++ registry headers into Python dicts.
- `validators.py` — validation rules used by `validate_registries.py`.

## Bench: `bench/obd_ecu_sim/`

A standalone PlatformIO project (Mega 2560 + MCP2515 shield) that acts as an
OBD-II engine ECU: it answers Mode 01 requests on `0x7DF`/`0x7E0` from `0x7E8`
and counts every request it sees, for testing CAN input polling (#476) against
something other than preOBD's own responder. Serial knobs add answer delay,
an unsupported PID (silent or NRC) and a silent ECU; stats print every 5 s.
`s` (silent) still ACKs frames, so to test a dead bus, power the Mega off.

```bash
cd tools/bench/obd_ecu_sim
pio run -t upload && pio device monitor   # set MCP_CS_PIN / MCP_CRYSTAL_MHZ in its platformio.ini first
```
