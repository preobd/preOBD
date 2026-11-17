/*
 * can_layout_output.h - The layout outputs: PREOBD (our own CAN frames for
 * gauges, #612 phase 3) and HALTECH (Haltech's ECU broadcast, phase 4)
 *
 * A layout output pushes fixed frames on its CAN bus every interval. It is a
 * CAN sender like OBD, so the bus manager refuses it on LISTEN and on the bus
 * CAN input reads (one job per bus, #632). Every layout output on a bus shares
 * the bus manager's one CANC_LAYOUT bit.
 *
 * This header holds the fixed ID block and the lifecycle the output manager
 * calls; the signals, the map and the pacing are can_layout.h, the tables are
 * layout_preobd.cpp and layout_haltech.cpp.
 */

#ifndef CAN_LAYOUT_OUTPUT_H
#define CAN_LAYOUT_OUTPUT_H

#include <stdint.h>

// The IDs each layout format sends (PREOBD_BASE_ID, HALTECH_FRAME_IDS ...).
// outputCanIdsOverlap() (output_admission.h) refuses a second owner on a bus.
#include "can_layout_ids.h"

// Attach layout output <id> to its record's CAN bus. True when attached (also
// while the bus waits to start), false when the bus refused it.
bool canLayoutStart(uint8_t id);
// Detach it; the bus bit clears with the last layout output.
void canLayoutStop(uint8_t id);
// Once per loop from updateOutputs(): pace every live layout output and drain
// one per bus.
void canLayoutTick();
// The input map is out of date (an input was added, removed, re-typed or
// re-routed): rebuilt on the next tick, never inside the hook. Set at the same
// places as obdQuery_markStale().
void canLayoutMarkStale();
// The detail lines of OUTPUT <id> for a layout output: IDs, frames, the map,
// bus load, skipped / unacked passes.
void canLayoutPrintDetail(uint8_t id);

#endif // CAN_LAYOUT_OUTPUT_H
