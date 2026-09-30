/*
 * input_display.cpp - CLI presentation for the Input model.
 *
 * Moved verbatim from inputs/input_manager.cpp (#402 step 1). Zero
 * behaviour change intended.
 */

#include <math.h>  // isnan
#include <string.h>

#include "input_display.h"
#include "../inputs/input_manager.h"
#include "../pinctrl/analog_pins.h"
#include "../pinctrl/pin_label.h"
#include "../core/message_router.h"  // For msg.control
#include "../core/message_api.h"      // For msg
#include "../sensors/units_registry.h"
#include "../sensors/application_presets.h"
#include "../sensors/sensor_library.h"
#include "../storage/system_config.h"
#if INCLUDE_CAN
#include "../sensors/drivers/can/can_frame_cache.h"
#endif

// These are COLD paths: they run only when a CLI command asks for them, never
// in the sensor loop. On Teensy 4.x every function lands in ITCM (RAM1) by
// default, and ITCM is allocated in 32 KB blocks -- crossing a block boundary
// costs 32 KB of RAM1 that local variables can no longer use, not the few
// hundred bytes the code actually occupies. FLASHMEM keeps this module in
// flash where it belongs. Same macro json_config.cpp / json_registry.cpp /
// pin_label.cpp already use; a no-op off Teensy.
#ifndef FLASHMEM
#define FLASHMEM
#endif

// Helper to print pin name (A0, 1, I2C:0, CAN:0, etc)
FLASHMEM static void printPin(uint8_t pin) {
    if (pin >= 0xF0) {
        msg.control.print(F("I2C:"));
        msg.control.print(pin - 0xF0);
    } else if (pin >= 0xC0 && pin < 0xE0) {
        msg.control.print(F("CAN:"));
        msg.control.print(pin - 0xC0);
    } else {
        int8_t analogNum = analogIndexForPin(pin);
        if (analogNum >= 0) {
            msg.control.print(F("A"));
            msg.control.print(analogNum);
        } else {
            msg.control.print(pin);
        }
    }
}

