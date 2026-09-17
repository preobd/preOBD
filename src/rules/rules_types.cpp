/*
 * rules_types.cpp - Compile-time validation for the control engine records.
 *
 * No runtime logic yet — this TU exists so rules_types.h is compiled into
 * every build and its layout invariants are enforced. The evaluation engine,
 * EEPROM wiring, and CLI land in later commits.
 */

#include "rules_types.h"

// Size guards. Exact per-platform sizes (not upper bounds) so a field addition
// is caught immediately rather than silently eating budget headroom: AVR's
// 1-byte alignment packs Rule to 29 B; ARM's 4-byte float alignment pads it to
// 32 B (5 floats + 2 u16 + 5 u8 = 29, rounded up to a 4-byte multiple). These
// cap the per-record cost in the shared config-store arena (ARCHITECTURE
// §11.10): packed inputs + rules + actions must fit the EEPROM budget on the
// 1080-byte Teensy 4.x. Rule grew 24 -> 32 B (ARM) in #425 (warnLo/warnHi);
// EEPROM_VERSION bumped accordingly (version.h).
#ifdef __AVR__
static_assert(sizeof(Rule) == 29, "Rule size changed on AVR — update this guard and EEPROM_VERSION (see ARCHITECTURE §11.10)");
#else
static_assert(sizeof(Rule) == 32, "Rule size changed on 32-bit platforms — update this guard and EEPROM_VERSION (see ARCHITECTURE §11.10)");
#endif
static_assert(sizeof(Action) <= 24, "Action grew past its EEPROM record budget — see ARCHITECTURE §11.10");

// The reserved sentinel must stay out of the valid id range implied by the caps.
static_assert(MAX_RULES   < RULES_ID_NONE, "MAX_RULES must leave RULES_ID_NONE reserved");
static_assert(MAX_ACTIONS < RULES_ID_NONE, "MAX_ACTIONS must leave RULES_ID_NONE reserved");
