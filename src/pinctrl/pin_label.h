/*
 * pin_label.h - Pin-encoding decoder: physical pin number -> display label.
 *
 * Pure pin-encoding decode (I2C virtual pins >= 0xF0, CAN virtual pins
 * 0xC0-0xDF, analog via analogIndexForPin(), else the raw number). Reads no
 * inputs[] state — see CLAUDE.md's #402 notes for why this lives in
 * pinctrl/ rather than cli/: it has callers outside the CLI
 * (storage/json_config.cpp, cli/cmd_rule.cpp), and putting it under cli/
 * would create a new storage -> cli edge that tools/check_layering.py
 * would reject.
 */

#ifndef PIN_LABEL_H
#define PIN_LABEL_H

#include <stdint.h>
#include <stddef.h>

// Format a pin's display label ("A0", "I2C:0", "CAN:3", or the raw number)
// into buf. Single source for LIST INPUTS and the JSON dump's "pinLabel".
void formatPinLabel(uint8_t pin, char* buf, size_t bufLen);

#endif // PIN_LABEL_H
