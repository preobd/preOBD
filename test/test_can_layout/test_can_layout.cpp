// Native unit test: the CAN layout engine and the PREOBD layout
// (src/outputs/formats/can_layout.cpp, layout_preobd.cpp, #612 phase 3).
//
// Pinned here: the input map (pin order, holes, disabled keeps its instance,
// stale-then-rebuild), frame encoding (known bytes, no-data cases), pass pacing
// (completion, no drift, no burst, stalled/skipped, one drain per bus), every
// signal's stated range against its field and the application defaults, and the
// published DBC: this file RENDERS the PREOBD table to DBC text and fails when
// docs/can/preOBD.dbc differs. PREOBD_WRITE_DBC=1 (tools/gen_preobd_dbc.py)
// writes the file instead, so the DBC cannot drift from the firmware table.
//
// DBC convention used (checked with cantools when installed): the signals are
// Motorola / big-endian (@0+), and a Motorola start bit is the position of the
// signal's MOST significant bit in DBC's "sawtooth" numbering (byte 0 = bits
// 7..0, byte 1 = 15..8, ...). A 16-bit field in bytes b..b+1 therefore starts
// at bit 8*b + 7; a 1-byte field in byte b also at 8*b + 7.
#include <unity.h>
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/outputs/formats/field_encode.cpp"
#include "../../src/outputs/formats/obd_encode.cpp"
#include "../../src/outputs/formats/can_layout.cpp"
#include "../../src/outputs/formats/layout_preobd.cpp"
#include "../../src/outputs/formats/layout_haltech.cpp"
#include "../../src/outputs/formats/can_layout_ids.cpp"
#include "../../src/inputs/pressure_reference.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"
#include "../../src/sensors/can_sensor_library/obd_output_pids.cpp"

void setUp(void) {}
void tearDown(void) {}

// ---- helpers ----

static const CanLayout& L = PREOBD_LAYOUT;

static void clearInputs(Input* in, uint8_t n) {
    memset(in, 0, sizeof(Input) * n);
    for (uint8_t i = 0; i < n; i++) in[i].pin = 0xFF;
}

static void setInput(Input& in, uint8_t pin, uint8_t app, float value, uint8_t routeMask = 0xFF) {
    in.pin = pin;
    in.applicationIndex = app;
    in.measurementType = getApplicationExpectedMeasurementType(app);
    in.value = value;
    in.flags.isEnabled = 1;
    in.flags.hasReading = 1;
    in.routeMask = routeMask;
}

static uint8_t sigIndex(uint8_t app, uint8_t instance) {
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(L, i, s);
        if (s.app == app && s.instance == instance) return i;
    }
    TEST_FAIL_MESSAGE("no such signal");
    return 0;
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void frameOf(const Input* in, uint8_t n, uint8_t route, uint8_t frame, uint8_t* out) {
    uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
    canLayoutBuildMap(L, in, n, map);
    canLayoutBuildFrame(L, map, in, n, route, frame, out);
}

// ---- J2: the map ----

static void test_map_pin_order_and_holes(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[5], 20, APP_EGT, 500);
    setInput(in[2], 30, APP_EGT, 600);
    setInput(in[7], 10, APP_EGT, 400);
    setInput(in[0], 0xFF, APP_EGT, 999);   // not configured: a hole, never counted
    in[0].pin = 0xFF;
    uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
    canLayoutBuildMap(L, in, MAX_INPUTS, map);
    TEST_ASSERT_EQUAL_UINT8(7, map[sigIndex(APP_EGT, 1)]);   // lowest pin
    TEST_ASSERT_EQUAL_UINT8(5, map[sigIndex(APP_EGT, 2)]);
    TEST_ASSERT_EQUAL_UINT8(2, map[sigIndex(APP_EGT, 3)]);
    TEST_ASSERT_EQUAL_UINT8(LAYOUT_NO_INPUT, map[sigIndex(APP_EGT, 4)]);
    TEST_ASSERT_EQUAL_UINT8(LAYOUT_NO_INPUT, map[sigIndex(APP_CHT, 1)]);
}

static void test_map_slot_reuse_does_not_move_instances(void) {
    // Same pins in different slots: the instances follow the pins
    Input a[MAX_INPUTS]; clearInputs(a, MAX_INPUTS);
    setInput(a[0], 10, APP_CHT, 100); setInput(a[1], 20, APP_CHT, 200);
    Input b[MAX_INPUTS]; clearInputs(b, MAX_INPUTS);
    setInput(b[9], 10, APP_CHT, 100); setInput(b[3], 20, APP_CHT, 200);
    uint8_t ma[CAN_LAYOUT_MAX_SIGNALS], mb[CAN_LAYOUT_MAX_SIGNALS];
    canLayoutBuildMap(L, a, MAX_INPUTS, ma);
    canLayoutBuildMap(L, b, MAX_INPUTS, mb);
    TEST_ASSERT_EQUAL_UINT8(0, ma[sigIndex(APP_CHT, 1)]);
    TEST_ASSERT_EQUAL_UINT8(9, mb[sigIndex(APP_CHT, 1)]);
    TEST_ASSERT_EQUAL_UINT8(1, ma[sigIndex(APP_CHT, 2)]);
    TEST_ASSERT_EQUAL_UINT8(3, mb[sigIndex(APP_CHT, 2)]);
}

static void test_disabled_egt1_keeps_egt2(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[0], 10, APP_EGT, 400);
    setInput(in[1], 11, APP_EGT, 500);
    in[0].flags.isEnabled = 0;
    uint8_t f[8];
    frameOf(in, MAX_INPUTS, 1, 4, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 0));           // EGT#1: disabled, no data
    TEST_ASSERT_EQUAL_HEX16(5500, be16(f + 2));              // EGT#2 stays #2: (500+50)/0.1
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 6));
}

