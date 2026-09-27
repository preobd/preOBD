# Development Documentation

For people working on the preOBD firmware itself. If you just want to build and
use preOBD, start with [Getting Started](../getting-started/GETTING_STARTED.md).

## Orientation

- [Directory Setup](DIRECTORY_SETUP.md) — where everything lives in the source tree
- [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md) —
  `board.h`, `wiring.ini`, and the `SUPPORTS_*` → `INCLUDE_*` model
- [`boards/README.md`](../../boards/README.md) — adding or changing a board

## Common Tasks

- [Adding Sensors](ADDING_SENSORS.md) — new sensors, manufacturers, read functions and applications
- [JSON Schema Changes](JSON_SCHEMA_CHANGES.md) — keeping the configuration format backward compatible
- [Feature Costs](FEATURE_COSTS.md) — what each feature costs in flash and RAM on the Mega

## Architecture

- [Registry System](architecture/REGISTRY_SYSTEM.md) — hash-based sensor/application lookups
- [EEPROM Structure](architecture/EEPROM_STRUCTURE.md) — stored layout and versioning
- [CAN HAL](architecture/CAN_HAL_ARCHITECTURE.md) — CAN controller abstraction
- [Transport Architecture](architecture/TRANSPORT_ARCHITECTURE.md) — message routing over serial ports
- [BLE GATT Profile](architecture/BLE_GATT_PROFILE.md) — the Bluetooth protocol

## Tools and Checks

- `tools/generate_registry_enums.py`, `tools/validate_registries.py` — sensor/application registries
- `tools/check_layering.py` — directory dependency ratchet
- `tools/check_docs.py` — every command, sensor and application named in the docs exists in the firmware
- `pio test -e native` — host-side unit tests in `test/`

The pre-commit hook in `.githooks/` runs the registry generator, the layering
check and the docs check. Enable it once per clone:

```bash
git config core.hooksPath .githooks
```
