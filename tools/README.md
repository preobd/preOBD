# openEMS Static Configuration Toolchain

This directory contains Python tools for generating and validating static configurations for the openEMS project.

## configure.py

An interactive command-line tool to generate a `config.h` file with static sensor definitions. This avoids the need for EEPROM and allows for smaller firmware sizes.

### Features

- **Interactive Prompts**: Guides the user through selecting pins, applications, and sensors.
- **Platform-Aware**: Automatically detects the target board from `platformio.ini` and applies hardware limits.
- **Pin Validation**: Prevents duplicate or reserved pin assignments.
- **JSON Save/Load**: Configurations can be saved to a JSON file and loaded later for reuse or modification.
- **Thin Library Generation**: Can generate "thinned" versions of the sensor and application libraries containing only the configured items.

### Usage

**Interactive Mode**

To start the interactive configuration process, run:

```bash
python3 tools/configure.py
```

The tool will prompt you for each input's pin, application, and sensor.

**Loading a Configuration**

To load a previously saved configuration from a JSON file, use the `--load` flag:

```bash
python3 tools/configure.py --load configs/my_vehicle_config.json
```

**Generating Thinned Libraries**

To generate `sensor_library_static.h` and `application_presets_static.h` in `src/lib/generated/`, use the `--generate-thin-libs` flag. These files will contain only the sensors and applications used in your configuration.

```bash
python3 tools/configure.py --generate-thin-libs
```

## validate_registries.py

A command-line tool for validating the integrity of the C registry files (`sensor_library.h`, etc.). This is intended for use in Continuous Integration (CI) to catch errors like hash collisions or invalid cross-references.