static void test_type_mismatch_route_and_no_reading(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[0], 10, APP_COOLANT_TEMP, 90.0f);
    uint8_t f[8];
    frameOf(in, MAX_INPUTS, 1, 1, f);
    TEST_ASSERT_EQUAL_HEX8(0x05, f[0]); TEST_ASSERT_EQUAL_HEX8(0x78, f[1]);   // 90.0 degC -> 1400
    // Measures something else than the row: no data
    in[0].measurementType = MEASURE_PRESSURE;
    frameOf(in, MAX_INPUTS, 1, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f));
    in[0].measurementType = MEASURE_TEMPERATURE;
    // Route bit off: output id 3 is bit 0x08, the input is routed to 0x01 only
    in[0].routeMask = 0x01;
    frameOf(in, MAX_INPUTS, 0x08, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f));
    frameOf(in, MAX_INPUTS, 0x01, 1, f);
    TEST_ASSERT_EQUAL_HEX16(1400, be16(f));
    // No reading yet / NaN / inf
    in[0].flags.hasReading = 0;
    frameOf(in, MAX_INPUTS, 0x01, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f));
    in[0].flags.hasReading = 1;
    in[0].value = NAN;
    frameOf(in, MAX_INPUTS, 0x01, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f));
    in[0].value = INFINITY;
    frameOf(in, MAX_INPUTS, 0x01, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f));
}

static void test_stale_then_rebuild(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[0], 10, APP_OIL_TEMP, 100);
    CanLayoutMap m; memset(&m, 0, sizeof(m));
    m.stale = true;
    TEST_ASSERT_TRUE(canLayoutMapFresh(m, L, in, MAX_INPUTS));
    TEST_ASSERT_EQUAL_UINT8(0, m.slot[sigIndex(APP_OIL_TEMP, 1)]);
    // A configuration change moves the sensor to another slot: the old map
    // keeps serving until it is marked, and the hook only marks
    clearInputs(in, MAX_INPUTS);
    setInput(in[6], 10, APP_OIL_TEMP, 100);
    TEST_ASSERT_FALSE(canLayoutMapFresh(m, L, in, MAX_INPUTS));
    TEST_ASSERT_EQUAL_UINT8(0, m.slot[sigIndex(APP_OIL_TEMP, 1)]);
    canLayoutMapMarkStale(m);
    TEST_ASSERT_TRUE(canLayoutMapFresh(m, L, in, MAX_INPUTS));
    TEST_ASSERT_EQUAL_UINT8(6, m.slot[sigIndex(APP_OIL_TEMP, 1)]);
    TEST_ASSERT_FALSE(canLayoutMapFresh(m, L, in, MAX_INPUTS));   // fresh again
}

// ---- known values ----

static void test_known_values(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_ENGINE_RPM, 3000);
    setInput(in[1], 2, APP_VEHICLE_SPEED, 100.5f);
    setInput(in[2], 3, APP_MAP_PRESSURE, 1.0f);       // bar absolute -> 100 kPa
    setInput(in[3], 4, APP_BOOST_PRESSURE, 0.5f);     // bar gauge -> 50 kPa, +100 offset
    uint8_t f[8];
    frameOf(in, MAX_INPUTS, 1, 0, f);
    TEST_ASSERT_EQUAL_HEX16(0x0BB8, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(10050, be16(f + 2));
    TEST_ASSERT_EQUAL_HEX16(1000, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(1500, be16(f + 6));

    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_PRIMARY_BATTERY, 13.8f);
    setInput(in[1], 2, APP_THROTTLE_POSITION, 100.0f);
    setInput(in[2], 3, APP_FUEL_LEVEL, 42.5f);
    frameOf(in, MAX_INPUTS, 1, 6, f);
    TEST_ASSERT_EQUAL_HEX16(13800, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 2));
    TEST_ASSERT_EQUAL_HEX16(1000, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(425, be16(f + 6));

    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_ELEVATION, 150.0f);
    setInput(in[1], 2, APP_BAROMETRIC_PRESSURE, 1.013f);
    frameOf(in, MAX_INPUTS, 1, 7, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 0));      // ambient: none
    TEST_ASSERT_EQUAL_HEX16(1013, be16(f + 2));        // 101.3 kPa
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(1150, be16(f + 6));        // 150 m + 1000

    // Levels are one byte each (MEASURE_DIGITAL), bytes 2-7 spare
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_COOLANT_LEVEL, 1.0f);
    frameOf(in, MAX_INPUTS, 1, 8, f);
    TEST_ASSERT_EQUAL_HEX8(0x01, f[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, f[1]);                // oil level: no data
    for (int i = 2; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0x00, f[i]);
}

static void test_clamps_below_no_data_and_survives_overflow(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_COOLANT_TEMP, 1e9f);
    setInput(in[1], 2, APP_OIL_TEMP, -1e9f);
    setInput(in[2], 3, APP_OIL_PRESSURE, 3.0e38f);     // x100 overflows to inf in kPa
    uint8_t f[8];
    frameOf(in, MAX_INPUTS, 1, 1, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, be16(f + 0));      // a real value is never the no-data pattern
    TEST_ASSERT_EQUAL_HEX16(0x0000, be16(f + 2));
    frameOf(in, MAX_INPUTS, 1, 2, f);
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, be16(f + 0));
}

// ---- J5: the table ----

static double cap(const CanLayoutSignal& s, bool max) {
    const double top = s.length == 1 ? 254.0 : s.length == 2 ? 65534.0 : 4294967294.0;
    return max ? top * (double)s.scale + (double)s.offset : (double)s.offset;
}

