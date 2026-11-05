// Native unit test: the standard OBD-II PID table and the units it maps to
// (src/sensors/can_sensor_library/standard_pids.cpp, #509).
//
// The table's first version was wrong in 16 of 31 rows. Among other errors,
// several rows named a unit the registry lacked, so SET CAN labelled them %.
// This pins every row's shape against what SET CAN and the decoder assume,
// the J1979-unit -> base-unit conversion, and the JSON unit lookup that must
// accept the exported symbol without hash-colliding (djb2("L/H") == "KPH").
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"

void setUp(void) {}
void tearDown(void) {}

static StandardPIDInfo row(uint8_t i) {
    StandardPIDInfo r;
    memcpy_P(&r, &STANDARD_PID_TABLE[i], sizeof(r));
    return r;
}

static StandardPIDInfo rowFor(uint8_t pid) {
    const StandardPIDInfo* p = lookupStandardPID(pid);
    TEST_ASSERT_NOT_NULL_MESSAGE(p, "PID missing from table");
    StandardPIDInfo r;
    memcpy_P(&r, p, sizeof(r));
    return r;
}

// Every row: sorted/unique, fits a cache entry, real unit of its own type, fits
// the input's name buffers
static void test_every_row_is_well_formed(void) {
    for (uint8_t i = 0; i < NUM_STANDARD_PIDS; i++) {
        StandardPIDInfo r = row(i);
        char msg[48];
        snprintf(msg, sizeof(msg), "PID 0x%02X", r.pid);
        if (i) TEST_ASSERT_TRUE_MESSAGE(row(i - 1).pid < r.pid, msg);          // sorted, unique
        TEST_ASSERT_TRUE_MESSAGE((r.pid & 0x1F) != 0, msg);                   // not a bitmap PID
        TEST_ASSERT_TRUE_MESSAGE(r.data_length == 1 || r.data_length == 2 || r.data_length == 4, msg);
        TEST_ASSERT_TRUE_MESSAGE(r.data_offset + r.data_length <= CAN_FRAME_DATA_MAX, msg);  // a cache entry
        // Past one single frame's 5 bytes, only the multi-frame PIDs (#527)
        if (r.data_offset + r.data_length > 5) {
            TEST_ASSERT_TRUE_MESSAGE(r.pid == 0x70 || r.pid == 0x78 || r.pid == 0x79 || r.pid == 0x7F, msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(r.measurementType != MEASURE_DIGITAL, msg);  // #509: shows up as %
        TEST_ASSERT_TRUE_MESSAGE(r.scale_factor != 0.0f, msg);
        TEST_ASSERT_TRUE_MESSAGE(strlen_P(r.name) < 32, msg);                 // Input::displayName
        TEST_ASSERT_TRUE_MESSAGE(strlen_P(r.abbr) < 8, msg);                  // Input::abbrName
        float scale, offset;
        uint8_t u = 0xEE;
        TEST_ASSERT_TRUE_MESSAGE(standardPidToBase(r, scale, offset, u), msg);  // unit resolves, right type
        TEST_ASSERT_TRUE_MESSAGE(unitsMatchMeasurement(u, (MeasurementType)r.measurementType), msg);
    }
}

// Rows are in J1979 units; the input stores base units
static void test_conversion_to_base(void) {
    float scale, offset;
    uint8_t u;

    StandardPIDInfo map = rowFor(0x0B);                  // kPa -> bar
    TEST_ASSERT_TRUE(standardPidToBase(map, scale, offset, u));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, scale);
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("KPA"), u);   // shown in kPa, as before

    StandardPIDInfo tmil = rowFor(0x4D);                 // min -> s
    TEST_ASSERT_TRUE(standardPidToBase(tmil, scale, offset, u));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, scale);
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("MIN"), u);

    StandardPIDInfo ect = rowFor(0x05);                  // C base: unchanged
    TEST_ASSERT_TRUE(standardPidToBase(ect, scale, offset, u));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, scale);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -40.0f, offset);

    StandardPIDInfo maf = rowFor(0x10);
    TEST_ASSERT_EQUAL(MEASURE_MASS_FLOW, maf.measurementType);
    TEST_ASSERT_TRUE(standardPidToBase(maf, scale, offset, u));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, scale);
}

// The rows #509 fixed, and those left out on purpose
static void test_audited_rows(void) {
    TEST_ASSERT_EQUAL(MEASURE_FUEL_RATE, rowFor(0x5E).measurementType);   // was "Mass Airflow"
    TEST_ASSERT_EQUAL(MEASURE_DISTANCE, rowFor(0xA6).measurementType);    // odometer
    TEST_ASSERT_EQUAL(MEASURE_TIME, rowFor(0x1F).measurementType);
    TEST_ASSERT_EQUAL(MEASURE_PERCENT, rowFor(0x8E).measurementType);     // friction torque, A-125
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -125.0f, rowFor(0x8E).offset);
    TEST_ASSERT_EQUAL_UINT8(1, rowFor(0x66).data_offset);                 // A = sensor-support byte
    TEST_ASSERT_EQUAL_UINT8(1, rowFor(0x67).data_offset);
    const uint8_t gone[] = {0x6F, 0xA4, 0xAA, 0xC3, 0xC4};
    for (uint8_t p : gone) TEST_ASSERT_NULL(lookupStandardPID(p));
}

