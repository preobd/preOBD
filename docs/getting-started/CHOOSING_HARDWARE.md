# Choosing Hardware

preOBD runs on several boards. If you are buying one for this project, get a
**Teensy 4.1** — it runs every feature, has a built-in SD card slot and native
CAN, and has room to grow.

## Board Comparison

| | Teensy 4.1 | Teensy 4.0 | Teensy 3.6 | ESP32-S3 | Feather STM32F405 | Arduino Mega 2560 |
|---|---|---|---|---|---|---|
| **Recommended** | ✅ Yes | Good | Older | Good | Good | Limited |
| Logic voltage | 3.3V | 3.3V | 3.3V | 3.3V | 3.3V | 5V |
| ADC | 12-bit | 12-bit | 13-bit | 12-bit | 12-bit | 10-bit |
| Max inputs | 40 | 40 | 32 | 32 | 40 | 8 |
| CAN | 3 × native (FlexCAN) | 3 × native | 2 × native | 1 × native (TWAI) | 1 × native (bxCAN) | 1 × MCP2515 module (SPI) |
| SD card logging | Built-in slot | External module (`SD_PIN`) | Built-in slot | External module (`SD_PIN`) | Built-in slot | ❌ |
| LCD, alarms, relays | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (2 alarm Rules, 2 relays) |
| OBD-II output (Torque, etc.) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| RealDash output | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| ELM327 emulator over Bluetooth | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| JSON backup/restore | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Test mode (simulated sensors) | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| CLI history and line editing | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ (plain line input) |

Every native CAN port needs an external **CAN transceiver** chip or module
(SN65HVD230 on 3.3V boards, MCP2551/TJA1050 on 5V) — see the
[CAN Transceiver Guide](../guides/hardware/CAN_TRANSCEIVER_GUIDE.md).

### Why is the Mega limited?

It has 8 KB of RAM. preOBD's globals take about 86% of it, and the rest is
the stack, which needs about 620 B at its peak (`SAVE`). The features marked ❌
are left out of the Mega build because they don't fit, not because they are
unfinished. Its command line takes the same commands as every other board, but
without history or cursor editing: up-arrow and left/right do nothing, and
backspace still works. Type commands in full: the other boards complete an
abbreviation such as `VERS` when you press Enter, and the Mega doesn't.

The Mega is fine for a simple dash of up to 8 sensors on 5V senders.

### Feather STM32F405 notes

The Adafruit Feather STM32F405 Express runs every feature. Things that differ
from the Teensy:

- **Uploading is over USB DFU** (`pio run -e feather_f405 -t upload`). The
  first time, preOBD isn't on the board yet: fit a jumper from **B0** to
  **3.3V**, press reset, upload, then remove the jumper. After that, uploads
  reboot the board into DFU on their own.
- **CAN needs an external transceiver** (SN65HVD230) on **D9 (RX) / D10 (TX)**.
- **The microSD slot is built in** and uses the fast SDIO bus, not SPI.
- **The on-board NeoPixel is the status LED**, so there's nothing to wire for it.
- **A6 is not a header pin.** It reads the board's BAT pin (the LiPo connector)
  through a 2:1 divider. Measure a car battery through your own divider on a free
  analog pin, and never connect 12V to BAT.
- The 20×4 LCD's backpack usually needs **5V**: power it from the **USB** pin.
  SDA/SCL tolerate 5V.

## 3.3V vs 5V

This matters more than anything else when wiring:

- **3.3V boards (Teensy, ESP32, Feather) are not 5V tolerant.** Putting 5V on an input pin
  damages it. Many automotive sensors output up to 4.5V or 5V — use a voltage
  divider ([how](../guides/sensor-types/PRESSURE_SENSOR_GUIDE.md)), and power
  resistive senders' bias resistors from 3.3V.
- **Anything connected to 12V** — battery voltage, the alternator W terminal,
  hall-effect speed sensors — needs a divider or protection circuit on *every*
  board. Each sensor guide shows the circuit.

## Other Parts You May Want

| Part | For | Guide |
|------|-----|-------|
| 20×4 I2C character LCD (PCF8574 backpack) | Dash display | [LCD Display Guide](../guides/outputs/LCD_DISPLAY_GUIDE.md) |
| CAN transceiver | OBD-II output, RealDash, CAN import | [CAN Transceiver Guide](../guides/hardware/CAN_TRANSCEIVER_GUIDE.md) |
| HM-10 BLE module | Phone OBD-II apps without an adapter; wireless configuration | [Bluetooth Hardware Guide](../guides/hardware/BLUETOOTH_HARDWARE_GUIDE.md) |
| MAX6675 / MAX31855 module + K-type thermocouple | CHT, EGT | [Thermocouple Guide](../guides/sensor-types/THERMOCOUPLE_GUIDE.md) |
| BME280 module | Ambient temperature, pressure, humidity | [BME280 Guide](../guides/sensor-types/BME280_GUIDE.md) |
| Piezo buzzer, momentary button, RGB LED | Alarm output, alarm silence, status | [LED Indicator Guide](../guides/hardware/LED_INDICATOR_GUIDE.md), [Alarm System Guide](../guides/configuration/ALARM_SYSTEM_GUIDE.md) |
| Relay module | Cooling fans, pumps, lights | [Relay Control Guide](../guides/outputs/RELAY_CONTROL.md) |
| 100 Ω and 2.49 kΩ 1% resistors | Bias resistors for resistive senders | [Bias Resistor Guide](../guides/hardware/BIAS_RESISTOR_GUIDE.md) |
| Automotive 12V → 5V supply | Powering the board in the car | — |

Next: [Getting Started](GETTING_STARTED.md).