FLASHMEM void printInputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Input Info ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  Enabled: "));
    msg.control.println(input->flags.isEnabled ? F("YES") : F("NO"));

    msg.control.print(F("  Application: "));
    msg.control.println(getApplicationNameByIndex(input->applicationIndex));

    msg.control.print(F("  Sensor: "));
    msg.control.println(getSensorNameByIndex(input->sensorIndex));

    msg.control.print(F("  Display Name: '"));
    msg.control.print(input->displayName);
    msg.control.println(F("'"));

    msg.control.print(F("  Short Name: '"));
    msg.control.print(input->abbrName);
    msg.control.println(F("'"));

    msg.control.print(F("  Units: "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.println(F("  Alarms: managed by Rules — see RULE LIST"));

    msg.control.print(F("  Output Mask: 0x"));
    if (input->outputMask < 0x10) msg.control.print('0');
    msg.control.println(input->outputMask, HEX);

    msg.control.print(F("  Current Value: "));
    if (isnan(input->value)) {
        msg.control.print(F("ERROR"));
    } else {
        msg.control.print(convertFromBaseUnits(input->value, input->unitsIndex), 2);
    }
    msg.control.print(F(" "));
    msg.control.println(getUnitStringByIndex(input->unitsIndex));

    msg.control.println();
    msg.control.println(F("To see alarm config:  INFO <pin> ALARM"));
    msg.control.println(F("To see calibration:   INFO <pin> CALIBRATION"));
    msg.control.println(F("To see output routing: INFO <pin> OUTPUT"));
    msg.control.println();
}

FLASHMEM void printInputOutputInfo(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Output Routing ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();

    msg.control.print(F("  CAN:      "));
    msg.control.println((input->outputMask & (1 << OUTPUT_CAN)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  RealDash: "));
    msg.control.println((input->outputMask & (1 << OUTPUT_REALDASH)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  Serial:   "));
    msg.control.println((input->outputMask & (1 << OUTPUT_SERIAL)) ? F("ENABLED") : F("DISABLED"));
    msg.control.print(F("  SD_Log:   "));
    msg.control.println((input->outputMask & (1 << OUTPUT_SD)) ? F("ENABLED") : F("DISABLED"));

    msg.control.println();
}

FLASHMEM void printInputCalibration(uint8_t pin) {
    Input* input = getInputByPin(pin);
    if (!input) {
        msg.control.print(F("ERROR: Input for pin "));
        printPin(pin);
        msg.control.println(F(" not found"));
        return;
    }

    msg.control.println();
    msg.control.print(F("===== Calibration ["));
    printPin(pin);
    msg.control.print(F("] ====="));
    msg.control.println();
    msg.control.print(F("  Type: "));
    if (!input->flags.useCustomCalibration) {
        msg.control.println(F("Preset (PROGMEM)"));
    } else if (input->calibrationType == CAL_THERMISTOR_STEINHART) {
        msg.control.println(F("Steinhart-Hart Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.steinhart.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.steinhart.steinhart_c, 10);
    } else if (input->calibrationType == CAL_THERMISTOR_BETA) {
        msg.control.println(F("Beta Custom"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.beta.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  Beta: "));
        msg.control.println(input->customCalibration.beta.beta, 1);
        msg.control.print(F("  R0: "));
        msg.control.print(input->customCalibration.beta.r0, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  T0: "));
        msg.control.print(input->customCalibration.beta.t0, 2);
        msg.control.println(F(" K"));
    } else if (input->calibrationType == CAL_LINEAR) {
        msg.control.println(F("Linear Custom"));
        msg.control.print(F("  Voltage Range: "));
        msg.control.print(input->customCalibration.pressureLinear.voltage_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.voltage_max, 2);
        msg.control.println(F(" V"));
        msg.control.print(F("  Output Range: "));
        msg.control.print(input->customCalibration.pressureLinear.output_min, 2);
        msg.control.print(F("-"));
        msg.control.print(input->customCalibration.pressureLinear.output_max, 2);
        msg.control.println();
    } else if (input->calibrationType == CAL_PRESSURE_POLYNOMIAL) {
        msg.control.println(F("Polynomial Custom (VDO)"));
        msg.control.print(F("  Bias Resistor: "));
        msg.control.print(input->customCalibration.pressurePolynomial.bias_resistor, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  A: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_a, 10);
        msg.control.print(F("  B: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_b, 10);
        msg.control.print(F("  C: "));
        msg.control.println(input->customCalibration.pressurePolynomial.poly_c, 10);
    } else if (input->calibrationType == CAL_RPM) {
        msg.control.println(F("RPM Custom"));
        msg.control.print(F("  Poles: "));
        msg.control.println(input->customCalibration.rpm.poles);
        msg.control.print(F("  Pulley Ratio: "));
        msg.control.println(input->customCalibration.rpm.pulley_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.rpm.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.rpm.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  RPM Range: "));
        msg.control.print(input->customCalibration.rpm.min_rpm);
        msg.control.print(F("-"));
        msg.control.println(input->customCalibration.rpm.max_rpm);
    } else if (input->calibrationType == CAL_SPEED) {
        msg.control.println(F("Speed Custom"));
        msg.control.print(F("  Pulses/Rev: "));
        msg.control.println(input->customCalibration.speed.pulses_per_rev);
        msg.control.print(F("  Tire Circumference: "));
        msg.control.print(input->customCalibration.speed.tire_circumference_mm);
        msg.control.println(F(" mm"));
        msg.control.print(F("  Drive Ratio: "));
        msg.control.println(input->customCalibration.speed.final_drive_ratio, 2);
        msg.control.print(F("  Calibration Mult: "));
        msg.control.println(input->customCalibration.speed.calibration_mult, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(input->customCalibration.speed.timeout_ms);
        msg.control.println(F(" ms"));
        msg.control.print(F("  Max Speed: "));
        msg.control.print(input->customCalibration.speed.max_speed_kph);
        msg.control.println(F(" km/h"));
    } else if (input->calibrationType == CAL_VOLTAGE_DIVIDER) {
        msg.control.println(F("Voltage Divider Custom"));
        msg.control.print(F("  R1: "));
        msg.control.print(input->customCalibration.voltageDivider.r1, 1);
        msg.control.print(F(" Ω  R2: "));
        msg.control.print(input->customCalibration.voltageDivider.r2, 1);
        msg.control.println(F(" Ω"));
        msg.control.print(F("  Correction: "));
        msg.control.print(input->customCalibration.voltageDivider.correction, 3);
        msg.control.print(F("  Offset: "));
        msg.control.print(input->customCalibration.voltageDivider.offset, 3);
        msg.control.println(F(" V"));
    } else if (input->calibrationType == CAL_CAN_IMPORT) {
        CANSensorCalibration cal;
        if (input->flags.useCustomCalibration) {
            memcpy(&cal, &input->customCalibration.can, sizeof(cal));
        } else {
            memcpy_P(&cal, input->presetCalibration, sizeof(cal));
        }
        uint16_t resolved_timeout = cal.timeout_ms;
        if (resolved_timeout < 100 || resolved_timeout > 30000) resolved_timeout = CAN_DEFAULT_TIMEOUT_MS;

        msg.control.println(input->flags.useCustomCalibration ? F("CAN Import Custom") : F("CAN Import Preset"));
        msg.control.print(F("  CAN ID: 0x"));
        msg.control.println(cal.source_can_id, HEX);
        msg.control.print(F("  PID: 0x"));
        if (cal.source_pid < 0x10) msg.control.print('0');
        msg.control.println(cal.source_pid, HEX);
        msg.control.print(F("  Offset/Length: "));
        msg.control.print(cal.data_offset);
        msg.control.print(F("/"));
        msg.control.print(cal.data_length);
        msg.control.println(cal.is_big_endian ? F(" bytes (big-endian)") : F(" bytes (little-endian)"));
        msg.control.print(F("  Scale: "));
        msg.control.println(cal.scale_factor, 4);
        msg.control.print(F("  Offset: "));
        msg.control.println(cal.offset, 4);
        msg.control.print(F("  Timeout: "));
        msg.control.print(resolved_timeout);
        msg.control.println(F(" ms"));
#if INCLUDE_CAN
        // Why a CAN input reads NaN is usually "nothing answered" (#476)
        msg.control.print(F("  Last frame: "));
        CANFrameEntry* entry = getCANCacheEntry(cal.source_can_id, cal.source_pid);
        if (entry && entry->valid) {
            msg.control.print((unsigned long)(millis() - entry->timestamp_ms));
            msg.control.println(F(" ms ago"));
        } else {
            msg.control.println(F("never"));
        }
#endif
    }
    msg.control.println();
}

FLASHMEM void listAllInputs() {
    msg.control.println(F("Active Inputs:"));
    bool found = false;

    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].pin != 0xFF && inputs[i].flags.isEnabled) {
            found = true;
            char pinLabel[8];
            formatPinLabel(inputs[i].pin, pinLabel, sizeof(pinLabel));
            msg.control.print(F("  "));
            msg.control.print(pinLabel);
            msg.control.print(F(": "));
            msg.control.print(inputs[i].abbrName);
            msg.control.print(F(" ("));
            msg.control.print(inputs[i].displayName);
            msg.control.print(F(") = "));
            if (isnan(inputs[i].value)) {
                msg.control.print(F("ERROR"));
            } else {
                msg.control.print(convertFromBaseUnits(inputs[i].value, inputs[i].unitsIndex));
            }
            msg.control.print(F(" "));
            msg.control.print((__FlashStringHelper*)getUnitStringByIndex(inputs[i].unitsIndex));
            if (inputs[i].obd2pid != 0) {
                msg.control.print(F(" [OBD PID 0x"));
                if (inputs[i].obd2pid < 0x10) msg.control.print('0');
                msg.control.print(inputs[i].obd2pid, HEX);
                msg.control.print(F(" len "));
                msg.control.print(inputs[i].obd2length);
                msg.control.print(F("]"));
            }
            msg.control.println();
        }
    }

    if (!found) {
        msg.control.println(F("  (none)"));
    }
}