static void test_table_is_well_formed(void) {
    TEST_ASSERT_EQUAL_UINT8(9, L.frames);
    TEST_ASSERT_EQUAL_HEX16(0x500, L.baseId);
    bool covered[NUM_APPLICATION_PRESETS] = {false};
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(L, i, s);
        TEST_ASSERT_TRUE(s.frame < L.frames);
        TEST_ASSERT_TRUE(s.startByte + s.length <= 8);
        TEST_ASSERT_TRUE(s.length == 1 || s.length == 2);
        TEST_ASSERT_TRUE(s.app > APP_NONE && s.app < NUM_APPLICATION_PRESETS);
        TEST_ASSERT_TRUE(s.instance >= 1);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(getApplicationExpectedMeasurementType(s.app), s.measurementType,
                                        getApplicationNameByIndex(s.app));
        covered[s.app] = true;
        for (uint8_t j = (uint8_t)(i + 1); j < L.count; j++) {
            CanLayoutSignal t; canLayoutSignalAt(L, j, t);
            TEST_ASSERT_FALSE_MESSAGE(t.app == s.app && t.instance == s.instance, "duplicate signal");
            if (t.frame == s.frame) {
                const bool apart = s.startByte + s.length <= t.startByte || t.startByte + t.length <= s.startByte;
                TEST_ASSERT_TRUE_MESSAGE(apart, "signals overlap in a frame");
            }
        }
    }
    for (uint8_t a = 1; a < NUM_APPLICATION_PRESETS; a++) {
        TEST_ASSERT_TRUE_MESSAGE(covered[a], getApplicationNameByIndex(a));   // all 28 applications
    }
}

static void test_kpa_index_is_kpa(void) {
    char name[16];
    strncpy_P(name, (const char*)pgm_read_ptr(&UNITS_REGISTRY[U_KPA].name), sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    TEST_ASSERT_EQUAL_STRING("KPA", name);
    TEST_ASSERT_EQUAL_UINT8(UNITS_NOT_FOUND, LAYOUT_UNITS_NONE);   // obdConvertUnits leaves the value alone
}

static void test_ranges_fit_the_fields_and_cover_the_defaults(void) {
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(L, i, s);
        const char* name = getApplicationNameByIndex(s.app);
        TEST_ASSERT_TRUE_MESSAGE(s.rangeMin < s.rangeMax, name);
        TEST_ASSERT_TRUE_MESSAGE((double)s.rangeMin >= cap(s, false) - 1e-3, name);
        TEST_ASSERT_TRUE_MESSAGE((double)s.rangeMax <= cap(s, true) + 1e-3, name);
        // The application's alarm defaults (base units) must fit the stated range
        const ApplicationPreset* p = getApplicationByIndex(s.app);
        const float lo = obdConvertUnits(pgm_read_float(&p->defaultMinValue), s.unitsIndex);
        const float hi = obdConvertUnits(pgm_read_float(&p->defaultMaxValue), s.unitsIndex);
        if (s.measurementType != MEASURE_DIGITAL) {
            TEST_ASSERT_TRUE_MESSAGE(hi <= s.rangeMax, name);
            TEST_ASSERT_TRUE_MESSAGE(lo >= s.rangeMin, name);
        }
    }
}

// ---- J4: pacing ----

struct Fake {
    int budget = 1000;        // frames the bus accepts before the next refill
    bool abortOk = true;
    int aborts = 0;
    std::vector<uint32_t> ids;        // every frame written, in order
    std::vector<uint8_t> buses;
    uint8_t fill = 0;                 // build() output value
    uint16_t skip = 0;                // frames build() leaves out of the pass
};

static bool fWrite(void* c, uint8_t bus, uint32_t id, const uint8_t*) {
    Fake* f = (Fake*)c;
    if (f->budget <= 0) return false;
    f->budget--;
    f->ids.push_back(id);
    f->buses.push_back(bus);
    return true;
}
static bool fBuild(void* c, uint8_t frame, uint8_t* out8) {
    memset(out8, ((Fake*)c)->fill + frame, 8);
    return !(((Fake*)c)->skip & (1u << frame));
}
static bool fAbort(void* c, uint8_t) { Fake* f = (Fake*)c; f->aborts++; return f->abortOk; }

static LayoutIO ioFor(Fake& f) { return { fWrite, fBuild, fAbort, &f }; }

static void tickOne(LayoutPass& p, Fake& f, uint32_t now, int k, uint16_t interval = 100) {
    f.budget = k;
    const LayoutIO io = ioFor(f);
    layoutPassPace(p, now, interval, 0, io);
    layoutPassDrain(p, 0, io);
}

static void test_pass_completes_across_ticks(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    tickOne(p, f, 0, 4);
    TEST_ASSERT_EQUAL_UINT32(4, f.ids.size());
    TEST_ASSERT_TRUE(p.pending);
    tickOne(p, f, 10, 4);
    tickOne(p, f, 20, 4);
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_EQUAL_UINT32(9, f.ids.size());
    for (size_t i = 0; i < 9; i++) TEST_ASSERT_EQUAL_HEX32(0x500 + i, f.ids[i]);   // in order, 0x500-0x508
    TEST_ASSERT_EQUAL_UINT16(0, p.skipped);
    TEST_ASSERT_EQUAL_UINT16(0, p.unacked);
    TEST_ASSERT_EQUAL_UINT32(1, p.passes);
    TEST_ASSERT_EQUAL_INT(0, f.aborts);
    // Nothing more until the interval is up
    tickOne(p, f, 99, 100);
    TEST_ASSERT_EQUAL_UINT32(9, f.ids.size());
    tickOne(p, f, 100, 100);
    TEST_ASSERT_EQUAL_UINT32(18, f.ids.size());
}

