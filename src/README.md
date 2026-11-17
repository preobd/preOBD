# src/ — one folder per layer

> **Sensors feed inputs. Rules decide. Routes send inputs to outputs.
> Transports carry. Buses are shared. The HAL hides the chip.**

A directory listing sorts alphabetically. This is the order the data flows in:

```
sensors/      ACQUIRE       wired sensor drivers, CAN input (can/, readers in can/readers/), test mode (simulate/)
   │
inputs/       STATE         the value table: inputs[], each input's value, units, health
   │
rules/        DECIDE        rules and actions
   │
outputs/      DESTINATIONS  table/      the output coordinator
   │                        formats/    CSV, RealDash, OBD-II, ELM327, SD log
   │                        applink/    built-in: BLE status, alarms, actions, live values
   │                        display/    built-in: LCD, OLED
   │                        annunciator/ built-in: buzzer, RGB LED
   │
transports/   CARRY         what carries an output's or the console's bytes:
   │                        framed nRF link, ESP32 Bluetooth, serial stream, SD mount
   │
buses/        SHARE         I2C, SPI, CAN, UARTs, each started once for everyone who
   │                        needs it; which buses the config needs; pin ownership (pinctrl/)
   │
hal/          CHIP          one interface, per-MCU code: CAN controllers, watchdog

console/      beside        the CLI and the button
config/       beside        saved settings: EEPROM, JSON import/export
core/         under all     messaging and the log, system mode, platform, watchdog
```

There is no message bus. The middle of the system is the value table:
sensors write it, and rules and outputs read it when they are due.

Rules of thumb:

- A file goes in the layer whose noun describes it. A CAN signal is a sensor,
  so CAN input lives in `sensors/can/`. A shared peripheral is a bus, even
  when an output is its only user today.
- Dependencies should point down the list. Today the enforced rules are
  that `core/` may include only `hal/`, and that files in `outputs/formats/`
  may not include `hal/` or `transports/` (`tools/check_layering.py`). Every
  other edge that crosses folders, including the ones that point up, is
  listed with its reason in `tools/layering_exceptions.txt`, and the list may
  only shrink.
- `config.h` (application constants) and `config/` (saved settings) are
  different things.