// Multi-frame rows (#527) read the value J1979 puts after the support byte,
// in the layout preOBD's own output sends on 0x70/0x78 (obd_output_pids.cpp)
static void test_multi_frame_rows(void) {
    StandardPIDInfo boost = rowFor(0x70);
    TEST_ASSERT_EQUAL_UINT8(3, boost.data_offset);                 // D-E: boost sensor A
    TEST_ASSERT_EQUAL_UINT8(2, boost.data_length);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.03125f, boost.scale_factor);
    TEST_ASSERT_EQUAL(MEASURE_PRESSURE, boost.measurementType);
    const uint8_t egtPids[] = {0x78, 0x79};
    for (uint8_t pid : egtPids) {
        StandardPIDInfo egt = rowFor(pid);
        TEST_ASSERT_EQUAL_UINT8(1, egt.data_offset);               // B-C: sensor 1
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, egt.scale_factor);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, -40.0f, egt.offset);
    }
    StandardPIDInfo rt = rowFor(0x7F);
    TEST_ASSERT_EQUAL_UINT8(1, rt.data_offset);                    // B-E: total run time
    TEST_ASSERT_EQUAL_UINT8(4, rt.data_length);
    TEST_ASSERT_EQUAL(MEASURE_TIME, rt.measurementType);
    // EGT sensor 4 (H-I, offset 7) and 0x7F's PTO time (J-M, offset 9) fit a cache entry
    TEST_ASSERT_TRUE(7 + 2 <= CAN_FRAME_DATA_MAX);
    TEST_ASSERT_TRUE(9 + 4 <= CAN_FRAME_DATA_MAX);
}

// Sensors 2+ of multi-sensor PIDs (#535): each belongs to a table row, the
// sensors of a PID run 2..n with no gap, and each value sits inside the
// reply (single-frame PIDs inside one frame's 5 bytes), clear of the others
static void test_pid_sensors_well_formed(void) {
    for (uint8_t i = 0; i < NUM_STANDARD_PID_SENSORS; i++) {
        StandardPIDSensor v;
        memcpy_P(&v, &STANDARD_PID_SENSORS[i], sizeof(v));
        char msg[48];
        snprintf(msg, sizeof(msg), "PID 0x%02X sensor %u", v.pid, v.sensor);
        StandardPIDInfo r = rowFor(v.pid);
        if (i) {
            StandardPIDSensor p;
            memcpy_P(&p, &STANDARD_PID_SENSORS[i - 1], sizeof(p));
            TEST_ASSERT_TRUE_MESSAGE(p.pid < v.pid || (p.pid == v.pid && p.sensor + 1 == v.sensor), msg);
            if (p.pid != v.pid) TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, v.sensor, msg);
        } else {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, v.sensor, msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(v.data_offset >= 1, msg);   // past the support byte
        TEST_ASSERT_TRUE_MESSAGE(v.data_offset + r.data_length <= CAN_FRAME_DATA_MAX, msg);
        const bool multiFrame = v.pid == 0x70 || v.pid == 0x78 || v.pid == 0x79 || v.pid == 0x7F;
        if (!multiFrame) {
            TEST_ASSERT_TRUE_MESSAGE(v.data_offset + r.data_length <= 5, msg);
        }
        // No overlap with sensor 1 or any other sensor of the PID
        const uint8_t lo = v.data_offset, hi = v.data_offset + r.data_length;
        TEST_ASSERT_TRUE_MESSAGE(hi <= r.data_offset || lo >= r.data_offset + r.data_length, msg);
        for (uint8_t j = 0; j < i; j++) {
            StandardPIDSensor o;
            memcpy_P(&o, &STANDARD_PID_SENSORS[j], sizeof(o));
            if (o.pid != v.pid) continue;
            TEST_ASSERT_TRUE_MESSAGE(hi <= o.data_offset || lo >= o.data_offset + r.data_length, msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(strlen_P(v.name) < 32, msg);   // Input::displayName
        TEST_ASSERT_TRUE_MESSAGE(strlen_P(v.abbr) < 8, msg);    // Input::abbrName
        TEST_ASSERT_TRUE_MESSAGE(lookupStandardPIDSensor(v.pid, v.sensor) == &STANDARD_PID_SENSORS[i], msg);
    }
}

// The J1979 positions: EGT 1-4 at B, D, F, H; boost B's actual value at H
// (F-G is its commanded value); run time idle F-I, PTO J-M
static void test_pid_sensor_offsets(void) {
    const uint8_t egt[] = {0x78, 0x79};
    for (uint8_t pid : egt) {
        TEST_ASSERT_EQUAL_UINT8(4, standardPIDSensorCount(pid));
        for (uint8_t n = 2; n <= 4; n++) {
            StandardPIDSensor v;
            memcpy_P(&v, lookupStandardPIDSensor(pid, n), sizeof(v));
            TEST_ASSERT_EQUAL_UINT8(2 * n - 1, v.data_offset);
        }
        TEST_ASSERT_NULL(lookupStandardPIDSensor(pid, 5));
    }
    struct { uint8_t pid, sensor, offset, count; } cases[] = {
        {0x66, 2, 3, 2}, {0x67, 2, 2, 2}, {0x70, 2, 7, 2}, {0x7F, 2, 5, 3}, {0x7F, 3, 9, 3},
    };
    for (auto& c : cases) {
        StandardPIDSensor v;
        memcpy_P(&v, lookupStandardPIDSensor(c.pid, c.sensor), sizeof(v));
        TEST_ASSERT_EQUAL_UINT8(c.offset, v.data_offset);
        TEST_ASSERT_EQUAL_UINT8(c.count, standardPIDSensorCount(c.pid));
    }
    TEST_ASSERT_EQUAL_UINT8(1, standardPIDSensorCount(0x0C));   // one value
    TEST_ASSERT_NULL(lookupStandardPIDSensor(0x0C, 2));
    TEST_ASSERT_NULL(lookupStandardPIDSensor(0x78, 1));         // sensor 1 is the row
}

// New types: base unit first (factor 1), conversions, exclusive matching
static void test_new_units(void) {
    const MeasurementType types[] = {MEASURE_DISTANCE, MEASURE_TIME, MEASURE_MASS_FLOW, MEASURE_FUEL_RATE};
    for (MeasurementType t : types) {
        uint8_t base = defaultUnitsForMeasurement(t);
        TEST_ASSERT_TRUE(unitsMatchMeasurement(base, t));
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pgm_read_float(&UNITS_REGISTRY[base].conversionFactor));
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.621371f, pgm_read_float(&UNITS_REGISTRY[findUnitsIndexByName("MI")].conversionFactor));
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 1.0f / 3600.0f, pgm_read_float(&UNITS_REGISTRY[findUnitsIndexByName("H")].conversionFactor));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.132277f, pgm_read_float(&UNITS_REGISTRY[findUnitsIndexByName("LBMIN")].conversionFactor));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.264172f, pgm_read_float(&UNITS_REGISTRY[findUnitsIndexByName("GPH")].conversionFactor));
    TEST_ASSERT_FALSE(unitsMatchMeasurement(findUnitsIndexByName("KM"), MEASURE_SPEED));
    TEST_ASSERT_FALSE(unitsMatchMeasurement(findUnitsIndexByName("MIN"), MEASURE_DISTANCE));
    TEST_ASSERT_FALSE(unitsMatchMeasurement(findUnitsIndexByName("PERCENT"), MEASURE_TIME));
    // Every name/alias hash in the table is what djb2 computes
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        char buf[20];
        strncpy_P(buf, (const char*)pgm_read_ptr(&UNITS_REGISTRY[i].name), sizeof(buf));
        TEST_ASSERT_EQUAL_HEX16(djb2_hash(buf), pgm_read_word(&UNITS_REGISTRY[i].nameHash));
        strncpy_P(buf, (const char*)pgm_read_ptr(&UNITS_REGISTRY[i].alias), sizeof(buf));
        TEST_ASSERT_EQUAL_HEX16(djb2_hash(buf), pgm_read_word(&UNITS_REGISTRY[i].aliasHash));
    }
}

