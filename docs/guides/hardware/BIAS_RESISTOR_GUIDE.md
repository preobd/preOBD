# Bias Resistor Selection Guide

This guide covers bias resistor selection for resistive sensor inputs on DIY preOBD builds using discrete components on Arduino, Teensy, or any 3.3V/5V microcontroller platform.

---

## How the Circuit Works

Resistive sensors (temperature and pressure senders) are not powered sources — they are variable resistors. The voltage divider circuit converts the sensor's changing resistance into a voltage the ADC can read:

```
VCC (5V or 3.3V)
    │
    R_sensor (variable — changes with temperature or pressure)
    │
    ├─────► ADC Input
    │
    R_bias (fixed resistor you choose and install)
    │
   GND
```

The voltage at the ADC input is:

```
V_ADC = VCC × (R_bias / (R_sensor + R_bias))
```

The firmware inverts this to compute resistance from the ADC reading, then looks up the resistance in a calibration table.

**Key principle:** For best ADC resolution across the sensor's operating range, the bias resistor value should be in the same order of magnitude as the sensor's resistance at the temperatures/pressures you care about. Too high and you waste ADC range; too low and the signal is compressed near the rail.

---

## The Two Sensor Families

Extensive research across the major sender families used in classic and vintage vehicles reveals that resistive sensors fall cleanly into two resistance families. Your bias resistor choice depends entirely on which family your sensor belongs to.

### Family 1 — Low-Resistance Gauge Senders (2–330Ω)

These are mechanical gauge senders designed to drive a D'Arsonval or air-core gauge movement directly. They use a low-resistance wiper on a resistive element, and their resistance range tops out below ~330Ω in normal operating conditions.

**Validated sensors in this family:**

| Manufacturer | Type | Cold/Zero | Hot/Full | Notes |
|---|---|---|---|---|
| VDO SingleViu | Coolant temp (120°C range) | ~1,743Ω below range; **22Ω at 120°C** | — | High resistance only far outside operating range |
| VDO SingleViu | Coolant temp (150°C range) | — | ~10Ω | |
| VDO SingleViu | Oil pressure (0–5 bar) | 10Ω at 0 bar | 184Ω at 5 bar | |
| VDO SingleViu | Oil pressure (0–10 bar) | 10Ω at 0 bar | 184Ω at 10 bar | |
| VDO SingleViu | Fuel level | ~10Ω | ~180Ω | |
| Smiths | Coolant / oil temp | ~10–300Ω operating range | | Classic British vehicles |
| Stewart Warner | Temp / pressure senders | ~10–300Ω operating range | | Pre-EFI American vehicles |
| Pre-EFI Ford | Gauge temp sender | ~10–78Ω | | Pre-1980s classic Ford |
| Classic GM | Gauge temp sender (1967–1978) | ~46–350Ω | | Pre-EFI trucks and cars |
| Jeep CJ (AMC era, 1972–1986) | Coolant temp | 73Ω cold | 9Ω hot | |
| Mopar 56026779 | Jeep 4.0 oil pressure | 90Ω at 0 psi | 2Ω at 80 psi | Near-rail at max pressure with 100Ω bias |
| Marine gauge senders | Various | ~10–300Ω | | |

**Recommended bias resistor: 100Ω**

> **Why not lower?** The geometric mean of VDO's operating resistance range is ~47–57Ω. However, 100Ω is preferred because it is a standard value, keeps the ADC voltage well within range at the cold end, and avoids rail-saturation risk on very-low-resistance senders like the Mopar oil pressure sender (2Ω at full scale). 47Ω would give slightly better mid-range resolution but risks clipping.

> **Why not 470Ω or 1kΩ?** At 470Ω bias, the VDO coolant sender at 90°C (about 38Ω) produces only 30–40 ADC counts of swing across a 20°C span — noticeably degraded resolution. At 1kΩ the situation is worse. These values were studied and rejected.

**ADC utilization with 100Ω bias (5V reference, 12-bit ADC):**

| Condition | Sensor R | ADC counts |
|---|---|---|
| VDO coolant, 50°C | ~197Ω | ~2,700 |
| VDO coolant, 90°C | ~38Ω | ~3,720 |
| VDO coolant, 120°C | ~22Ω | ~3,900 |
| VDO pressure, 0 bar | 10Ω | ~4,010 |
| VDO pressure, 5 bar | 184Ω | ~2,600 |
| Jeep oil, 0 psi | 90Ω | ~2,150 |
| Jeep oil, 80 psi | 2Ω | ~4,090 (near rail) |

