/*
 * can_layout_output.cpp - The layout outputs (PREOBD, HALTECH) on the live
 * system (see can_layout_output.h): lifecycle, the input map, pacing and the bus.
 *
 * The pure parts (map, frame encoding, pass pacing) are can_layout.{h,cpp};
 * this file feeds them the input table and the bus manager. FlexCAN headers
 * stay out: can_bus.h is the only CAN include.
 */

#include "../../config.h"

#if INCLUDE_CAN_LAYOUTS

#include "can_layout_output.h"
#include "can_layout.h"
#include "../table/output_table.h"
#include "../../buses/can_bus.h"
#include "../../hal/bus_defaults.h"   // NUM_CAN_BUSES: a layout port per bus
#include "../../inputs/input.h"
#include "../../inputs/pressure_reference.h"   // baroReferenceBar: absolute fields (§14.8)
#include "../../inputs/input_manager.h"
#include "../../sensors/application_presets.h"
#include "../../buses/pinctrl/pin_label.h"
#include "../../core/message_api.h"
#include "../../core/log_tags.h"

#ifndef FLASHMEM   // Teensy-only attribute: cold code in flash, not RAM1
#define FLASHMEM
#endif

// A layout's IDs are fixed, so admission allows one output of each layout per
// CAN bus: a port per bus per layout
#define LAYOUT_FORMATS 2   // PREOBD, HALTECH
#define LAYOUT_PORTS (NUM_CAN_BUSES * LAYOUT_FORMATS)

struct LayoutSlot {
    uint8_t outputId;          // 0xFF: free
    const CanLayout* layout;   // the format's table
    float   baroBar;           // the barometric reference, read once per pass
    CanLayoutMap map;          // signal -> inputs[] slot
};

// The table a layout format sends; nullptr for any other format
static const CanLayout* layoutOf(uint8_t format) {
    if (format == FMT_PREOBD) return &PREOBD_LAYOUT;
    if (format == FMT_HALTECH) return &HALTECH_LAYOUT;
    return nullptr;
}

static LayoutPort port[LAYOUT_PORTS];
static LayoutIO   portIo[LAYOUT_PORTS];
static LayoutSlot slot[LAYOUT_PORTS];
static bool       slotsInit = false;
static bool       anyStale = false;
static uint8_t    roundRobin = 0;

static void initSlots() {
    if (slotsInit) return;
    slotsInit = true;
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) slot[i].outputId = 0xFF;
}

static uint8_t slotOf(uint8_t id) {
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) if (slot[i].outputId == id) return i;
    return 0xFF;
}

// ---- Bus side of the pass IO ----

static bool ioWrite(void*, uint8_t bus, uint32_t id, const uint8_t* data) {
    return canBusWrite(bus, id, data, 8, false);
}

static bool ioAbort(void*, uint8_t bus) {
    return canBusAbortUnackedTx(bus, CANC_LAYOUT);
}

static bool ioBuild(void* ctx, uint8_t frame, uint8_t* out8) {
    const uint8_t s = (uint8_t)(uintptr_t)ctx;
    if (frame == 0) slot[s].baroBar = baroReferenceBar();   // one reading for the whole pass
    return canLayoutBuildFrame(*slot[s].layout, slot[s].map.slot, inputs, MAX_INPUTS,
                               (uint8_t)(1u << slot[s].outputId), frame, out8, slot[s].baroBar);
}

// The bus restarted: whatever was queued is gone, start a pass afresh
static void onLayoutReset() {
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) {
        if (port[i].live) layoutPassReset(port[i].pass);
    }
}

// The map is rebuilt lazily, on the next tick after a mark: the hook can run
// in the middle of a command that is still changing inputs
static void rebuildMaps() {
    anyStale = false;
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) {
        if (slot[i].outputId != 0xFF) canLayoutMapFresh(slot[i].map, *slot[i].layout, inputs, MAX_INPUTS);
    }
}

FLASHMEM void canLayoutMarkStale() {
    anyStale = true;
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) canLayoutMapMarkStale(slot[i].map);
}

FLASHMEM bool canLayoutStart(uint8_t id) {
    if (id >= MAX_OUTPUTS) return false;
    initSlots();
    canLayoutStop(id);
    const CanLayout* layout = layoutOf(outputFormatOf(id));
    if (layout == nullptr) return false;
    uint8_t s = 0xFF;
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) if (slot[i].outputId == 0xFF) { s = i; break; }
    if (s == 0xFF) return false;
    const uint8_t bus = outputTransportCanBus(outputTransportOf(id));
    if (bus == 0xFF) return false;
    canSetHandlers(CANC_LAYOUT, nullptr, onLayoutReset, nullptr);
    const CanAttach r = canAttach(bus, CANC_LAYOUT);
    if (r == CAN_ATTACH_BUS_OFF || r == CAN_ATTACH_NO_BUS || r == CAN_ATTACH_LISTEN_ONLY ||
        r == CAN_ATTACH_INPUT_BUS) {
        msg.debug.error(TAG_CAN, "layout output: CAN%u is off, listen-only or CAN input's", (unsigned)bus + 1);
        return false;
    }
    slot[s].outputId = id;   // attached, also while the bus waits
    slot[s].layout = layout;
    slot[s].baroBar = STD_BARO_BAR;
    port[s].live = true;
    port[s].bus = bus;
    const OutputRecord* rec = outputRecord(id);
    port[s].interval = rec ? rec->interval : 100;
    layoutPassInit(port[s].pass, layout->frames, layout->baseId, layout->frameIds);
    portIo[s] = { ioWrite, ioBuild, ioAbort, (void*)(uintptr_t)s };
    canLayoutMapMarkStale(slot[s].map);
    canLayoutMapFresh(slot[s].map, *layout, inputs, MAX_INPUTS);
    anyStale = true;   // inputs may still be loading at boot: look again on the first tick
    canLayoutMapMarkStale(slot[s].map);
    return true;
}