// SYSTEM DUMP JSON exports the symbol; the import must read it back
static void test_symbol_lookup(void) {
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("KPH"), findUnitsIndexByNameOrSymbol("km/h"));   // lost on restore before
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("LPH"), findUnitsIndexByNameOrSymbol("L/h"));    // hash == KPH's
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("GPS"), findUnitsIndexByNameOrSymbol("g/s"));
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("LBMIN"), findUnitsIndexByNameOrSymbol("lb/min"));
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("GPH"), findUnitsIndexByNameOrSymbol("gal/h"));
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("KM"), findUnitsIndexByNameOrSymbol("km"));
    TEST_ASSERT_EQUAL_UINT8(findUnitsIndexByName("PSI"), findUnitsIndexByNameOrSymbol("psi"));
    TEST_ASSERT_EQUAL_UINT8(UNITS_NOT_FOUND, findUnitsIndexByNameOrSymbol("furlongs"));
    TEST_ASSERT_EQUAL_UINT8(UNITS_NOT_FOUND, findUnitsIndexByNameOrSymbol(nullptr));
    // No symbol may name another unit: an exported symbol reads back as itself
    for (uint8_t i = 0; i < NUM_UNITS; i++) {
        char sym[16];
        strncpy_P(sym, (const char*)pgm_read_ptr(&UNITS_REGISTRY[i].symbol), sizeof(sym));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(i, findUnitsIndexByNameOrSymbol(sym), sym);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_row_is_well_formed);
    RUN_TEST(test_conversion_to_base);
    RUN_TEST(test_audited_rows);
    RUN_TEST(test_multi_frame_rows);
    RUN_TEST(test_pid_sensors_well_formed);
    RUN_TEST(test_pid_sensor_offsets);
    RUN_TEST(test_new_units);
    RUN_TEST(test_symbol_lookup);
    return UNITY_END();
}
