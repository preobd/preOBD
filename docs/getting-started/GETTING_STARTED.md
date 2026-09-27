# Getting Started: From Zero to Your First Reading

This walks you from nothing installed to a live temperature reading on your
computer screen, on the bench, in about an hour. You don't need a car, a sensor
or any soldering — just a board, a USB cable, two resistors and a breadboard.

Once this works, wiring real senders in the car is the same steps with a
different part on the end of the wire.

> **Safety first.** preOBD is beta software. Keep your mechanical gauges, and
> don't rely on it for anything that could hurt you or your engine until you have
> tested it thoroughly. See [DISCLAIMER.md](../../DISCLAIMER.md).

---

## What You Need

| Item | Notes |
|------|-------|
| A supported board | **Teensy 4.1** recommended; Arduino Mega 2560, Teensy 4.0/3.6 and ESP32-S3 also work. See [Choosing Hardware](CHOOSING_HARDWARE.md) |
| USB cable | Micro-USB for Teensy; USB-B for the Mega. It must be a *data* cable, not charge-only |
| Breadboard and jumper wires | |
| 100 Ω resistor | The "bias" resistor every resistive sender needs |
| 47 Ω, 100 Ω and 220 Ω resistors | Stand-ins for a VDO temperature sender (one at a time) |
| A computer | Windows, macOS or Linux |

---

## Step 1: Install the Tools

preOBD is built with **PlatformIO**. The easiest way to get it:

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. In VS Code, open the Extensions view, search for **PlatformIO IDE** and install it.
   Restart VS Code when it asks.
