# Troubleshooting

Start here when something isn't working. Each entry says what to check first and
links to the guide with the details.

**Three commands answer most questions:**

```
SYSTEM DIAG          # checks the firmware tables, the hardware and every input
INFO <pin>           # everything about one input, including its current value
SYSTEM DUMP          # the whole configuration — include this when asking for help
```

---

## Building, Uploading and Connecting

**The upload fails.**
- Teensy: press the button on the board and run the upload again. On Linux, install
  the [PJRC udev rule](https://www.pjrc.com/teensy/00-teensy.rules) first.
- Close anything else that has the serial port open (another serial monitor, the Arduino IDE).
- Use a USB *data* cable — many charge-only cables look identical.

**The serial monitor shows nothing.**
- The speed must be 115200 baud.
- Press the board's reset button: the banner prints at boot.
- Type `HELP` and press Enter.

**"ERROR: … requires CONFIG mode".** Type `CONFIG`, make the change, `SAVE`, `RUN`.
See the [Config/Run Mode Guide](guides/configuration/CONFIG_RUN_MODE_GUIDE.md).

**My settings are gone after a power cycle.**
- Run `SAVE` before leaving CONFIG mode.
- After a firmware upgrade that changes the stored layout, the configuration resets
  and the CHANGELOG says so. Back up with `SYSTEM DUMP JSON` before upgrading and
  restore with `JSON IMPORT` ([JSON Configuration Guide](guides/configuration/JSON_CONFIGURATION_GUIDE.md)).
- On a Teensy 4.0 with many inputs, `SAVE` warns if they don't all fit in its EEPROM.

---

## Sensor Readings

**An input reads `ERROR`/`NaN` (`ERR` on the LCD).**
1. `SYSTEM DIAG INPUTS` — says whether the input looks open, shorted or out of range.
2. `SYSTEM DIAG PINS` — live voltage on every analog pin; find which pin the sensor is actually on.
3. Resistive senders need a **bias resistor from the pin to VCC** and the sender body grounded
   ([Bias Resistor Guide](guides/hardware/BIAS_RESISTOR_GUIDE.md)).
4. Thermocouples, the BME280 and other SPI/I2C devices need their bus brought up
   (`BUS SPI 0` / `BUS I2C 0`, `SAVE`, `SYSTEM REBOOT`). The boot log says
   "no I2C bus declared or selected" when this is missing.

**The reading is wrong but plausible.**
- Is it the right sensor? A VDO 120 °C and 150 °C sender look alike but read very
  differently. `INFO <pin>` shows the sensor configured.
- Is the bias resistor the value the sensor expects (100 Ω for gauge senders, 2.49 kΩ for
  EFI NTC sensors)? Otherwise tell the firmware: `SET <pin> CAL BIAS <ohms>`.
- A 0.5-4.5V sensor on a 3.3V board behind a divider needs `SET <pin> DIVIDER <ratio>`.
- Units: `SET <pin> UNITS F` (`SYSTEM UNITS` only sets what new inputs start in).
- See the guide for your sensor type and the [Advanced Calibration Guide](guides/configuration/ADVANCED_CALIBRATION_GUIDE.md).

**The reading is noisy or jumps around.**
- Add a 100 nF capacitor from the analog pin to ground, close to the board.
- Check grounds: the board, the sender body and the engine block must share a good ground.
- Route sensor wires away from ignition leads.

**"Sensor/application type mismatch".** The sensor doesn't measure what the application
expects (e.g. a pressure sender on `COOLANT_TEMP`). `LIST SENSORS TEMPERATURE` lists the
temperature sensors, and so on.

---

## Alarms and Relays

**An alarm doesn't sound.**
- Alarm limits are in base units: °C and bar, not °F and PSI.
- `INFO <pin> ALARM` — is there an alarm, and is it still in `WARMUP`?
- The buzzer is silent in CONFIG mode, and a sensor showing `ERR` never trips an alarm.
- `ALARM STATUS` — has it been acknowledged?
- See the [Alarm System Guide](guides/configuration/ALARM_SYSTEM_GUIDE.md).

**A relay doesn't switch.**
- Relays are held off in CONFIG mode — type `RUN`.
- `ACTION <id>` shows which Rule it follows; `RULE <id>` shows whether that Rule is active.
- Test the wiring with `ACTION <id> ON` in RUN mode; many modules need `INVERT`.
- See the [Relay Control Guide](guides/outputs/RELAY_CONTROL.md).

---

## Display and Outputs

**The LCD is blank.** `DISPLAY TYPE LCD`, `BUS I2C 0`, then `SAVE` and `SYSTEM REBOOT`.
Try `DISPLAY ADDRESS 0x3F`, and adjust the contrast pot.
See the [LCD Display Guide](guides/outputs/LCD_DISPLAY_GUIDE.md).

**The OBD-II app connects but shows no data.**
- The CAN bus must be brought up: `BUS CAN OUTPUT CAN1 ENABLE 500000`, then `OUTPUT CAN ENABLE`.
- CAN needs a transceiver and 120 Ω termination at each end of the bus.
- `LIST INPUTS` shows which PID each input answers on.
- See the [OBD-II Scanner Guide](guides/outputs/OBD2_SCANNER_GUIDE.md) and
  [CAN Transceiver Guide](guides/hardware/CAN_TRANSCEIVER_GUIDE.md).

**No SD log files.** SD logging pauses in CONFIG mode. `SYSTEM DIAG HW` reports
whether the card is mounted and rows are being written.
See the [Data Logging Guide](guides/outputs/DATA_LOGGING_GUIDE.md).

**Bluetooth module doesn't respond.** Enable its serial port first
(`BUS SERIAL <n> ENABLE <baud>`); the baud rate must match the module's. See the
[Bluetooth Hardware Guide](guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md).

---

## Asking for Help

Open a GitHub issue or discussion and include:

1. The board (Teensy 4.1, Mega, …) and the output of `VERSION`
2. The output of `SYSTEM DUMP`
3. The output of `SYSTEM DIAG`
4. What you expected, and what happened instead
