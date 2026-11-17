// Native unit test: absolute vs gauge pressure (#639).
//
// Pins every sensor row's reference by name, so a new pressure row cannot
// arrive without one and a reference cannot change unnoticed, and the
// barometric reference the conversion and the OBD export share
// (src/inputs/pressure_reference.cpp).
#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "../../src/sensors/sensor_library.cpp"
#include "../../src/sensors/sensor_library/sensor_categories.cpp"
#include "../../src/sensors/units_registry.cpp"
#include "../../src/sensors/application_presets.cpp"
#include "../../src/inputs/measurement_compat.cpp"
#include "../../src/inputs/pressure_reference.cpp"

#include <math.h>

Input inputs[MAX_INPUTS];

// Sensor read/init entry points referenced by SENSOR_LIBRARY[] as data
#define STUB_READ(fn) void fn(Input*) {}
STUB_READ(readMAX6675)          STUB_READ(readMAX31855)
STUB_READ(readThermistorSteinhart) STUB_READ(readThermistorLookup) STUB_READ(readThermistorBeta)
STUB_READ(readLinearSensor)     STUB_READ(readPressureTable)
STUB_READ(readPressureTableDescending) STUB_READ(readPressurePolynomial)
STUB_READ(readVoltageDivider)   STUB_READ(readLevelTable)
STUB_READ(readDigitalFloatSwitch) STUB_READ(readHallSpeed)
STUB_READ(readWPhaseRPM)        STUB_READ(readBME280Temp)
STUB_READ(readBME280Pressure)   STUB_READ(readBME280Humidity)
STUB_READ(readBME280Elevation)
STUB_READ(initThermocoupleCS)   STUB_READ(initFloatSwitch)
STUB_READ(initHallSpeed)        STUB_READ(initWPhaseRPM)
STUB_READ(initBME280)
#undef STUB_READ

void setUp(void) {
    memset(inputs, 0, sizeof(inputs));
    for (uint8_t i = 0; i < MAX_INPUTS; i++) inputs[i].pin = 0xFF;
}
void tearDown(void) {}

// The only absolute sensors in the library. Datasheets: the MPX "AP" parts
// have a sealed vacuum reference; BME280 is a barometer. Every other pressure
// row is a sender or transducer vented to (or sealed at) atmosphere.
static const char* const ABSOLUTE_SENSORS[] = {"MPX4250AP", "MPX5700AP", "BME280_PRESSURE"};

static bool namedAbsolute(const char* name) {
    for (const char* a : ABSOLUTE_SENSORS)
        if (strcmp(a, name) == 0) return true;
    return false;
}

static void test_every_row_has_its_reference(void) {
    uint8_t pressure = 0, absolute = 0;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        const char* name = getSensorNameByIndex(i);
        TEST_ASSERT_NOT_NULL(name);
        PressureReference ref = getSensorReference(i);
        if (getSensorMeasurementType(i) == MEASURE_PRESSURE && strcmp(name, "CAN_IMPORT") != 0) {
            pressure++;
            PressureReference want = namedAbsolute(name) ? REF_ABSOLUTE : REF_GAUGE;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(want, ref, name);
            if (ref == REF_ABSOLUTE) absolute++;
        } else {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(REF_NONE, ref, name);   // not a pressure, or its PID decides
        }
    }
    TEST_ASSERT_EQUAL_UINT8(3, absolute);   // each named sensor really is in the library
    TEST_ASSERT_TRUE(pressure > 10);
}

static void test_out_of_range_index(void) {
    TEST_ASSERT_EQUAL_UINT8(REF_NONE, getSensorReference(NUM_SENSORS));
    TEST_ASSERT_EQUAL_UINT8(REF_NONE, getSensorReference(0xFF));
}

static void addBaro(uint8_t slot, uint8_t pin, float bar) {
    inputs[slot].pin = pin;
    inputs[slot].flags.isEnabled = true;
    inputs[slot].applicationIndex = APP_BAROMETRIC_PRESSURE;
    inputs[slot].value = bar;
}

// Standard atmosphere with no baro input
static void test_baro_default(void) {
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
}

// The lowest pin wins, whatever the slot (#636)
static void test_baro_lowest_pin(void) {
    addBaro(0, 0xC1, 0.98f);   // CAN import
    TEST_ASSERT_EQUAL_FLOAT(0.98f, baroReferenceBar());
    addBaro(3, 7, 1.01f);      // wired
    TEST_ASSERT_EQUAL_FLOAT(1.01f, baroReferenceBar());
}

// A faulted or out-of-range baro input holds the last good reading while a
// baro input is configured (decision 5A); a disabled one counts as removed
static void test_baro_faults_hold_last_good(void) {
    addBaro(0, 5, 0.95f);
    TEST_ASSERT_EQUAL_FLOAT(0.95f, baroReferenceBar());
    inputs[0].value = NAN;
    TEST_ASSERT_EQUAL_FLOAT(0.95f, baroReferenceBar());
    inputs[0].value = 0.3f;
    TEST_ASSERT_EQUAL_FLOAT(0.95f, baroReferenceBar());
    inputs[0].value = 1.5f;
    TEST_ASSERT_EQUAL_FLOAT(0.95f, baroReferenceBar());
    addBaro(1, 9, 0.97f);      // a good one behind a bad lower pin
    TEST_ASSERT_EQUAL_FLOAT(0.97f, baroReferenceBar());
    inputs[0].flags.isEnabled = false;
    inputs[1].pin = 0xFF;      // every baro input gone: back to standard
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
}