3. Install [Git](https://git-scm.com/downloads) if you don't already have it.

Prefer the command line? `pip install platformio` gives you the same `pio` command.

**Linux only:** Teensy boards need a udev rule before the uploader can see them.
Follow the one-line instructions on the
[PJRC udev page](https://www.pjrc.com/teensy/00-teensy.rules).

---

## Step 2: Get the Code

```bash
git clone https://github.com/preobd/preOBD.git
cd preOBD
```

In VS Code: **File → Open Folder** and choose the `preOBD` folder. PlatformIO
recognises the project and may spend a few minutes downloading the toolchain the
first time.

---

## Step 3: Build and Upload

Plug the board in over USB, then open a terminal (in VS Code: **Terminal → New
Terminal**) and run the line for your board:

```bash
pio run -e teensy41 -t upload      # Teensy 4.1
pio run -e teensy40 -t upload      # Teensy 4.0
pio run -e mega2560 -t upload      # Arduino Mega 2560
pio run -e esp32s3dev -t upload    # ESP32-S3
```

The first build takes a few minutes. It ends with `SUCCESS` when the firmware is
on the board.

**If the upload fails:** on a Teensy, press the white button on the board and run
the command again. On a Mega, make sure no other program (such as the Arduino IDE
serial monitor) has the port open.

You don't need to change any configuration files to follow this guide. Later,
when you add an LCD, buzzer, status LED or mode button, you'll describe your
wiring in `boards/<board>/wiring.ini` — see the
[Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md).

---

## Step 4: Open the Serial Monitor

```bash
pio device monitor
```

(115200 baud; PlatformIO picks the speed up automatically.) Press the board's
reset button, or unplug and replug it, and you should see the preOBD banner,
the firmware version, and then:

```
========================================
  NO CONFIGURATION FOUND
  Automatically entering CONFIG mode
========================================
```

That's expected on a new board: nothing is configured yet, so it waits in
**CONFIG mode** for you. Type `HELP` and press Enter to check that it's listening.

> Any serial terminal works (PuTTY, screen, the Arduino IDE's serial monitor).
> Commands are not case-sensitive.

---

## Step 5: Wire a Pretend Sender

A VDO temperature sender is just a resistor whose value changes with temperature.
preOBD measures it through a **voltage divider**: a fixed 100 Ω **bias resistor**
from the board's supply to the analog pin, and the sender from the analog pin to
ground. On the bench, a fixed resistor stands in for the sender.

```
3.3V (Teensy/ESP32) or 5V (Mega)
      │
    [100 Ω]   ← bias resistor
      │
      ├──────► A0
      │
    [100 Ω]   ← stand-in for the sender (try 47 Ω and 220 Ω later)
      │
     GND
```

| Board | Supply pin | Analog pin A0 |
|-------|------------|---------------|
| Teensy 4.1 / 4.0 | 3.3V | A0 (pin 14) |
| Arduino Mega | 5V | A0 |
| ESP32-S3 | 3.3V | A0 per your board's pinout |

> ⚠️ **Never connect 5V to a Teensy or ESP32 pin.** They are 3.3V parts and a 5V
> signal will destroy the pin or the board. Use the 3.3V supply pin on those boards.

---

## Step 6: Configure the Input

In the serial monitor, type:

```
SET A0 COOLANT_TEMP VDO_120C_TABLE
```

This tells preOBD that pin A0 measures **coolant temperature** (the
*application*) with a **VDO 120 °C sender** (the *sensor*). preOBD picks the
right calibration, units and a default over-temperature alarm for you.

Check it:

```
LIST INPUTS
```

You should see something like:

```
Active Inputs:
  A0: ECT (Engine Coolant Temperature) = 69.8 C
```

With a 100 Ω stand-in the reading is **about 70 °C**. Swap the stand-in and watch
it change (`LIST INPUTS` again, or `INFO A0` for more detail):

| Stand-in resistor | Approximate reading |
|-------------------|---------------------|
| 220 Ω | ~47 °C |
| 100 Ω | ~70 °C |
| 47 Ω | ~92 °C |

Resistor tolerance means you'll be a degree or two off — that's normal.

Want °F? `SET A0 UNITS F`.

---

## Step 7: Save and Run

```
SAVE
RUN
```

`SAVE` writes the configuration to the board's memory so it survives a power
cycle. `RUN` switches to normal operation: configuration is locked and the
alarm, logging and relay features come alive. From now on the board starts in
RUN mode by itself.

To change anything later, type `CONFIG`, make the change, `SAVE`, `RUN`.

**See the numbers stream:** in RUN mode, `OUTPUT Serial ENABLE` (from CONFIG
mode, then `SAVE`) prints a line of comma-separated readings every second.

---

## Congratulations — What Next?

You've done the whole loop: build, flash, configure, read. Everything else in
preOBD is more of the same.

**Put it in the car**
- Find your sender in the [Sensor Selection Guide](../guides/sensor-types/SENSOR_SELECTION_GUIDE.md)
  (`LIST SENSORS` shows the same list on the board)
- Read the guide for its type — [thermistors](../guides/sensor-types/THERMISTOR_GUIDE.md),
  [pressure](../guides/sensor-types/PRESSURE_SENSOR_GUIDE.md),
  [thermocouples](../guides/sensor-types/THERMOCOUPLE_GUIDE.md),
  [battery voltage](../guides/sensor-types/VOLTAGE_SENSOR_GUIDE.md),
  [RPM](../guides/sensor-types/W_PHASE_RPM_GUIDE.md) — for its wiring and
  any protection it needs
- Check which bias resistor it needs: [Bias Resistor Guide](../guides/hardware/BIAS_RESISTOR_GUIDE.md)

**Set alarms** — [Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md)

**Show the data**
- An LCD on the dash — [LCD Display Guide](../guides/outputs/LCD_DISPLAY_GUIDE.md)
- A phone OBD-II app (Torque, OBD Fusion) — [OBD-II Scanner Guide](../guides/outputs/OBD2_SCANNER_GUIDE.md)
- RealDash — [RealDash Setup Guide](../guides/outputs/REALDASH_SETUP_GUIDE.md)
- Log to an SD card — [Data Logging Guide](../guides/outputs/DATA_LOGGING_GUIDE.md)

**Control things** — switch a cooling fan with the [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md)

**Stuck?** — [Troubleshooting](../TROUBLESHOOTING.md), and the
[Quick Reference](QUICK_REFERENCE.md) for the everyday commands.