The Mopar oil pressure sender at maximum pressure (2Ω) approaches the ADC rail. This is inherent to the sensor's design and not a problem with the bias resistor choice — it is within firmware's `ADC_RAIL_MARGIN` guard and readings at that point have inherently lower precision.

---

### Family 2 — High-Impedance NTC Thermistors (100Ω–10kΩ+)

These are negative-temperature-coefficient thermistors used in EFI systems as ECT (engine coolant temperature) and IAT sensors. They have much higher resistance at low temperatures and are not interchangeable with gauge senders. Their resistance at normal operating temperatures is in the hundreds to thousands of ohms.

**Validated sensors in this family:**

| Manufacturer / Application | Type | Cold end | Hot end | Notes |
|---|---|---|---|---|
| GM TBI/TPI (1979–1995) | ECT sensor | ~9,420Ω at 0°C | ~177Ω at high temp | Used in EFI retrofit builds |
| AC Delco (GM 1979+) | ECT / CLT | Same GM curve | | |
| Bosch NTC M12 | Coolant temp | ~2,500–9,000Ω cold | ~100–300Ω hot | Common Euro EFI |
| Jeep XJ / Mopar 56027012 | Coolant temp gauge sender | 7,800Ω at 0°C | 135Ω at 120°C | Misleadingly looks like a gauge sender but is high-impedance NTC |
| Jeep Renix CTS (1987–1990) | ECT (ECU sensor) | ~9,400Ω at −20°C | ~200Ω at 180°C | Renault-designed Jeep 4.0 EFI |
| Haltech / aftermarket EFI | CLT/IAT | Varies, typically GM-curve | | |

**Recommended bias resistor: 2.49kΩ** (use 2.4kΩ or 2.5kΩ standard values — 2.49kΩ is the E96 standard value and matches MegaSquirt calibration tables)

> **Why 2.49kΩ?** The operating resistance range for these sensors spans roughly 200Ω to 9,000Ω. The geometric mean of that range is ~1,300Ω. A 2.49kΩ bias resistor is close to optimal for the hot end (where resolution matters most) while still giving usable readings at cold-soak temperatures. It is also the standard value used by MegaSquirt.

> **Note on the Mopar 56027012:** This XJ-era Jeep sender (1984–2001) reads as a high-impedance NTC despite its externally similar appearance to a classic low-resistance sender. The part number spans the Renix and Chrysler-EFI era Jeep XJ. It belongs on the 2.49kΩ position — do not confuse it with older CJ-era AMC gauge senders.

**ADC utilization with 2.49kΩ bias (5V reference, 12-bit ADC):**

| Condition | Sensor R | ADC counts |
|---|---|---|
| GM ECT, −20°C | ~9,420Ω | ~857 |
| GM ECT, 20°C | ~3,500Ω | ~1,690 |
| GM ECT, 80°C | ~470Ω | ~3,280 |
| GM ECT, 120°C | ~177Ω | ~3,820 |
| Mopar 56027012, 0°C | 7,800Ω | ~989 |
| Mopar 56027012, 80°C | ~370Ω | ~3,559 |
| Mopar 56027012, 120°C | 135Ω | ~3,879 |

---

## Quick Reference

| Sensor type | Resistance range | Use this bias |
|---|---|---|
| VDO SingleViu temperature | 10–330Ω (operating) | **100Ω** |
| VDO SingleViu pressure | 10–184Ω | **100Ω** |
| Smiths / Stewart Warner | 10–300Ω | **100Ω** |
| Pre-EFI Ford gauge senders | 10–78Ω | **100Ω** |
| Pre-EFI GM gauge senders (pre-1979) | 46–350Ω | **100Ω** |
| Jeep CJ / AMC (1972–1986) | 9–73Ω | **100Ω** |
| Mopar 56026779 Jeep oil pressure | 2–90Ω | **100Ω** |
| Marine senders | 10–300Ω | **100Ω** |
| GM EFI ECT (1979+) | 177–9,420Ω | **2.49kΩ** |
| Bosch NTC M12 | 100–9,000Ω | **2.49kΩ** |
| Mopar 56027012 / Jeep XJ temp | 135–7,800Ω | **2.49kΩ** |
| Jeep Renix CTS | 200–9,400Ω | **2.49kΩ** |
| Haltech CLT/IAT (GM-curve) | 177–9,420Ω | **2.49kΩ** |