static void test_application_reference(void) {
    TEST_ASSERT_EQUAL_UINT8(REF_ABSOLUTE, applicationReference(APP_MAP_PRESSURE));
    TEST_ASSERT_EQUAL_UINT8(REF_ABSOLUTE, applicationReference(APP_BAROMETRIC_PRESSURE));
    TEST_ASSERT_EQUAL_UINT8(REF_GAUGE, applicationReference(APP_BOOST_PRESSURE));
    TEST_ASSERT_EQUAL_UINT8(REF_GAUGE, applicationReference(APP_OIL_PRESSURE));
    TEST_ASSERT_EQUAL_UINT8(REF_NONE, applicationReference(APP_NONE));
    TEST_ASSERT_EQUAL_UINT8(REF_NONE, applicationReference(APP_COOLANT_TEMP));
    // Every pressure application has a reference, every other none
    for (uint8_t a = 1; a < NUM_APPLICATION_PRESETS; a++) {
        bool pressure = getApplicationExpectedMeasurementType(a) == MEASURE_PRESSURE;
        TEST_ASSERT_EQUAL_MESSAGE(pressure, applicationReference(a) != REF_NONE,
                                  getApplicationNameByIndex(a));
    }
}

static void test_offset_is_pure(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.9f, pressureReferenceOffset(REF_GAUGE, REF_ABSOLUTE, 0.9f));
    TEST_ASSERT_EQUAL_FLOAT(-0.9f, pressureReferenceOffset(REF_ABSOLUTE, REF_GAUGE, 0.9f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pressureReferenceOffset(REF_GAUGE, REF_GAUGE, 0.9f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pressureReferenceOffset(REF_ABSOLUTE, REF_ABSOLUTE, 0.9f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pressureReferenceOffset(REF_NONE, REF_ABSOLUTE, 0.9f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pressureReferenceOffset(REF_GAUGE, REF_NONE, 0.9f));
}

// A wired pressure input on `sensor` + `app`, reading `raw` from its driver
static float readThrough(const char* sensor, uint8_t app, float raw) {
    uint8_t s = getSensorIndexByName(sensor);
    TEST_ASSERT_NOT_EQUAL(0xFF, s);
    Input& in = inputs[2];
    in.pin = 14; in.flags.isEnabled = true;
    in.sensorIndex = s; in.applicationIndex = app;
    in.measurementType = MEASURE_PRESSURE;
    in.readFunction = (void (*)(Input*))pgm_read_ptr(&SENSOR_LIBRARY[s].readFunction);
    in.value = raw;            // the stub driver leaves it: this is "the reading"
    readInputNow(in);
    TEST_ASSERT_TRUE(in.flags.hasReading);
    return in.value;
}

// The four cases from the plan, at rest, with a baro input at 0.95 bar
static void test_cases_end_to_end(void) {
    addBaro(0, 5, 0.95f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, readThrough("VDO_2BAR_CURVE", APP_BOOST_PRESSURE, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, readThrough("MPX4250AP", APP_BOOST_PRESSURE, 0.95f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.95f, readThrough("MPX4250AP", APP_MAP_PRESSURE, 0.95f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.95f, readThrough("VDO_2BAR_CURVE", APP_MAP_PRESSURE, 0.0f));   // gauge on MAP
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, readThrough("MPX5700AP", APP_OIL_PRESSURE, 0.95f));      // engine off
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.6f, readThrough("MPX4250AP", APP_BOOST_PRESSURE, 0.35f));   // vacuum
    TEST_ASSERT_TRUE(isnan(readThrough("MPX4250AP", APP_BOOST_PRESSURE, NAN)));                    // a fault stays a fault
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.95f, readThrough("MPX4250AP", APP_NONE, 0.95f));             // no application: as read
}

// Injected and test-mode values are already application values (decision 3A)
static void readStub(Input* in) { in->value = 0.5f; }
static void test_injected_not_converted(void) {
    addBaro(0, 5, 0.95f);
    Input& in = inputs[2];
    in.pin = 14; in.flags.isEnabled = true;
    in.sensorIndex = getSensorIndexByName("MPX4250AP");
    in.applicationIndex = APP_BOOST_PRESSURE;
    in.measurementType = MEASURE_PRESSURE;
    in.readFunction = readStub;
    readInputNow(in);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, in.value);
}

// The barometric input is never converted against itself
static void test_baro_input_not_converted(void) {
    Input& in = inputs[2];
    uint8_t s = getSensorIndexByName("VDO_2BAR_CURVE");   // nonsense, but must not self-reference
    in.pin = 14; in.flags.isEnabled = true;
    in.sensorIndex = s; in.applicationIndex = APP_BAROMETRIC_PRESSURE;
    in.measurementType = MEASURE_PRESSURE;
    in.readFunction = (void (*)(Input*))pgm_read_ptr(&SENSOR_LIBRARY[s].readFunction);
    in.value = 0.2f;
    readInputNow(in);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, in.value);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_row_has_its_reference);
    RUN_TEST(test_out_of_range_index);
    RUN_TEST(test_baro_default);
    RUN_TEST(test_baro_lowest_pin);
    RUN_TEST(test_baro_faults_hold_last_good);
    RUN_TEST(test_application_reference);
    RUN_TEST(test_offset_is_pure);
    RUN_TEST(test_cases_end_to_end);
    RUN_TEST(test_injected_not_converted);
    RUN_TEST(test_baro_input_not_converted);
    return UNITY_END();
}
