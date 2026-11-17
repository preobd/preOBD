/*
 * can_layout.h - The CAN layout engine: signal rows, the input map, frame
 * encoding and pass pacing (#612 phase 3, plan B1/B3/B4/B5)
 *
 * Pure: no globals, no CAN driver, no Arduino I/O. can_layout_output.cpp wires
 * it to the live input table and the bus manager; test_can_layout drives it
 * directly, and renders the PREOBD table to the published DBC.
 *
 * A layout is a PROGMEM table of CanLayoutSignal rows: which application (and
 * which instance of it) goes in which bytes of which frame, and how it is
 * scaled. Layouts encode from the input's BASE unit (degC, bar, V, rpm, km/h,
 * %, m); `unitsIndex` converts to the unit the signal carries (pressures go
 * out in kPa), resolved to a registry index once, in the row.
 *
 * No data: all-ones in the field (fieldAllOnes), when the signal is not mapped
 * to an input, the input is disabled, has no reading yet, is not routed to
 * this output, is not finite, or measures a different quantity than the row.
 * Layouts never look at rules state (FAULT / STALE): that is not an output's
 * business. A real value clamps to max-1 so it can never read as no data.
 */

#ifndef CAN_LAYOUT_H
#define CAN_LAYOUT_H

#include <stdint.h>
#include <stddef.h>
#include "field_encode.h"

struct Input;

#define LAYOUT_SIGNED         FIELD_SIGNED
#define LAYOUT_LITTLE_ENDIAN  FIELD_LITTLE_ENDIAN

#define LAYOUT_UNITS_NONE     0xFF   // no conversion from the base unit

// One signal. 24 bytes, PROGMEM. The array is append-only after release.
struct CanLayoutSignal {
    uint8_t  frame;            // CAN ID = base + frame
    uint8_t  app;              // APP_* (registry_enums.h), never a raw number
    uint8_t  instance;         // 1-based: the n-th configured input of that application, in pin order
    uint8_t  startByte;        // 0..7
    uint8_t  length;           // 1 or 2 (4 works too)
    uint8_t  flags;            // LAYOUT_*
    uint8_t  measurementType;  // MeasurementType: the input must measure exactly this
    uint8_t  unitsIndex;       // UNITS_REGISTRY index the value is carried in, or LAYOUT_UNITS_NONE
    float    scale, offset;    // physical = raw * scale + offset
    float    rangeMin, rangeMax;   // the physical range the signal promises to carry
};

// A layout: its rows, with the frame count and base ID
struct CanLayout {
    const CanLayoutSignal* signals;   // PROGMEM
    uint8_t  count;
    uint8_t  frames;
    uint16_t baseId;
};

#define CAN_LAYOUT_MAX_SIGNALS 40
#define LAYOUT_NO_INPUT 0xFF   // a map entry with no input

// The PREOBD layout (layout_preobd.cpp)
extern const CanLayout PREOBD_LAYOUT;

// Row i, copied out of PROGMEM
void canLayoutSignalAt(const CanLayout& layout, uint8_t i, CanLayoutSignal& out);

// ---- The map (B3) ----

/**
 * map[i] = the slot in inputs[0..nInputs) that signal i reads, or
 * LAYOUT_NO_INPUT. Instance n of an application is the n-th CONFIGURED input
 * (pin != 0xFF) with that application, in PIN order: stable across slot
 * reuse and save compaction. A disabled input keeps its instance (it sends no
 * data); nothing shifts. map holds layout.count entries.
 */
void canLayoutBuildMap(const CanLayout& layout, const Input* inputs, uint8_t nInputs, uint8_t* map);

// The map with its freshness: marked stale by a configuration change (the hook
// never rebuilds, it may run mid-command), rebuilt by the next canLayoutMapFresh().
struct CanLayoutMap {
    uint8_t slot[CAN_LAYOUT_MAX_SIGNALS];
    bool    stale;
};
void canLayoutMapMarkStale(CanLayoutMap& m);
/** Rebuild if stale (or never built); true when it was rebuilt. */
bool canLayoutMapFresh(CanLayoutMap& m, const CanLayout& layout, const Input* inputs, uint8_t nInputs);

