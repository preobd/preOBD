/*
 * input_display.h - CLI presentation for the Input model.
 *
 * Printers and listers that read inputs[] and format it for the control
 * plane. Moved out of inputs/input_manager.cpp (#402 step 1) — this is CLI
 * output, not domain-model behaviour; the Input model should not carry it.
 */

#ifndef INPUT_DISPLAY_H
#define INPUT_DISPLAY_H

#include <stdint.h>

void printInputInfo(uint8_t pin);    // Print detailed input information
// printInputAlarmInfo() lives in cmd_rule.cpp (declared in command_handlers.h):
// the per-input alarm view renders the input's canonical alarm Rule, not the
// retired legacy Input alarm fields (#338).
void printInputOutputInfo(uint8_t pin);
void printInputCalibration(uint8_t pin);
void listAllInputs();                // List all active inputs

#endif // INPUT_DISPLAY_H