---

## Platform Differences: 5V vs. 3.3V

The bias resistor values above (100Ω and 2.49kΩ) are correct for both 5V and 3.3V platforms **when VCC is used as the ADC reference voltage.** The voltage divider ratio is what matters for resistance calculation, not the absolute voltage.

If you are using an internal bandgap reference (1.1V on AVR, 1.2V on Teensy 3.x), the math still works but your ADC range is compressed — using VCC as reference is generally preferred for these sensors.

**3.3V platform notes:**

On 3.3V systems (Teensy 4.x, STM32, RP2040, ESP32 at 3.3V), the full ADC counts are reached at 3.3V. All sensors listed in this guide produce analog output voltages well below 3.3V with the recommended bias resistors, so there is no clipping risk. The 12-bit ADC resolution on most 3.3V platforms (4096 counts) more than compensates for the lower Vref.

The only case where you would use a different bias resistor on 3.3V vs. 5V is if you were previously using a very high bias value (2.2kΩ or higher) tuned to an internal bandgap reference — that approach is not recommended.

**Summary:**

| Platform | Ref voltage | Low-resistance bias | High-impedance bias |
|---|---|---|---|
| Arduino Mega (5V) | 5V | 100Ω | 2.49kΩ |
| Arduino Uno (5V) | 5V | 100Ω | 2.49kΩ |
| Teensy 4.x (3.3V) | 3.3V | 100Ω | 2.49kΩ |
| ESP32 (3.3V) | 3.3V | 100Ω | 2.49kΩ |
| STM32 (3.3V) | 3.3V | 100Ω | 2.49kΩ |

---

## Sensors Outside the Two-Family Model

A small number of sensors do not fit either family and require special handling:

**Ford EFI ECT (1983–1995 Fox body and some EEC-IV vehicles):** Resistance range is approximately 2,800–58,750Ω. This is much higher impedance than the GM NTC family. Optimal bias would be ~10kΩ–47kΩ. These sensors are not supported by the standard two-bias-position design and have been explicitly excluded from the validated sensor set. If you are using preOBD to monitor a Fox body Ford with its factory EFI ECT sensor, you will need a custom bias resistor and a custom calibration table.

**Linear voltage output sensors (MAP, MAF, wideband O2):** These produce a 0–5V analog output and do not use a bias resistor at all. They connect directly to the ADC input. See the linear sensor documentation.

---

## Hardware Wiring

```
VCC (5V or 3.3V)
    │
    Sensor wire from vehicle
    │
    ├─────► Analog input pin (ADC)
    │
   [R_bias]  ← install this resistor here
    │
   GND
```

**Resistor specifications:**
- Tolerance: 1% metal film strongly recommended (5% acceptable for low-resolution platforms)
- Power rating: 1/4W is sufficient
- Temperature stability: Metal film preferred over carbon composition

The bias resistor should be installed as close to the ADC pin as practical. Add a 100nF ceramic decoupling capacitor from the ADC pin to GND to filter ignition noise — classic vehicle electrical systems are very noisy.

**Important:** The value in `config.h` (`BIAS_RESISTOR_LOW` / `BIAS_RESISTOR_HIGH` or `DEFAULT_BIAS_RESISTOR` depending on your build) must exactly match the physical resistor you installed. A mismatch here causes systematic error in every reading.

---

## Why Not 1kΩ?

Earlier versions of this guide recommended 1kΩ as a default "compromise" value. This was based on early analysis of VDO sensors before the full sensor landscape was validated.

1kΩ was never actually optimal for anything:
- For low-resistance gauge senders (10–330Ω), it provides poor resolution — the ADC swing across the sensor's operating range is compressed significantly compared to 100Ω
- For high-impedance NTC sensors (GM, Jeep, Bosch), the cold-end compression is worse than 2.49kΩ and there is no compensating benefit at the hot end

1kΩ has no constituency in the validated sensor set. Do not use it.

## Why Not 2.2kΩ?

2.2kΩ was the original "industry standard" value — it predates digital data acquisition and was optimized for powering analog gauge movements, where current draw and cold-end behavior were the primary concerns. It is not optimized for ADC resolution.

For high-impedance NTC sensors, 2.2kΩ provides similar (slightly worse) resolution to 2.49kΩ and is less compatible with MegaSquirt calibration tables. For low-resistance gauge senders, 2.2kΩ is a poor fit and significantly wastes ADC range.

