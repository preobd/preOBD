/*
 * pressure.h - Pressure Sensors
 *
 * Linear, polynomial, and table-based pressure sensors.
 * Includes generic 0.5-4.5V sensors, AEM, NXP/Freescale, and VDO pressure senders.
 */

#ifndef SENSOR_LIBRARY_SENSORS_PRESSURE_H
#define SENSOR_LIBRARY_SENSORS_PRESSURE_H

#include <Arduino.h>

// ===== PROGMEM STRINGS =====
// Linear pressure sensors
static const char PSTR_GENERIC_BOOST[] PROGMEM = "GENERIC_BOOST";
static const char PSTR_GENERIC_BOOST_LABEL[] PROGMEM = "0.5-4.5V linear (0-5 bar)";
static const char PSTR_GENERIC_PRESSURE_150PSI[] PROGMEM = "GENERIC_PRESSURE_150PSI";
static const char PSTR_GENERIC_PRESSURE_150PSI_LABEL[] PROGMEM = "0.5-4.5V linear (0-150 PSI / 10 bar)";
static const char PSTR_AEM_30_2130_150[] PROGMEM = "AEM_30_2130_150";
static const char PSTR_AEM_30_2130_150_LABEL[] PROGMEM = "AEM 150 PSI (0-150 PSI / 10 bar)";
static const char PSTR_MPX4250AP[] PROGMEM = "MPX4250AP";
static const char PSTR_MPX4250AP_LABEL[] PROGMEM = "Freescale/NXP (20-250 kPa)";
static const char PSTR_MPX5700AP[] PROGMEM = "MPX5700AP";
static const char PSTR_MPX5700AP_LABEL[] PROGMEM = "Freescale/NXP (15-700 kPa)";

// VDO polynomial (curve fit)
static const char PSTR_VDO_2BAR_CURVE[] PROGMEM = "VDO_2BAR_CURVE";
static const char PSTR_VDO_2BAR_CURVE_LABEL[] PROGMEM = "VDO 2 Bar (curve fit)";
static const char PSTR_VDO_5BAR_CURVE[] PROGMEM = "VDO_5BAR_CURVE";
static const char PSTR_VDO_5BAR_CURVE_LABEL[] PROGMEM = "VDO 5 Bar (curve fit)";

// VDO table-based
static const char PSTR_VDO_2BAR_TABLE[] PROGMEM = "VDO_2BAR_TABLE";
static const char PSTR_VDO_2BAR_TABLE_LABEL[] PROGMEM = "VDO 2 Bar (table)";
static const char PSTR_VDO_5BAR_TABLE[] PROGMEM = "VDO_5BAR_TABLE";
static const char PSTR_VDO_5BAR_TABLE_LABEL[] PROGMEM = "VDO 5 Bar (table)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define PRESSURE_SENSORS \
    /* Linear pressure sensors */ \
    X_SENSOR(PSTR_GENERIC_BOOST, PSTR_GENERIC_BOOST_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &generic_boost_linear_cal, SENSOR_READ_INTERVAL_MS, -1.0, 3.0, 0x59C8, PIN_ANALOG) \
    X_SENSOR(PSTR_GENERIC_PRESSURE_150PSI, PSTR_GENERIC_PRESSURE_150PSI_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &generic_pressure_150psi_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10.34, 0xA67B, PIN_ANALOG) \
    X_SENSOR(PSTR_AEM_30_2130_150, PSTR_AEM_30_2130_150_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &aem_30_2130_150_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10.34, 0x31B4, PIN_ANALOG) \
    X_SENSOR(PSTR_MPX4250AP, PSTR_MPX4250AP_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &mpx4250ap_linear_cal, SENSOR_READ_INTERVAL_MS, 0.2, 2.5, 0xDF76, PIN_ANALOG) \
    X_SENSOR(PSTR_MPX5700AP, PSTR_MPX5700AP_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &mpx5700ap_linear_cal, SENSOR_READ_INTERVAL_MS, 0.15, 7.0, 0xC4B7, PIN_ANALOG) \
    /* VDO polynomial (curve fit) */ \
    X_SENSOR(PSTR_VDO_2BAR_CURVE, PSTR_VDO_2BAR_CURVE_LABEL, nullptr, readPressurePolynomial, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_POLYNOMIAL, &vdo2bar_polynomial_cal, SENSOR_READ_INTERVAL_MS, 0.0, 2.0, 0x6FB8, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_5BAR_CURVE, PSTR_VDO_5BAR_CURVE_LABEL, nullptr, readPressurePolynomial, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_POLYNOMIAL, &vdo5bar_polynomial_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.0, 0x231B, PIN_ANALOG) \
    /* VDO table-based */ \
    X_SENSOR(PSTR_VDO_2BAR_TABLE, PSTR_VDO_2BAR_TABLE_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo2bar_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 2.0, 0xD35B, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_5BAR_TABLE, PSTR_VDO_5BAR_TABLE_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo5bar_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.0, 0x86BE, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_PRESSURE_H
