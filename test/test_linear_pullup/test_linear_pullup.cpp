// Native unit test: the linear driver's internal pull-up (#684).
//
// A preset with enable_pullup gets INPUT_PULLUP only with no divider. Through
// a divider the pull-up and the lower leg would hold an open sensor's pin
// mid-scale (a plausible reading instead of NaN) and skew every reading;
// there the lower leg pulls an open pin to 0 V, which already reads NaN.
#include <unity.h>
#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "../../src/sensors/drivers/linear/linear_sensor.cpp"
#include "../../src/sensors/sensor_calibration_data/generic/generic_calibrations.h"

// The ADC count the next read returns
static int nextAdc = 0;
int readAnalogPin(int, bool* isValid) { *isValid = true; return nextAdc; }

void setUp(void) { memset(stubPinMode, 0, sizeof(stubPinMode)); }
void tearDown(void) {}

static Input presetInput(float divider) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.pin = 14;
    in.calibrationType = CAL_LINEAR;
    in.presetCalibration = &generic_pressure_150psi_cal;   // enable_pullup = true
    in.divider_ratio = divider;
    return in;
}

static int adcFor(float pinVolts) { return (int)(pinVolts / AREF_VOLTAGE * ADC_MAX_VALUE + 0.5f); }

static void test_pullup_without_divider(void) {
    Input in = presetInput(1.0f);
    nextAdc = adcFor(2.5f);
    readLinearSensor(&in);
    TEST_ASSERT_EQUAL_UINT8(INPUT_PULLUP, stubPinMode[14]);
    TEST_ASSERT_TRUE(isfinite(in.value));
    Input legacy = presetInput(0.0f);   // 0 = no divider (old EEPROM data)
    readLinearSensor(&legacy);
    TEST_ASSERT_EQUAL_UINT8(INPUT_PULLUP, stubPinMode[14]);
}

static void test_no_pullup_through_divider(void) {
    Input in = presetInput(0.654f);
    nextAdc = adcFor(2.5f * 0.654f);
    readLinearSensor(&in);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[14]);
    TEST_ASSERT_TRUE(isfinite(in.value));
}

// Open sensor behind a divider: the lower leg pulls the pin to 0 V -> NaN
static void test_open_sensor_through_divider_is_nan(void) {
    Input in = presetInput(0.654f);
    nextAdc = 0;
    readLinearSensor(&in);
    TEST_ASSERT_TRUE(isnan(in.value));
}

// A custom calibration never had the pull-up; still none
static void test_custom_cal_no_pullup(void) {
    Input in = presetInput(1.0f);
    in.flags.useCustomCalibration = 1;
    in.customCalibration.pressureLinear.voltage_min = 0.5f;
    in.customCalibration.pressureLinear.voltage_max = 4.5f;
    in.customCalibration.pressureLinear.output_min = 0.0f;
    in.customCalibration.pressureLinear.output_max = 10.0f;
    nextAdc = adcFor(2.5f);
    readLinearSensor(&in);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[14]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pullup_without_divider);
    RUN_TEST(test_no_pullup_through_divider);
    RUN_TEST(test_open_sensor_through_divider_is_nan);
    RUN_TEST(test_custom_cal_no_pullup);
    return UNITY_END();
}
