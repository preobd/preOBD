# preOBD - Open Source Engine Monitoring System

**⚠️ BETA SOFTWARE** - See [DISCLAIMER](DISCLAIMER.md) for important safety information.

Open-source engine monitoring for classic cars and other vehicles without OBD-II
diagnostics.

---

## What is preOBD?

preOBD reads the temperature, pressure, voltage, RPM and speed senders a classic
car already has — or new ones you fit — and makes the readings available on a
dash LCD, in OBD-II phone apps, in RealDash, and in SD-card logs. It can sound
alarms and switch relays for fans and pumps. It runs on a Teensy, ESP32-S3,
Feather STM32F405 or Arduino Mega and is configured by typing commands over USB.

**What it does:**
- Reads temperature (thermocouples, VDO/Smiths/Stewart-Warner/AC Delco/Bosch/Jeep senders, NTC thermistors, 0.5–4.5V sensors)
- Reads pressure (resistive senders, 0.5–4.5V sensors, MAP sensors), fuel level and throttle position
- Reads battery voltage, engine RPM (from the alternator W terminal) and vehicle speed (hall-effect sensors)
- Reads ambient temperature, pressure and humidity (BME280)
- Imports OBD-II PIDs from a modern ECU on the CAN bus — mix classic gauges with an engine swap's data
- Shows readings on a 20×4 LCD
- Answers OBD-II requests over CAN, so ELM327 adapters and apps such as Torque and OBD Fusion work
- Emulates an ELM327 over Bluetooth LE, so OBD-II apps can connect with no adapter
- Streams to RealDash
- Logs to an SD card or the serial port
- Sounds alarms and switches 12V relays (cooling fans, pumps, warning lights)
- Can also be configured and monitored from a browser over Bluetooth, with the [Web Bluetooth page](https://preobd.github.io/preOBD/)

**What it doesn't do:**
- Engine control (monitoring only; relay outputs are auxiliary)
- Provide safety certification or guarantees

---

## Getting Started

1. **[Choose your hardware](docs/getting-started/CHOOSING_HARDWARE.md)** — a Teensy 4.1 is recommended
2. **[Getting Started](docs/getting-started/GETTING_STARTED.md)** — install the tools, flash the board and get your first reading on the bench, in about an hour
3. Find your senders in the **[Sensor Selection Guide](docs/guides/sensor-types/SENSOR_SELECTION_GUIDE.md)** and wire them up

Configuration is a few commands over USB:

```
CONFIG
SET A0 COOLANT_TEMP VDO_120C_TABLE      # pin, what it measures, which sender
SET A1 OIL_PRESSURE VDO_5BAR_CURVE
SET A1 ALARM MIN 0.7                    # alarm below 0.7 bar
SAVE
RUN
```

Type `HELP` on the device for the full command list.

---

## Supported Hardware

| Board | Notes |
|-------|-------|
| **Teensy 4.1** | Recommended — every feature, built-in SD slot, 3 native CAN buses |
| Teensy 4.0 / 3.6 | Every feature; native CAN |
| ESP32-S3 | Every feature; native CAN (TWAI) |
| Feather STM32F405 | Every feature; native CAN (bxCAN), built-in SD slot |
| Arduino Mega 2560 | 5V; up to 8 inputs; no SD logging, RealDash, Bluetooth OBD or CLI history (8 KB RAM) |

See [Choosing Hardware](docs/getting-started/CHOOSING_HARDWARE.md) for the full comparison.

---

## Documentation

**Start here**
- [Choosing Hardware](docs/getting-started/CHOOSING_HARDWARE.md)
- [Getting Started](docs/getting-started/GETTING_STARTED.md) — zero to first reading
- [Quick Reference](docs/getting-started/QUICK_REFERENCE.md) — everyday commands
- [Troubleshooting](docs/TROUBLESHOOTING.md)

**Guides** — sensors, wiring, alarms, relays, outputs: see the [documentation index](docs/README.md)

**Reference**
- [Serial Commands](docs/reference/SERIAL_COMMANDS.md) — every command
- [OBD-II PIDs](docs/reference/OBD2_PID_REFERENCE.md)

**Contributing** — [Development documentation](docs/development/README.md)

---

## Contributing

Contributions are welcome:

1. **Sensor calibrations** — share data for senders that aren't in the library yet
2. **Platform testing** — try it on your hardware and report back
3. **Documentation** — tell us what was unclear
4. **Bug reports** — help make it more reliable

Please test on hardware before submitting, include datasheets for new sensors, and
follow the existing code style. The [development documentation](docs/development/README.md)
explains how the code is organised.

## Community and Support

- **GitHub Issues** — bug reports and feature requests
- **GitHub Discussions** — questions and setup help

When asking for help, include your board, the output of `VERSION`, `SYSTEM DUMP`
and `SYSTEM DIAG`, and what you've already tried. See [Troubleshooting](docs/TROUBLESHOOTING.md).

## License

preOBD is licensed under a custom **MIT + NonCommercial License**.

### What You Can Do (Personal / Noncommercial Use)
- Use the software for personal projects
- Modify, fork, or extend the software
- Publish your modifications (noncommercially)
- Learn from and build upon the source code
- Use it for educational or research purposes

### What You Cannot Do (Without Permission)
- Sell the software
- Bundle it into a product you sell
- Use it in a for-profit business
- Monetize it in any form
- Distribute commercial derivatives

For commercial use, contact: info@preobd.com

See [LICENSE](LICENSE) file for complete terms.

---

**preOBD is beta software. See [DISCLAIMER](DISCLAIMER.md) for important safety information and limitations.**