// ---- Frame encoding (B1/B4) ----

/** What a signal sends for one input; false: no data (the caller writes all-ones). */
bool canLayoutEncodeSignal(const CanLayoutSignal& sig, const Input& in, uint8_t routeBit, uint8_t* out);

/**
 * The 8 bytes of frame `frame`: each signal in it from the input its map entry
 * names, all-ones where there is no data; bytes no signal covers are 0.
 * routeBit: the output's route bit (1 << id); an input not routed to it sends no data.
 */
void canLayoutBuildFrame(const CanLayout& layout, const uint8_t* map, const Input* inputs,
                         uint8_t nInputs, uint8_t routeBit, uint8_t frame, uint8_t* out8);

// ---- Pacing and the TX path (B5) ----

// Sends one frame; false: the bus took none now (queue full or bus down)
typedef bool (*LayoutWriteFn)(void* ctx, uint8_t bus, uint32_t id, const uint8_t* data);
// Snapshots frame `frame` into 8 bytes (at the start of a pass)
typedef void (*LayoutBuildFn)(void* ctx, uint8_t frame, uint8_t* out8);
// Abort the bus's unACKed transmits; false: refused (another consumer's exchange is in flight)
typedef bool (*LayoutAbortFn)(void* ctx, uint8_t bus);

#define LAYOUT_MAX_FRAMES 16

// One output's pass state. A pass is every frame of the layout, snapshotted
// when it starts, drained as far as the bus accepts and resumed on the next tick.
struct LayoutPass {
    uint8_t  buf[LAYOUT_MAX_FRAMES][8];
    uint8_t  frames;      // frames in a pass
    uint8_t  next;        // the next frame to send
    bool     pending;     // a pass is unsent (next < frames)
    bool     primed;      // lastSend is valid
    uint32_t lastSend;    // when the last pass was due (advances by exactly one interval)
    uint32_t baseId;
    uint16_t skipped;     // passes not started because the last was still leaving
    uint16_t unacked;     // stalled passes dropped (nobody ACKing, queue full)
    uint32_t passes;      // passes started
};

struct LayoutIO {
    LayoutWriteFn write;
    LayoutBuildFn build;
    LayoutAbortFn abort;
    void* ctx;
};

void layoutPassInit(LayoutPass& p, uint8_t frames, uint32_t baseId);
/** The bus restarted: whatever was in flight is gone. Counters stay. */
void layoutPassReset(LayoutPass& p);

/**
 * Pace one output. Due when a full interval has passed since the last due
 * time; lastSend then advances by exactly one interval (no drift), resyncing
 * to `now` when more than one interval behind (no catch-up burst). A pass is
 * snapshotted if the last one finished; if it is still unsent, the bus's
 * unACKed transmits are aborted and a fresh pass starts (unacked++), unless
 * the abort is refused, in which case the old pass keeps draining (skipped++).
 */
void layoutPassPace(LayoutPass& p, uint32_t now, uint16_t intervalMs, uint8_t bus, const LayoutIO& io);

/** Send frames until the bus refuses one or the pass is done. True when the pass finished. */
bool layoutPassDrain(LayoutPass& p, uint8_t bus, const LayoutIO& io);

// A set of outputs ticking together
struct LayoutPort {
    LayoutPass pass;
    uint16_t   interval;
    uint8_t    bus;
    bool       live;
};

/**
 * Pace every live port, then drain ONE port per bus: ports on a bus take turns
 * (rr rotates; pass the same variable every call). The result is a count of
 * drains performed.
 */
uint8_t layoutTick(LayoutPort* ports, uint8_t n, uint32_t now, const LayoutIO* ios, uint8_t* rr);

// Frames per second and bus load of a layout at `intervalMs`, for OUTPUT <id>:
// the load in percent x10 at `baud`, a classic 11-bit data frame being ~111 bits
// with worst-case stuffing and interframe space.
uint16_t canLayoutBusLoadX10(uint8_t frames, uint16_t intervalMs, uint32_t baud);

#endif // CAN_LAYOUT_H