FLASHMEM void canLayoutStop(uint8_t id) {
    initSlots();
    const uint8_t s = slotOf(id);
    if (s == 0xFF) return;
    canDetach(port[s].bus, CANC_LAYOUT);
    port[s].live = false;
    slot[s].outputId = 0xFF;
}

void canLayoutTick() {
    uint8_t any = 0;
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) any |= port[i].live;
    if (!any) return;
    if (anyStale) rebuildMaps();
    // The interval can change by OUTPUT <id> INTERVAL while running
    for (uint8_t i = 0; i < LAYOUT_PORTS; i++) {
        if (!port[i].live) continue;
        const OutputRecord* rec = outputRecord(slot[i].outputId);
        if (rec && rec->interval >= 20) port[i].interval = rec->interval;
    }
    layoutTick(port, LAYOUT_PORTS, millis(), portIo, &roundRobin);
}

// ---- OUTPUT <id> ----

// Is signal i's application used by the next row too (EGT#1, EGT#2 ...)?
static FLASHMEM bool appRepeats(const CanLayout& L, uint8_t i) {
    if (i + 1 >= L.count) return false;
    CanLayoutSignal a, b;
    canLayoutSignalAt(L, i, a);
    canLayoutSignalAt(L, i + 1, b);
    return a.app == b.app;
}

FLASHMEM void canLayoutPrintDetail(uint8_t id) {
    initSlots();
    const OutputRecord* rec = outputRecord(id);
    const CanLayout* lp = rec ? layoutOf(rec->format) : nullptr;
    if (!lp) return;
    const CanLayout& L = *lp;
    msg.control.print(F("  Frames: "));
    msg.control.print(L.frames);
    if (L.frameIds) {
        // Haltech's IDs: listed, and a frame with nothing mapped stays off the bus
        msg.control.print(F(" at"));
        for (uint8_t f = 0; f < L.frames; f++) {
            // Upper case, as the docs and Haltech's table write them (print(HEX) is lower case on Teensy)
            const uint16_t fid = canLayoutFrameId(L, f);
            char hex[6] = {' ', '0', 'x', 0, 0, 0};
            static const char D[] = "0123456789ABCDEF";
            hex[3] = D[(fid >> 8) & 0xF]; hex[4] = D[(fid >> 4) & 0xF]; hex[5] = D[fid & 0xF];
            for (uint8_t c = 0; c < 6; c++) msg.control.print(hex[c]);
        }
        msg.control.println(F(" (11-bit, 1 Mbit/s), every interval; see docs/can/HALTECH.md"));
        msg.control.println(F("  A frame with no input routed here is not sent; a field with no data is 0"));
    } else {
        msg.control.print(F(" from 0x"));
        msg.control.print(L.baseId, HEX);
        msg.control.println(F(" (11-bit), every interval; see docs/can/README.md"));
    }

    const uint8_t s = slotOf(id);
    // The map as the live engine has it; a stopped output shows what it would be
    uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
    if (s != 0xFF) memcpy(map, slot[s].map.slot, sizeof(map));
    else           canLayoutBuildMap(L, inputs, MAX_INPUTS, map);
    msg.control.print(F("  Signals:"));
    uint8_t col = 0;
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal sg;
        canLayoutSignalAt(L, i, sg);
        if (col == 0) msg.control.print(F("\n   "));
        msg.control.print(' ');
        msg.control.print(getApplicationNameByIndex(sg.app));
        // Repeated applications carry their instance (EGT#2)
        if (sg.instance > 1 || appRepeats(L, i)) {
            msg.control.print('#');
            msg.control.print(sg.instance);
        }
        msg.control.print(F(" = "));
        if (map[i] >= MAX_INPUTS) {
            msg.control.print(F("none"));
        } else {
            char label[12];
            formatPinLabel(inputs[map[i]].pin, label, sizeof(label));
            msg.control.print(label);
            if (inputs[map[i]].measurementType != sg.measurementType) msg.control.print(F(" (type mismatch)"));
        }
        msg.control.print(',');
        col = (uint8_t)((col + 1) % 2);
    }
    msg.control.println();

    const uint8_t bus = outputTransportCanBus(rec->transport);
    const uint32_t baud = bus != 0xFF ? canBusRunningBaud(bus) : 0;
    if (baud) {
        const uint16_t load = canLayoutBusLoadX10(L.frames, rec->interval, baud);
        msg.control.print(F("  Bus load: about "));
        msg.control.print(load / 10);
        msg.control.print('.');
        msg.control.print(load % 10);
        msg.control.print(F("% at "));
        msg.control.print(baud / 1000);
        msg.control.println(F(" kbit/s"));
        if (load > 300) {
            msg.control.println(F("  NOTE: over 30% of the bus; a longer INTERVAL leaves room for other traffic"));
        }
    }
    if (s != 0xFF) {
        msg.control.print(F("  Passes sent: "));
        msg.control.print(port[s].pass.passes);
        msg.control.print(F(", skipped (still leaving): "));
        msg.control.print(port[s].pass.skipped);
        msg.control.print(F(", unACKed (dropped): "));
        msg.control.println(port[s].pass.unacked);
    }
}

#endif  // INCLUDE_CAN_LAYOUTS
