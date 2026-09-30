// Native unit test: OBD-II output encoding by PID (src/outputs/obd_encode.cpp,
// src/sensors/can_sensor_library/obd_output_pids.cpp, #516).
//
// The output chose its encoding by measurement type, so standard PIDs went
// out in preOBD's own format (RPM as rpm/4, MAP as bar*10) and standard tools
// misread them. It now encodes by PID. Pinned here: every J1979 row encodes
// as the exact inverse of how SET CAN decodes it; exact bytes for the PIDs
// tools read most; the multi-frame layouts; clamping; the type gate; that
// every application answers on a table PID of its own type.
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/sensors/can_sensor_library/standard_pids.cpp"
#include "../../src/sensors/can_sensor_library/obd_output_pids.cpp"
#include "../../src/outputs/obd_encode.cpp"

void setUp(void) {}
void tearDown(void) {}

static ObdOutputPid spec(uint8_t pid) {
    ObdOutputPid s;
    TEST_ASSERT_TRUE_MESSAGE(obdOutputSpec(pid, s), "PID has no output spec");
    return s;
}

static uint32_t rawAt(const uint8_t* p, const ObdOutputPid& s) {
    uint32_t r = 0;
    for (uint8_t i = 0; i < s.valueLength; i++) r = (r << 8) | p[s.valueOffset + i];
    return r;
}

// base value (degC, bar ...) whose encoding should be exactly `raw`
static float baseForRaw(const ObdOutputPid& s, uint32_t raw) {
    float v = raw * s.scale + s.offset;     // in the PID's unit
    if (!s.units) return v;
    char name[16];
    strncpy_P(name, s.units, sizeof(name));
    uint8_t u = findUnitsIndexByName(name);
    return (v - pgm_read_float(&UNITS_REGISTRY[u].conversionOffset)) /
           pgm_read_float(&UNITS_REGISTRY[u].conversionFactor);
}

// Every single-value J1979 row and every output-table row: a value that
// decodes to raw R encodes back to exactly R (the import decode, inverted)
static void test_every_row_round_trips(void) {
    uint8_t payload[OBD_OUT_MAX_LENGTH];
    for (uint16_t pid = 1; pid <= 0xFF; pid++) {
        ObdOutputPid s;
        if (!obdOutputSpec((uint8_t)pid, s)) continue;
        char msg[24];
        snprintf(msg, sizeof(msg), "PID 0x%02X", (unsigned)pid);
        const uint32_t probes[] = {1, 100, s.valueLength == 1 ? 200u : 12345u};
        for (uint32_t raw : probes) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(s.length, obdEncodeValue(s, baseForRaw(s, raw), false, 0.0f, payload), msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(raw, rawAt(payload, s), msg);
        }
    }
}

// The bytes a standard tool expects, for the PIDs tools read most
static void test_exact_bytes(void) {
    uint8_t p[OBD_OUT_MAX_LENGTH];
    TEST_ASSERT_EQUAL_UINT8(2, obdEncodeValue(spec(0x0C), 3000.0f, false, 0, p));   // RPM x4 (was /4)
    TEST_ASSERT_EQUAL_HEX8(0x2E, p[0]); TEST_ASSERT_EQUAL_HEX8(0xE0, p[1]);
    obdEncodeValue(spec(0x0B), 1.013f, false, 0, p);                                // MAP kPa (was bar*10)
    TEST_ASSERT_EQUAL_UINT8(101, p[0]);
    obdEncodeValue(spec(0x33), 0.98f, false, 0, p);                                 // baro kPa
    TEST_ASSERT_EQUAL_UINT8(98, p[0]);
    obdEncodeValue(spec(0x0A), 3.0f, false, 0, p);                                  // fuel pressure 3A kPa
    TEST_ASSERT_EQUAL_UINT8(100, p[0]);
    TEST_ASSERT_EQUAL_UINT8(2, obdEncodeValue(spec(0x42), 12.6f, false, 0, p));     // V x1000 (was 1 byte x10)
    TEST_ASSERT_EQUAL_HEX8(0x31, p[0]); TEST_ASSERT_EQUAL_HEX8(0x38, p[1]);
    obdEncodeValue(spec(0x05), 90.0f, false, 0, p);                                 // A-40
    TEST_ASSERT_EQUAL_UINT8(130, p[0]);
    obdEncodeValue(spec(0x2F), 60.0f, false, 0, p);                                 // 100A/255
    TEST_ASSERT_EQUAL_UINT8(153, p[0]);
}

