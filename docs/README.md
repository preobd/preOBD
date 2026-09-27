# preOBD Documentation

See [DISCLAIMER](../DISCLAIMER.md) for safety information and software limitations.

**New to preOBD?** Read these in order:

1. [Choosing Hardware](getting-started/CHOOSING_HARDWARE.md) — which board and parts to buy
2. [Getting Started](getting-started/GETTING_STARTED.md) — install, flash and get your first reading on the bench
3. [Sensor Selection Guide](guides/sensor-types/SENSOR_SELECTION_GUIDE.md) — find the senders you have
4. [Quick Reference](getting-started/QUICK_REFERENCE.md) — the everyday commands

Something not working? [Troubleshooting](TROUBLESHOOTING.md).

---

## How preOBD Works

Each **input** is a sensor on a pin, configured with two names:

- the **application** — what it measures (`COOLANT_TEMP`, `OIL_PRESSURE`, `ENGINE_RPM`)
- the **sensor** — the hardware (`VDO_120C_TABLE`, `MAX6675`, `W_PHASE_RPM`)

```
SET A2 COOLANT_TEMP VDO_120C_TABLE
```

preOBD picks the calibration, units and a sensible default alarm from those two
names. Readings then go to every output you enable — LCD, OBD-II, RealDash, SD
card, serial — and to **Rules**, which sound alarms and switch relays.

You configure preOBD by typing commands over USB in **CONFIG mode**, `SAVE` them to
the board's memory, and switch to **RUN mode** for normal use. `HELP` on the device
lists every command.

---

## Guides

### Sensors
| Guide | Covers |
|-------|--------|
| [Sensor Selection](guides/sensor-types/SENSOR_SELECTION_GUIDE.md) | Every supported sensor and application |
| [Thermistors](guides/sensor-types/THERMISTOR_GUIDE.md) | VDO, Smiths, AC Delco, Bosch, Jeep senders; generic NTC |
| [Thermocouples](guides/sensor-types/THERMOCOUPLE_GUIDE.md) | MAX6675 / MAX31855 for CHT and EGT |
| [Pressure](guides/sensor-types/PRESSURE_SENSOR_GUIDE.md) | Resistive senders, 0.5–4.5V sensors, MAP |
| [Voltage](guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md) | Battery monitoring |
| [W-Phase RPM](guides/sensor-types/W_PHASE_RPM_GUIDE.md) | RPM from the alternator |
| [Hall-Effect Speed](guides/sensor-types/HALL_SPEED_GUIDE.md) | Vehicle speed |
| [Digital Inputs](guides/sensor-types/DIGITAL_SENSOR_GUIDE.md) | Float switches |
| [BME280](guides/sensor-types/BME280_GUIDE.md) | Ambient temperature, pressure, humidity |
| [CAN Sensor Import](guides/sensor-types/CAN_SENSOR_IMPORT_GUIDE.md) | Reading OBD-II PIDs from an ECU |

### Wiring and Hardware
| Guide | Covers |
|-------|--------|
| [Bias Resistors](guides/hardware/BIAS_RESISTOR_GUIDE.md) | The resistor every resistive sender needs |
| [Pin Requirements](guides/hardware/PIN_REQUIREMENTS_GUIDE.md) | Which pins suit which sensors |
| [CAN Transceivers](guides/hardware/CAN_TRANSCEIVER_GUIDE.md) | CAN bus hardware |
| [Bluetooth](guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md) | HM-10 and other BLE modules; wireless configuration |
| [LED Indicator](guides/hardware/LED_INDICATOR_GUIDE.md) | RGB status LED |

### Configuration
| Guide | Covers |
|-------|--------|
| [Config/Run Mode](guides/configuration/CONFIG_RUN_MODE_GUIDE.md) | The two modes and the mode button |
| [Alarms](guides/configuration/ALARM_SYSTEM_GUIDE.md) | Alarm limits, warnings, acknowledging |
| [Advanced Calibration](guides/configuration/ADVANCED_CALIBRATION_GUIDE.md) | Custom calibration for unlisted sensors |
| [JSON Backup and Restore](guides/configuration/JSON_CONFIGURATION_GUIDE.md) | Export, import, SD card backups |
| [Build Configuration](guides/configuration/BUILD_CONFIGURATION_GUIDE.md) | `wiring.ini`, board definitions, feature flags |

### Outputs
| Guide | Covers |
|-------|--------|
| [LCD Display](guides/outputs/LCD_DISPLAY_GUIDE.md) | 20×4 I2C LCD |
| [OBD-II Scanners](guides/outputs/OBD2_SCANNER_GUIDE.md) | ELM327 adapters, Torque, OBD Fusion |
| [Direct BLE OBD](guides/outputs/DIRECT_BLE_OBD_GUIDE.md) | OBD-II apps over Bluetooth, no adapter |
| [RealDash](guides/outputs/REALDASH_SETUP_GUIDE.md) | RealDash dashboards |
| [Data Logging](guides/outputs/DATA_LOGGING_GUIDE.md) | SD card and serial CSV |
| [Relay Control](guides/outputs/RELAY_CONTROL.md) | Fans, pumps and lights |

---

## Reference

- [Serial Commands](reference/SERIAL_COMMANDS.md) — every command
- [OBD-II PID Reference](reference/OBD2_PID_REFERENCE.md) — the PIDs preOBD answers
- [Troubleshooting](TROUBLESHOOTING.md)

## Development

For contributors: [development documentation](development/README.md) — code
layout, architecture, adding sensors.

---

## Getting Help

- **GitHub Issues** — bug reports
- **GitHub Discussions** — questions and setup help

Include your board, `VERSION`, `SYSTEM DUMP` and `SYSTEM DIAG` output.
