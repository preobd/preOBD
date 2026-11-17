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

// A faulted, out-of-range or disabled baro input is skipped; never NaN
static void test_baro_faults_fall_through(void) {
    addBaro(0, 5, NAN);
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
    addBaro(0, 5, 0.3f);
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
    addBaro(0, 5, 1.5f);
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
    addBaro(0, 5, 0.95f);
    inputs[0].flags.isEnabled = false;
    TEST_ASSERT_EQUAL_FLOAT(STD_BARO_BAR, baroReferenceBar());
    addBaro(1, 9, 0.97f);      // a good one behind a bad lower pin
    addBaro(0, 5, NAN);
    TEST_ASSERT_EQUAL_FLOAT(0.97f, baroReferenceBar());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_row_has_its_reference);
    RUN_TEST(test_out_of_range_index);
    RUN_TEST(test_baro_default);
    RUN_TEST(test_baro_lowest_pin);
    RUN_TEST(test_baro_faults_fall_through);
    return UNITY_END();
}
