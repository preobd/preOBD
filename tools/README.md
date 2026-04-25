# preOBD Developer Tools

Python helpers for working on the preOBD codebase.

Configuration is done at runtime via the serial CLI; there are no compile-time
configuration generators.

## Scripts

### `generate_registry_enums.py`

Auto-generates `src/lib/generated/registry_enums.h` from the sensor library,
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

## Module: `preobd_config/`

- `registry_parser.py` — parses C++ registry headers into Python dicts.
- `validators.py` — validation rules used by `validate_registries.py`.