// Multi-frame J1979 PIDs: support byte, value position, length
static void test_boost_and_egt(void) {
    uint8_t p[OBD_OUT_MAX_LENGTH];
    // 1.0 bar boost (gauge) + 1.01325 bar baro = 201.325 kPa abs, /32 steps
    TEST_ASSERT_EQUAL_UINT8(10, obdEncodeValue(spec(0x70), 1.0f, true, 1.01325f, p));
    TEST_ASSERT_EQUAL_HEX8(0x02, p[0]);                        // boost sensor A supported
    TEST_ASSERT_EQUAL_HEX8(0, p[1]); TEST_ASSERT_EQUAL_HEX8(0, p[2]);   // no commanded boost
    TEST_ASSERT_EQUAL_UINT32(6442, ((uint32_t)p[3] << 8) | p[4]);        // D-E
    TEST_ASSERT_EQUAL_HEX8(0, p[9]);                           // J: no control status
    // A MAP input (absolute already) on 0x70: no baro added
    obdEncodeValue(spec(0x70), 2.0f, false, 1.01325f, p);
    TEST_ASSERT_EQUAL_UINT32(6400, ((uint32_t)p[3] << 8) | p[4]);
    // Boost on 0x0B (absolute MAP): gauge + baro
    obdEncodeValue(spec(0x0B), 0.5f, true, 1.0f, p);
    TEST_ASSERT_EQUAL_UINT8(150, p[0]);
    // EGT 850 C on 0x78: A = sensor 1, B-C = (T+40)*10
    TEST_ASSERT_EQUAL_UINT8(9, obdEncodeValue(spec(0x78), 850.0f, false, 0, p));
    TEST_ASSERT_EQUAL_HEX8(0x01, p[0]);
    TEST_ASSERT_EQUAL_UINT32(8900, ((uint32_t)p[1] << 8) | p[2]);
    TEST_ASSERT_EQUAL_HEX8(0, p[3]);
}

// preOBD block formulas as documented in OBD2_PID_REFERENCE.md
static void test_preobd_block(void) {
    uint8_t p[OBD_OUT_MAX_LENGTH];
    obdEncodeValue(spec(0xAA), 260.0f, false, 0, p);          // CHT above the old 1-byte 215 C cap
    TEST_ASSERT_EQUAL_UINT32(3000, ((uint32_t)p[0] << 8) | p[1]);
    obdEncodeValue(spec(0xB0), 4.0f, false, 0, p);            // oil pressure 4 bar = 400 kPa, +100
    TEST_ASSERT_EQUAL_UINT32(500, ((uint32_t)p[0] << 8) | p[1]);
    obdEncodeValue(spec(0xB2), -0.5f, false, 0, p);           // vacuum -50 kPa
    TEST_ASSERT_EQUAL_UINT32(50, ((uint32_t)p[0] << 8) | p[1]);
    obdEncodeValue(spec(0xB6), 1.0f, false, 0, p);            // level switch on
    TEST_ASSERT_EQUAL_HEX8(0xFF, p[0]);
    obdEncodeValue(spec(0xB6), 0.0f, false, 0, p);
    TEST_ASSERT_EQUAL_HEX8(0x00, p[0]);
    obdEncodeValue(spec(0xB5), 250.0f, false, 0, p);          // altitude +500
    TEST_ASSERT_EQUAL_UINT32(750, ((uint32_t)p[0] << 8) | p[1]);
    for (uint8_t i = 0; i < NUM_OBD_OUTPUT_PIDS; i++) {
        ObdOutputPid s;
        memcpy_P(&s, &OBD_OUTPUT_TABLE[i], sizeof(s));
        if (s.flags & OBD_OUT_PREOBD) {
            TEST_ASSERT_TRUE(s.pid >= 0xAA && s.pid <= 0xBF);          // J1979's unassigned block
            TEST_ASSERT_NULL(lookupStandardPID(s.pid));
        }
        TEST_ASSERT_TRUE(s.length <= OBD_OUT_MAX_LENGTH);
        if (i) TEST_ASSERT_TRUE(pgm_read_byte(&OBD_OUTPUT_TABLE[i - 1].pid) < s.pid);
    }
}