static void test_last_send_advances_by_one_interval(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    tickOne(p, f, 1000, 100);
    TEST_ASSERT_EQUAL_UINT32(1000, p.lastSend);
    tickOne(p, f, 1100, 100);
    TEST_ASSERT_EQUAL_UINT32(1100, p.lastSend);
    tickOne(p, f, 1230, 100);           // late by 30: the schedule does not drift
    TEST_ASSERT_EQUAL_UINT32(1200, p.lastSend);
    tickOne(p, f, 1299, 100);           // 1300 is next
    TEST_ASSERT_EQUAL_UINT32(1200, p.lastSend);
    TEST_ASSERT_EQUAL_UINT32(27, f.ids.size());
}

static void test_no_catch_up_burst_after_a_gap(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    tickOne(p, f, 0, 100);
    tickOne(p, f, 10000, 100);          // 100 intervals late
    TEST_ASSERT_EQUAL_UINT32(2, p.passes);
    TEST_ASSERT_EQUAL_UINT32(10000, p.lastSend);    // resynced to now
    tickOne(p, f, 10001, 100);
    TEST_ASSERT_EQUAL_UINT32(2, p.passes);          // no burst
    TEST_ASSERT_EQUAL_UINT32(18, f.ids.size());
}

static void test_stalled_pass_aborts_and_restarts(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    tickOne(p, f, 0, 3);                // 3 of 9 leave, then the queue is full
    tickOne(p, f, 50, 0);               // nobody ACKs
    TEST_ASSERT_EQUAL_INT(0, f.aborts);
    f.fill = 7;
    tickOne(p, f, 100, 2);              // next pass due, last one still unsent
    TEST_ASSERT_EQUAL_INT(1, f.aborts);
    TEST_ASSERT_EQUAL_UINT16(1, p.unacked);
    TEST_ASSERT_EQUAL_UINT16(0, p.skipped);
    TEST_ASSERT_EQUAL_UINT32(2, p.passes);
    TEST_ASSERT_EQUAL_UINT32(5, f.ids.size());      // fresh pass: back to frame 0
    TEST_ASSERT_EQUAL_HEX32(0x500, f.ids[3]);
    TEST_ASSERT_EQUAL_HEX32(0x501, f.ids[4]);
}

static void test_abort_refused_keeps_old_pass_and_counts_skipped(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    f.abortOk = false;                  // an OBD multi-frame answer is in flight
    tickOne(p, f, 0, 3);
    tickOne(p, f, 100, 2);
    TEST_ASSERT_EQUAL_INT(1, f.aborts);
    TEST_ASSERT_EQUAL_UINT16(1, p.skipped);
    TEST_ASSERT_EQUAL_UINT16(0, p.unacked);
    TEST_ASSERT_EQUAL_UINT32(1, p.passes);          // still the first pass
    TEST_ASSERT_EQUAL_HEX32(0x503, f.ids[3]);       // resumed where it stopped
    TEST_ASSERT_EQUAL_HEX32(0x504, f.ids[4]);
}

static void test_one_drain_per_bus_round_robin(void) {
    Fake fa, fb; LayoutPort ports[3]; LayoutIO io[3];
    uint8_t rr = 0;
    for (int i = 0; i < 3; i++) {
        memset(&ports[i], 0, sizeof(ports[i]));
        layoutPassInit(ports[i].pass, 9, 0x500);
        ports[i].interval = 100;
        ports[i].live = true;
    }
    ports[0].bus = 0; ports[1].bus = 0; ports[2].bus = 1;
    io[0] = ioFor(fa); io[1] = ioFor(fa); io[2] = ioFor(fb);
    // t=0: all three due. Buses 0 and 1 each get ONE drain: 2 drains
    TEST_ASSERT_EQUAL_UINT8(2, layoutTick(ports, 3, 0, io, &rr));
    TEST_ASSERT_EQUAL_UINT32(9, fa.ids.size());      // one port on bus 0 sent its pass
    TEST_ASSERT_EQUAL_UINT32(9, fb.ids.size());
    TEST_ASSERT_TRUE(ports[2].pass.pending == false);
    const bool p0done = !ports[0].pass.pending;
    const bool p1done = !ports[1].pass.pending;
    TEST_ASSERT_TRUE(p0done != p1done);               // exactly one of them
    // t=1: the other port on bus 0 takes its turn
    TEST_ASSERT_EQUAL_UINT8(1, layoutTick(ports, 3, 1, io, &rr));
    TEST_ASSERT_FALSE(ports[0].pass.pending);
    TEST_ASSERT_FALSE(ports[1].pass.pending);
    TEST_ASSERT_EQUAL_UINT32(18, fa.ids.size());
    TEST_ASSERT_EQUAL_UINT16(0, ports[0].pass.unacked + ports[1].pass.unacked);
}

static void test_bus_reset_drops_the_pass(void) {
    Fake f; LayoutPass p; layoutPassInit(p, 9, 0x500);
    tickOne(p, f, 0, 3);
    layoutPassReset(p);
    TEST_ASSERT_FALSE(p.pending);
    tickOne(p, f, 5, 100);              // a fresh pass starts at once, no abort
    TEST_ASSERT_EQUAL_INT(0, f.aborts);
    TEST_ASSERT_EQUAL_UINT32(12, f.ids.size());
}

