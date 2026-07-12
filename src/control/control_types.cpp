/*
 * control_types.cpp - Compile-time validation for the control engine records.
 *
 * No runtime logic yet — this TU exists so control_types.h is compiled into
 * every build and its layout invariants are enforced. The evaluation engine,
 * EEPROM wiring, and CLI land in later commits.
 */

#include "control_types.h"

// Size guards. Upper bounds (not exact equality) so they hold across AVR's
// 1-byte alignment and ARM's 4-byte float alignment. These cap the per-record
// EEPROM cost that feeds the SystemConfig budget (CONTROL_ENGINE_PLAN §1.1):
//   rules[MAX_RULES] + actions[MAX_ACTIONS] must stay affordable on the
//   1080-byte Teensy 4.x EEPROM. With 24/6 B records at 8/8 that's ~240 B.
static_assert(sizeof(Rule)   <= 24, "Rule grew past its EEPROM budget — re-check CONTROL_ENGINE_PLAN §1.1");
static_assert(sizeof(Action) <=  8, "Action grew past its EEPROM budget — re-check CONTROL_ENGINE_PLAN §1.1");

// The reserved sentinel must stay out of the valid id range implied by the caps.
static_assert(MAX_RULES   < CONTROL_ID_NONE, "MAX_RULES must leave CONTROL_ID_NONE reserved");
static_assert(MAX_ACTIONS < CONTROL_ID_NONE, "MAX_ACTIONS must leave CONTROL_ID_NONE reserved");