static void test_clamping(void) {
    uint8_t p[OBD_OUT_MAX_LENGTH];
    obdEncodeValue(spec(0x05), -60.0f, false, 0, p);          // below the -40 floor
    TEST_ASSERT_EQUAL_UINT8(0, p[0]);
    obdEncodeValue(spec(0x05), 400.0f, false, 0, p);          // above 215
    TEST_ASSERT_EQUAL_UINT8(255, p[0]);
    obdEncodeValue(spec(0x0C), 99999.0f, false, 0, p);
    TEST_ASSERT_EQUAL_HEX8(0xFF, p[0]); TEST_ASSERT_EQUAL_HEX8(0xFF, p[1]);
    TEST_ASSERT_EQUAL_UINT8(0, obdEncodeValue(spec(0x05), NAN, false, 0, p));
    TEST_ASSERT_EQUAL_UINT8(0, obdEncodeValue(spec(0x05), INFINITY, false, 0, p));
}

// A J1979 PID carries one quantity: another type is not answered
static void test_type_gate(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.obd2pid = 0x0C;
    in.measurementType = MEASURE_TEMPERATURE;
    TEST_ASSERT_FALSE(obdInputAnswerable(in));
    in.measurementType = MEASURE_RPM;
    TEST_ASSERT_TRUE(obdInputAnswerable(in));
    in.obd2pid = 0xB6;                       // level switch PID
    in.measurementType = MEASURE_PERCENT;    // a percentage is not a switch
    TEST_ASSERT_FALSE(obdInputAnswerable(in));
    in.obd2pid = 0xE5;                       // user's own PID: legacy, needs a length
    in.obd2length = 0;
    TEST_ASSERT_FALSE(obdInputAnswerable(in));
    in.obd2length = 2;
    TEST_ASSERT_TRUE(obdInputAnswerable(in));
    ObdOutputPid s;
    TEST_ASSERT_FALSE(obdOutputSpec(0x66, s));   // multi-sensor import row: not answered (would truncate)
    TEST_ASSERT_FALSE(obdOutputSpec(0x6F, s));   // turbo inlet pressure: not in either table
}

// Every application answers out of the box on a table PID of its own type,
// with the table's length
static void test_every_preset(void) {
    for (uint8_t a = 1; a < NUM_APPLICATION_PRESETS; a++) {
        const ApplicationPreset* app = getApplicationByIndex(a);
        uint8_t pid = pgm_read_byte(&app->obd2pid);
        char name[24];
        strncpy_P(name, (const char*)pgm_read_ptr(&app->name), sizeof(name));
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, pid, name);
        ObdOutputPid s;
        TEST_ASSERT_TRUE_MESSAGE(obdOutputSpec(pid, s), name);
        TEST_ASSERT_EQUAL_MESSAGE(pgm_read_byte(&app->expectedMeasurementType), s.measurementType, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(s.length, pgm_read_byte(&app->obd2length), name);
    }
}

// Reply length stored with a PID: the table's, else the user's own
static void test_length_for(void) {
    TEST_ASSERT_EQUAL_UINT8(9, obdLengthFor(0x78, 2));    // EGT: J1979 9 bytes
    TEST_ASSERT_EQUAL_UINT8(10, obdLengthFor(0x70, 0));
    TEST_ASSERT_EQUAL_UINT8(3, obdLengthFor(0xE5, 3));    // user's own PID keeps its length
    TEST_ASSERT_EQUAL_UINT8(1, obdLengthFor(0xE5, 0));
    TEST_ASSERT_EQUAL_UINT8(1, obdLengthFor(0xE5, 10));   // from boost 0x70 to a PID of your own
    ObdOutputPid s;
    TEST_ASSERT_FALSE(obdOutputSpec(0xC8, s));             // pre-#516 CHT PID: legacy encoding now
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_row_round_trips);
    RUN_TEST(test_exact_bytes);
    RUN_TEST(test_boost_and_egt);
    RUN_TEST(test_preobd_block);
    RUN_TEST(test_clamping);
    RUN_TEST(test_type_gate);
    RUN_TEST(test_every_preset);
    RUN_TEST(test_length_for);
    return UNITY_END();
}