static void test_bus_load_estimate(void) {
    // 9 frames every 20 ms at 125 kbit/s: ~40 % (the floor's NOTE case)
    const uint16_t x10 = canLayoutBusLoadX10(9, 20, 125000);
    TEST_ASSERT_TRUE(x10 > 380 && x10 < 420);
    TEST_ASSERT_TRUE(canLayoutBusLoadX10(9, 100, 500000) < 30);
}

// ---- J5: the DBC ----

static std::string fmtNum(double v) {
    char b[40];
    snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

static const char* unitOf(const CanLayoutSignal& s) {
    switch (s.measurementType) {
    case MEASURE_TEMPERATURE: return "degC";
    case MEASURE_PRESSURE:    return "kPa";
    case MEASURE_VOLTAGE:     return "V";
    case MEASURE_RPM:         return "rpm";
    case MEASURE_SPEED:       return "km/h";
    case MEASURE_HUMIDITY:
    case MEASURE_PERCENT:     return "%";
    case MEASURE_ELEVATION:   return "m";
    default:                  return "";
    }
}

static std::string signalName(uint8_t i) {
    CanLayoutSignal s; canLayoutSignalAt(L, i, s);
    std::string n = getApplicationNameByIndex(s.app);
    bool repeats = s.instance > 1;
    for (uint8_t j = 0; j < L.count; j++) {
        CanLayoutSignal t; canLayoutSignalAt(L, j, t);
        if (j != i && t.app == s.app) repeats = true;
    }
    if (repeats) n += "_" + std::to_string(s.instance);
    return n;
}

static std::string renderDbc() {
    std::string o;
    o += "VERSION \"preOBD PREOBD layout 1\"\n\n";
    o += "NS_ :\n\tCM_\n\tBA_DEF_\n\tBA_\n\tVAL_\n\nBS_:\n\nBU_: PREOBD\n\n";
    for (uint8_t f = 0; f < L.frames; f++) {
        o += "BO_ " + std::to_string(L.baseId + f) + " PREOBD_F" + std::to_string(f) + ": 8 PREOBD\n";
        for (uint8_t i = 0; i < L.count; i++) {
            CanLayoutSignal s; canLayoutSignalAt(L, i, s);
            if (s.frame != f) continue;
            // Motorola (big-endian, @0): the start bit is the MSB's position
            const unsigned startBit = (s.flags & LAYOUT_LITTLE_ENDIAN) ? s.startByte * 8u : s.startByte * 8u + 7u;
            const double top = s.length == 1 ? 254.0 : 65534.0;
            o += " SG_ " + signalName(i) + " : " + std::to_string(startBit) + "|" + std::to_string(s.length * 8) +
                 ((s.flags & LAYOUT_LITTLE_ENDIAN) ? "@1" : "@0") + ((s.flags & LAYOUT_SIGNED) ? "-" : "+") +
                 " (" + fmtNum(s.scale) + "," + fmtNum(s.offset) + ") [" + fmtNum(cap(s, false)) + "|" +
                 fmtNum(top * s.scale + s.offset) + "] \"" + unitOf(s) + "\" Vector__XXX\n";
        }
        o += "\n";
    }
    for (uint8_t f = 0; f < L.frames; f++) {
        o += "CM_ BO_ " + std::to_string(L.baseId + f) + " \"preOBD gauge frame " + std::to_string(f) +
             ", pushed every interval. Fields are big-endian; all ones means no data.\";\n";
    }
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(L, i, s);
        o += "CM_ SG_ " + std::to_string(L.baseId + s.frame) + " " + signalName(i) + " \"" +
             getApplicationNameByIndex(s.app) + " input " + std::to_string(s.instance) +
             " (in pin order); all ones = no data.\";\n";
    }
    for (uint8_t i = 0; i < L.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(L, i, s);
        o += "VAL_ " + std::to_string(L.baseId + s.frame) + " " + signalName(i) + " " +
             (s.length == 1 ? "255" : "65535") + " \"N/A\" ;\n";
    }
    return o;
}

static bool readFile(const char* path, std::string& out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    char b[4096];
    size_t n;
    out.clear();
    while ((n = fread(b, 1, sizeof(b), fp)) > 0) out.append(b, n);
    fclose(fp);
    return true;
}

static const char* dbcPath() {
    static std::string found;
    const char* cands[] = {"docs/can/preOBD.dbc", "../docs/can/preOBD.dbc", "../../docs/can/preOBD.dbc"};
    std::string tmp;
    for (const char* c : cands) {
        if (readFile(c, tmp)) { found = c; return found.c_str(); }
    }
    return cands[0];
}

static void test_dbc_matches_the_firmware_table(void) {
    const std::string fresh = renderDbc();
    const char* w = getenv("PREOBD_WRITE_DBC");
    if (w && w[0] == '1') {
        FILE* fp = fopen("docs/can/preOBD.dbc", "wb");
        TEST_ASSERT_NOT_NULL_MESSAGE(fp, "cannot write docs/can/preOBD.dbc (run from the repo root)");
        fwrite(fresh.data(), 1, fresh.size(), fp);
        fclose(fp);
        return;
    }
    std::string committed;
    TEST_ASSERT_TRUE_MESSAGE(readFile(dbcPath(), committed), "docs/can/preOBD.dbc is missing: run tools/gen_preobd_dbc.py");
    TEST_ASSERT_TRUE_MESSAGE(fresh == committed, "docs/can/preOBD.dbc differs from the PREOBD table: run tools/gen_preobd_dbc.py");
}

