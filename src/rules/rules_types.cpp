/*
 * rules_types.cpp - Compile-time validation for the control engine records.
 *
 * No runtime logic yet — this TU exists so rules_types.h is compiled into
 * every build and its layout invariants are enforced. The evaluation engine,
 * EEPROM wiring, and CLI land in later commits.
 */

#include "rules_types.h"

// Size guards. Upper bounds (not exact equality) so they hold across AVR's
// 1-byte alignment and ARM's 4-byte float alignment. These cap the per-record
// cost in the shared config-store arena (ARCHITECTURE §11.10): packed inputs +
// rules + actions must fit the EEPROM budget on the 1080-byte Teensy 4.x.
static_assert(sizeof(Rule)   <= 24, "Rule grew past its EEPROM record budget — see ARCHITECTURE §11.10");
static_assert(sizeof(Action) <= 24, "Action grew past its EEPROM record budget — see ARCHITECTURE §11.10");

// The reserved sentinel must stay out of the valid id range implied by the caps.
static_assert(MAX_RULES   < RULES_ID_NONE, "MAX_RULES must leave RULES_ID_NONE reserved");
static_assert(MAX_ACTIONS < RULES_ID_NONE, "MAX_ACTIONS must leave RULES_ID_NONE reserved");
