/*
 * input_store.h - Input/Rule/Action EEPROM persistence (shared config-store arena)
 *
 * The shared front-growing config-store arena at EEPROM address 0 — one
 * ConfigStoreHeader followed by the Input, Rule, Action and ActionLink
 * sections (ARCHITECTURE.md §11.10). This is the storage-format half of the
 * split called for by #402: the serialisation, the checksum, the cap/
 * truncation policy and the stored-record probes #385/#358 added, split out
 * of src/inputs/input_manager.cpp (which keeps the Input model itself —
 * lifecycle, lookup, mutators, runtime).
 */

#ifndef INPUT_STORE_H
#define INPUT_STORE_H

#include "../config.h"
#include "../core/platform.h"
#include <Arduino.h>
#include "../inputs/input.h"

// ===== PERSISTENCE =====
bool saveInputConfig();               // Save all inputs (+ rules/actions/links) to EEPROM
bool loadInputConfig();               // Load all inputs (+ rules/actions/links) from EEPROM
void resetInputConfig();              // Clear all inputs and EEPROM

// ===== BUS REQUIREMENTS (see storage/bus_needs.h) =====
// The inputs layer's two contributions to bus ownership, both consumed by the
// bus layer without it ever including an inputs header:
//
//   storedInputBusNeeds() — a read-only pre-scan of the stored records, run at
//     boot BEFORE loadInputConfig() and initLCD() so the buses those records
//     need are up (and their pins reserved) before anything is validated (#358).
//   inputHoldsPin() — registered via setPinOwnerInputProbe() so every pin
//     claim can see configured inputs, which deliberately never enter the pin
//     registry (mega2560 has no RAM for them). Before initInputManager() makes
//     inputs[] live, this answers from the stored EEPROM records instead
//     (§12.2 source 2) — same records, so the answer never changes across the
//     boundary, only its speed (#385).
uint8_t storedInputBusNeeds();
bool inputHoldsPin(uint8_t pin);

// Self-ownership exclusion for the live-table half of inputHoldsPin() — lets a
// pin be re-claimed by the very Input already holding it, by pointer identity
// (a pin-keyed exclusion can't tell "it's mine" from "mine and a squatter's").
// validateInputConfig() (input_manager.cpp) sets this immediately before
// calling pinsConflict() and clears it (pass nullptr) immediately after, so
// the exclusion never leaks into an unrelated call. A setter — rather than a
// bare extern — so the invariant stays documented at the TU boundary this
// state now crosses (#402 step 2).
void inputStoreSetProbeExclusion(const Input* self);

#endif // INPUT_STORE_H