// #636: every standard PID SET CAN gives an application lands in PREOBD,
// built the way SET CAN builds it (a CAN:n pin above every wired one, the
// PID's measurement type, the row's application)
static void test_set_can_inputs_map(void) {
    for (uint8_t i = 0; i < NUM_STANDARD_PIDS; i++) {
        StandardPIDInfo r;
        memcpy_P(&r, &STANDARD_PID_TABLE[i], sizeof(r));
        const uint8_t app = importApplicationForPid(r.pid);
        if (app == APP_NONE) continue;
        char msg[24];
        snprintf(msg, sizeof(msg), "PID 0x%02X", r.pid);
        Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
        setInput(in[3], 0xC0, app, 1.0f);
        in[3].measurementType = r.measurementType;
        uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
        canLayoutBuildMap(L, in, MAX_INPUTS, map);
        const uint8_t s = sigIndex(app, 1);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, map[s], msg);
        CanLayoutSignal sig; canLayoutSignalAt(L, s, sig);
        uint8_t out[4];
        TEST_ASSERT_TRUE_MESSAGE(canLayoutEncodeSignal(sig, in[3], 1, out), msg);   // its type passes the row's gate
        // A wired input of the same application is instance 1, the import 2
        setInput(in[9], 5, app, 1.0f);
        canLayoutBuildMap(L, in, MAX_INPUTS, map);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(9, map[s], msg);
    }
}


// ---- HALTECH (#612 phase 4) ----
// Expected bytes are worked by hand from Haltech ECU Broadcast CAN Protocol
// v2.0: temperatures K x10, pressures absolute kPa x10 (brake: x1), speed
// km/h x10, battery V x10, percentages x10. No data is 0; a frame with no
// input routed to the output is not sent.

static const CanLayout& H = HALTECH_LAYOUT;

static bool hFrame(const Input* in, uint8_t route, uint8_t frame, uint8_t* out, float baro = STD_BARO_BAR) {
    uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
    canLayoutBuildMap(H, in, MAX_INPUTS, map);
    return canLayoutBuildFrame(H, map, in, MAX_INPUTS, route, frame, out, baro);
}

static void test_haltech_table_is_well_formed(void) {
    TEST_ASSERT_EQUAL_UINT8(HALTECH_FRAMES, H.frames);
    TEST_ASSERT_EQUAL_UINT8(CAN_LAYOUT_ZERO_NO_DATA, H.flags);
    TEST_ASSERT_NOT_NULL(H.frameIds);
    for (uint8_t f = 0; f < H.frames; f++) {
        TEST_ASSERT_EQUAL_HEX16(layoutFormatFrameId(FMT_HALTECH, f), canLayoutFrameId(H, f));
        TEST_ASSERT_TRUE(canLayoutFrameId(H, f) <= 0x7FF);   // 11-bit
    }
    bool frameUsed[LAYOUT_MAX_FRAMES] = {false};
    for (uint8_t i = 0; i < H.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(H, i, s);
        const char* name = getApplicationNameByIndex(s.app);
        TEST_ASSERT_TRUE_MESSAGE(s.frame < H.frames, name);
        TEST_ASSERT_TRUE_MESSAGE(s.startByte + s.length <= 8 && s.length == 2, name);
        TEST_ASSERT_TRUE_MESSAGE(s.startByte % 2 == 0, name);   // Haltech's fields are byte pairs
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(getApplicationExpectedMeasurementType(s.app), s.measurementType, name);
        // Absolute is set exactly on the pressure rows: Haltech's pressures are all absolute
        TEST_ASSERT_EQUAL_MESSAGE(s.measurementType == MEASURE_PRESSURE, (s.flags & LAYOUT_ABSOLUTE) != 0, name);
        frameUsed[s.frame] = true;
        for (uint8_t j = (uint8_t)(i + 1); j < H.count; j++) {
            CanLayoutSignal t; canLayoutSignalAt(H, j, t);
            TEST_ASSERT_FALSE_MESSAGE(t.app == s.app && t.instance == s.instance, "duplicate signal");
            if (t.frame == s.frame) {
                const bool apart = s.startByte + s.length <= t.startByte || t.startByte + t.length <= s.startByte;
                TEST_ASSERT_TRUE_MESSAGE(apart, "signals overlap in a frame");
            }
        }
    }
    for (uint8_t f = 0; f < H.frames; f++) TEST_ASSERT_TRUE_MESSAGE(frameUsed[f], "a frame with no signal");
}

