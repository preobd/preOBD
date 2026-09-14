/*
 * bus_wiring.h - Build-time bus WIRING declarations (#377)
 *
 * Buses are the one peripheral class that used to skip the WIRED layer the
 * repo already documents for LED / mode button / SD (boards/resolve.h,
 * boards/teensy40/board.h): those jump straight from a pin/capability being
 * PRESENT to the feature being in the image. Bus INSTANCES had no equivalent —
 * "which I2C/SPI/CAN instance is physically wired on THIS unit" was never a
 * build-time fact, so something downstream had to guess (DEFAULT_I2C_BUS /
 * DEFAULT_SPI_BUS / DEFAULT_CAN_BUS). That guess is the bug (#377): it is what
 * silently claimed Wire (18/19 on Teensy 4.x) out from under configured
 * inputs, and what made a stock teensy40 claim CAN1's 22/23 for a transceiver
 * that might not be attached.
 *
 * This header fills the gap:
 *
 *   -D I2C_WIRED_BUS=<n>   ; Wire / Wire1 / Wire2
 *   -D SPI_WIRED_BUS=<n>   ; SPI / SPI1 / SPI2
 *   -D CAN_WIRED_BUS=<n>   ; CAN1 / CAN2 / CAN3
 *
 * Legal in boards/<board>/wiring.ini (per-unit, gitignored) AND in
 * boards/<board>/board.h (a purpose-built PCB where the bus is a board fact,
 * not a unit fact) — wiring.ini wins because it is -D'd on the command line
 * and board.h's definitions below are #ifndef-guarded.
 *
 * Naming the INSTANCE, not a boolean, is the point: Teensy 4.x has three of
 * each, and "don't guess which one" is the whole objection this closes.
 *
 * Semantics — one rule for all three buses:
 *   present -> "this bus instance is physically wired on my unit; bring it up
 *               without asking."
 *   absent  -> never guess. No auto bring-up. Tell the user what to run.
 *   A declaration GRANTS PERMISSION; it NEVER VETOES. An explicit
 *   `BUS I2C <n>` selection always wins, and declaring bus 1 wired does not
 *   forbid selecting bus 0 — so there is no precedence to arbitrate and
 *   experimentation is never locked out. See resolveWiredBusId() below.
 *
 * Range-checked HERE, not in boards/resolve.h: NUM_I2C_BUSES / NUM_SPI_BUSES /
 * NUM_CAN_BUSES are defined in bus_defaults.h, which resolve.h cannot see (it
 * is included from board.h, long before bus_defaults.h exists in the
 * translation unit). A declared CAN_WIRED_BUS on a board with NUM_CAN_BUSES==0
 * (e.g. mega2560) is a compile error here, not a silent no-op.
 */

#ifndef BUS_WIRING_H
#define BUS_WIRING_H

#include <stdint.h>
#include "../hal/bus_defaults.h"   // NUM_I2C_BUSES / NUM_SPI_BUSES / NUM_CAN_BUSES

// ---- I2C ----
#ifndef I2C_WIRED_BUS
#  define BUS_WIRED_I2C_ID 0xFF
#else
   // No (uint8_t) cast here: this macro is also used in #if elsewhere in the
   // tree (BUS_WIRED_I2C_ID != 0xFF), and the preprocessor cannot evaluate a
   // C-style cast — it would fail with "missing binary operator" instead of
   // the static_assert message below. Plain integer; every consumer takes it
   // as a uint8_t parameter or array index, where the narrowing is implicit
   // and safe (I2C_WIRED_BUS is range-checked to fit right here).
#  define BUS_WIRED_I2C_ID (I2C_WIRED_BUS)
   static_assert(I2C_WIRED_BUS < NUM_I2C_BUSES,
       "I2C_WIRED_BUS names a bus this platform does not have");
#endif

// ---- SPI ----
#ifndef SPI_WIRED_BUS
#  define BUS_WIRED_SPI_ID 0xFF
#else
#  define BUS_WIRED_SPI_ID (SPI_WIRED_BUS)
   static_assert(SPI_WIRED_BUS < NUM_SPI_BUSES,
       "SPI_WIRED_BUS names a bus this platform does not have");
#endif

// ---- CAN ----
#ifndef CAN_WIRED_BUS
#  define BUS_WIRED_CAN_ID 0xFF
#else
#  define BUS_WIRED_CAN_ID (CAN_WIRED_BUS)
   static_assert(CAN_WIRED_BUS < NUM_CAN_BUSES,
       "CAN_WIRED_BUS names a bus this platform does not have");
#endif

/**
 * The one decision, shared by all three buses (and unit-tested in isolation —
 * see test/test_bus_wiring): an explicit stored/CLI SELECTION always wins; a
 * build-time WIRING declaration is consulted only when there is none; absent
 * both, the answer is 0xFF ("refuse — do not guess").
 *
 * `configuredBusId` is whatever the runtime CONFIG says (systemConfig.buses.*,
 * 0xFF meaning "no explicit choice"). `declaredBusId` is one of the
 * BUS_WIRED_*_ID constants above. Returning `declaredBusId` unexamined when
 * `configuredBusId` is 0xFF is what makes a declaration a PERMISSION, never a
 * VETO: it hands back 0xFF right back out when nothing has been declared
 * either, and it never overrides an explicit configured choice.
 */
inline uint8_t resolveWiredBusId(uint8_t configuredBusId, uint8_t declaredBusId) {
    return (configuredBusId != 0xFF) ? configuredBusId : declaredBusId;
}

/**
 * Should registerSystemPins() (system_config.cpp, #379 Tier 1) reserve THIS
 * wired bus's pins as a build-time physical fact, before serial/CLI can claim
 * over them? Only when the declaration is the bus actually in effect — i.e.
 * exactly the case where resolveWiredBusId() above would return
 * `declaredBusId` rather than `configuredBusId`.
 *
 * Kept as a tiny pure predicate, separate from resolveWiredBusId() itself,
 * so it is unit-testable without pulling system_config.cpp's dependency
 * chain onto the host: registerSystemPins() calls this instead of repeating
 * the `configuredBusId == 0xFF && declaredBusId != 0xFF` condition inline.
 */
inline bool wiredBusIsInEffect(uint8_t configuredBusId, uint8_t declaredBusId) {
    return configuredBusId == 0xFF && declaredBusId != 0xFF;
}

#endif // BUS_WIRING_H