static void test_haltech_known_values(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    uint8_t f[8];
    // 0x360: RPM, MAP (absolute already), TPS; bytes 6-7 coolant pressure: none, 0
    setInput(in[0], 1, APP_ENGINE_RPM, 3000);
    setInput(in[1], 2, APP_MAP_PRESSURE, 1.0f);           // 100.0 kPa abs
    setInput(in[2], 3, APP_THROTTLE_POSITION, 45.6f);     // the spec's own example: 456
    TEST_ASSERT_TRUE(hFrame(in, 1, 0, f));
    TEST_ASSERT_EQUAL_HEX16(3000, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(1000, be16(f + 2));
    TEST_ASSERT_EQUAL_HEX16(456, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 6));

    // 0x361: gauge oil and fuel pressure + baro (§14.8). 3.0 bar + 1.01325 = 401.325 kPa
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_FUEL_PRESSURE, 3.0f);
    setInput(in[1], 2, APP_OIL_PRESSURE, 0.0f);           // engine off: atmosphere, 101.3 on the dash's abs scale
    TEST_ASSERT_TRUE(hFrame(in, 1, 1, f));
    TEST_ASSERT_EQUAL_HEX16(4013, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(1013, be16(f + 2));           // the dash shows 1013/10 - 101.3 = 0 kPa
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 4));              // engine demand: none
    // The baro the caller passes is the one used: 0.9 bar at altitude
    TEST_ASSERT_TRUE(hFrame(in, 1, 1, f, 0.9f));
    TEST_ASSERT_EQUAL_HEX16(3900, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(900, be16(f + 2));

    // 0x36B: brake pressure front is 1 kPa per bit (y = x - 101.3): 10 bar gauge = 1101.325 kPa abs
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_BRAKE_PRESSURE, 10.0f);
    TEST_ASSERT_TRUE(hFrame(in, 1, 2, f));
    TEST_ASSERT_EQUAL_HEX16(1101, be16(f + 0));

    // 0x370 speed; 0x372 battery and baro (baro is absolute: no offset added)
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_VEHICLE_SPEED, 100.5f);
    setInput(in[1], 2, APP_PRIMARY_BATTERY, 13.8f);
    setInput(in[2], 3, APP_BAROMETRIC_PRESSURE, 1.013f);
    TEST_ASSERT_TRUE(hFrame(in, 1, 3, f));
    TEST_ASSERT_EQUAL_HEX16(1005, be16(f + 0));
    TEST_ASSERT_TRUE(hFrame(in, 1, 4, f, 1.013f));
    TEST_ASSERT_EQUAL_HEX16(138, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 4));              // target boost: none
    TEST_ASSERT_EQUAL_HEX16(1013, be16(f + 6));

    // 0x373 / 0x374: EGT 1-8 in pin order, K x10. 800 degC = 1073.15 K
    clearInputs(in, MAX_INPUTS);
    for (uint8_t i = 0; i < 8; i++) setInput(in[i], (uint8_t)(10 + i), APP_EGT, 800.0f + 10.0f * i);
    TEST_ASSERT_TRUE(hFrame(in, 1, 5, f));
    TEST_ASSERT_EQUAL_HEX16(10732, be16(f + 0));          // 10731.5 rounds half up
    TEST_ASSERT_EQUAL_HEX16(10832, be16(f + 2));
    TEST_ASSERT_TRUE(hFrame(in, 1, 6, f));
    TEST_ASSERT_EQUAL_HEX16(11132, be16(f + 0));          // EGT 5: 840 degC
    TEST_ASSERT_EQUAL_HEX16(11432, be16(f + 6));          // EGT 8: 870 degC

    // 0x376 ambient, humidity (signed)
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_AMBIENT_TEMP, -10.0f);         // 263.15 K
    setInput(in[1], 2, APP_HUMIDITY, 45.0f);
    TEST_ASSERT_TRUE(hFrame(in, 1, 7, f));
    TEST_ASSERT_EQUAL_HEX16(2632, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(450, be16(f + 2));

    // 0x3E0: coolant, IAT, (fuel temperature: 0), oil
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_COOLANT_TEMP, 90.0f);          // 363.15 K
    setInput(in[1], 2, APP_INTAKE_AIR_TEMP, 25.0f);       // 298.15 K
    setInput(in[2], 3, APP_OIL_TEMP, 110.0f);             // 383.15 K
    TEST_ASSERT_TRUE(hFrame(in, 1, 8, f));
    TEST_ASSERT_EQUAL_HEX16(3632, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(2982, be16(f + 2));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 4));
    TEST_ASSERT_EQUAL_HEX16(3832, be16(f + 6));

    // 0x3E1 gearbox (TRANS_TEMP), diff; 0x6F8 fuel level % in bytes 6-7, bytes 0-5 0 (enums: Disabled)
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_TRANS_TEMP, 80.0f);
    setInput(in[1], 2, APP_DIFF_TEMP, 70.0f);
    setInput(in[2], 3, APP_FUEL_LEVEL, 42.5f);
    TEST_ASSERT_TRUE(hFrame(in, 1, 9, f));
    TEST_ASSERT_EQUAL_HEX16(3532, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(3432, be16(f + 2));
    TEST_ASSERT_TRUE(hFrame(in, 1, 10, f));
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX8(0, f[i]);
    TEST_ASSERT_EQUAL_HEX16(425, be16(f + 6));
}

// Decision 2: no data is 0, a frame with nothing routed here stays off the bus,
// a real value uses the full range
static void test_haltech_no_data_and_frame_skip(void) {
    Input in[MAX_INPUTS]; clearInputs(in, MAX_INPUTS);
    uint8_t f[8];
    // Nothing configured: no frame is sent
    for (uint8_t fr = 0; fr < H.frames; fr++) TEST_ASSERT_FALSE(hFrame(in, 1, fr, f));

    // A mapped input with no reading, or disabled: the frame is sent, its field is 0
    setInput(in[0], 1, APP_COOLANT_TEMP, 90.0f);
    in[0].flags.hasReading = 0;
    memset(f, 0xAA, sizeof(f));
    TEST_ASSERT_TRUE(hFrame(in, 1, 8, f));
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0, f[i]);
    in[0].flags.hasReading = 1;
    in[0].flags.isEnabled = 0;
    TEST_ASSERT_TRUE(hFrame(in, 1, 8, f));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 0));
    in[0].flags.isEnabled = 1;
    in[0].value = NAN;                                     // a faulted reading: 0, still sent
    TEST_ASSERT_TRUE(hFrame(in, 1, 8, f));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 0));

    // Routed to another output only: switched off here, and the frame with it
    in[0].value = 90.0f;
    in[0].routeMask = 0x02;
    TEST_ASSERT_FALSE(hFrame(in, 1, 8, f));
    TEST_ASSERT_TRUE(hFrame(in, 2, 8, f));
    TEST_ASSERT_EQUAL_HEX16(3632, be16(f + 0));

    // The wrong measurement type: 0 (the frame is still sent: the input is there)
    in[0].routeMask = 0xFF;
    in[0].measurementType = MEASURE_PRESSURE;
    TEST_ASSERT_TRUE(hFrame(in, 1, 8, f));
    TEST_ASSERT_EQUAL_HEX16(0, be16(f + 0));

    // Full range: a real value may reach 0xFFFF (no all-ones reserved), and clamps at 0
    clearInputs(in, MAX_INPUTS);
    setInput(in[0], 1, APP_ENGINE_RPM, 1e9f);
    setInput(in[1], 2, APP_THROTTLE_POSITION, -5.0f);
    setInput(in[2], 3, APP_OIL_PRESSURE, 3.0e38f);        // overflows to inf in kPa, still clamps
    TEST_ASSERT_TRUE(hFrame(in, 1, 0, f));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, be16(f + 4));
    TEST_ASSERT_TRUE(hFrame(in, 1, 1, f));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, be16(f + 2));

    // PREOBD is unchanged: a frame with nothing mapped is still sent, all-ones
    clearInputs(in, MAX_INPUTS);
    uint8_t map[CAN_LAYOUT_MAX_SIGNALS];
    canLayoutBuildMap(L, in, MAX_INPUTS, map);
    TEST_ASSERT_TRUE(canLayoutBuildFrame(L, map, in, MAX_INPUTS, 1, 1, f));
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, f[i]);
}

// The alarm defaults (base units, through the row's reference and units) fit
// each row's stated range, and the range fits the field
static void test_haltech_ranges(void) {
    for (uint8_t i = 0; i < H.count; i++) {
        CanLayoutSignal s; canLayoutSignalAt(H, i, s);
        const char* name = getApplicationNameByIndex(s.app);
        const double top = (s.flags & LAYOUT_SIGNED) ? 32767.0 : 65535.0;
        const double bottom = (s.flags & LAYOUT_SIGNED) ? -32768.0 : 0.0;
        TEST_ASSERT_TRUE_MESSAGE(s.rangeMin < s.rangeMax, name);
        TEST_ASSERT_TRUE_MESSAGE((double)s.rangeMin >= bottom * s.scale + s.offset - 1e-3, name);
        TEST_ASSERT_TRUE_MESSAGE((double)s.rangeMax <= top * s.scale + s.offset + 1e-3, name);
        const ApplicationPreset* p = getApplicationByIndex(s.app);
        float lo = pgm_read_float(&p->defaultMinValue), hi = pgm_read_float(&p->defaultMaxValue);
        if (s.flags & LAYOUT_ABSOLUTE) {
            const float off = pressureReferenceOffset(applicationReference(s.app), REF_ABSOLUTE, STD_BARO_BAR);
            lo += off; hi += off;
        }
        TEST_ASSERT_TRUE_MESSAGE(obdConvertUnits(hi, s.unitsIndex) <= s.rangeMax, name);
        TEST_ASSERT_TRUE_MESSAGE(obdConvertUnits(lo, s.unitsIndex) >= s.rangeMin, name);
    }
}

// The pass sends only the frames build() kept, on the layout's own IDs, in order
static void test_pass_skips_frames_and_uses_frame_ids(void) {
    Fake f; LayoutPass p;
    layoutPassInit(p, H.frames, H.baseId, H.frameIds);
    f.skip = (uint16_t)((1u << 2) | (1u << 6) | (1u << 10));   // 0x36B, 0x374, 0x6F8 have nothing
    tickOne(p, f, 0, 100);
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_EQUAL_UINT32(8, f.ids.size());
    static const uint32_t want[] = {0x360, 0x361, 0x370, 0x372, 0x373, 0x376, 0x3E0, 0x3E1};
    for (size_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX32(want[i], f.ids[i]);
    // A refused write resumes at the same frame, skipping still holds
    f.ids.clear();
    tickOne(p, f, 100, 3);
    TEST_ASSERT_TRUE(p.pending);
    tickOne(p, f, 110, 100);
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_EQUAL_UINT32(8, f.ids.size());
    for (size_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX32(want[i], f.ids[i]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_map_pin_order_and_holes);
    RUN_TEST(test_set_can_inputs_map);
    RUN_TEST(test_map_slot_reuse_does_not_move_instances);
    RUN_TEST(test_disabled_egt1_keeps_egt2);
    RUN_TEST(test_type_mismatch_route_and_no_reading);
    RUN_TEST(test_stale_then_rebuild);
    RUN_TEST(test_known_values);
    RUN_TEST(test_clamps_below_no_data_and_survives_overflow);
    RUN_TEST(test_table_is_well_formed);
    RUN_TEST(test_kpa_index_is_kpa);
    RUN_TEST(test_ranges_fit_the_fields_and_cover_the_defaults);
    RUN_TEST(test_pass_completes_across_ticks);
    RUN_TEST(test_last_send_advances_by_one_interval);
    RUN_TEST(test_no_catch_up_burst_after_a_gap);
    RUN_TEST(test_stalled_pass_aborts_and_restarts);
    RUN_TEST(test_abort_refused_keeps_old_pass_and_counts_skipped);
    RUN_TEST(test_one_drain_per_bus_round_robin);
    RUN_TEST(test_bus_reset_drops_the_pass);
    RUN_TEST(test_bus_load_estimate);
    RUN_TEST(test_dbc_matches_the_firmware_table);
    RUN_TEST(test_haltech_table_is_well_formed);
    RUN_TEST(test_haltech_known_values);
    RUN_TEST(test_haltech_no_data_and_frame_skip);
    RUN_TEST(test_haltech_ranges);
    RUN_TEST(test_pass_skips_frames_and_uses_frame_ids);
    return UNITY_END();
}
