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
// Generic linear sensors (pressure and other analog outputs)
static const char PSTR_GENERIC_TPS[] PROGMEM = "GENERIC_TPS";
static const char PSTR_GENERIC_TPS_LABEL[] PROGMEM = "0.5-4.5V linear (0-100% throttle)";
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

// Smiths (British classics)
static const char PSTR_SMITHS_OIL_PRESSURE_BP[] PROGMEM = "SMITHS_OIL_PRESSURE_BP";
static const char PSTR_SMITHS_OIL_PRESSURE_BP_LABEL[] PROGMEM = "Smiths BP/ACP Oil Pressure Sender (0-80 PSI)";
static const char PSTR_SMITHS_OIL_PRESSURE_BP_DESC[] PROGMEM = "Smiths BP/ACP oil sender (MG/Triumph). Endpoints only; linear interp, \xC2\xB1""10% FS accuracy.";

// Stewart-Warner
static const char PSTR_SW_OIL_PRESSURE[] PROGMEM = "SW_OIL_PRESSURE";
static const char PSTR_SW_OIL_PRESSURE_LABEL[] PROGMEM = "Stewart-Warner Oil Pressure Sender (0-80 PSI)";
static const char PSTR_SW_OIL_PRESSURE_DESC[] PROGMEM = "Stewart-Warner oil pressure sender, 33.5-240\xCE\xA9. Endpoints only; linear interp, \xC2\xB1""10% FS.";

// Jeep/AMC
static const char PSTR_JEEP_4_0_OIL_GAUGE[] PROGMEM = "JEEP_4_0_OIL_GAUGE";
static const char PSTR_JEEP_4_0_OIL_GAUGE_LABEL[] PROGMEM = "Jeep 4.0L Oil Pressure Sender (Gauge) (0-80 PSI)";
static const char PSTR_JEEP_CJ_OIL_THIN[] PROGMEM = "JEEP_CJ_OIL_THIN";
static const char PSTR_JEEP_CJ_OIL_THIN_LABEL[] PROGMEM = "Jeep CJ Oil Pressure Sender, 1\" Thin Gauge (0-80 PSI)";
static const char PSTR_JEEP_CJ_OIL_DEEP[] PROGMEM = "JEEP_CJ_OIL_DEEP";
static const char PSTR_JEEP_CJ_OIL_DEEP_LABEL[] PROGMEM = "Jeep CJ Oil Pressure Sender, 2\" Deep Gauge (0-80 PSI)";

// VDO table-based
static const char PSTR_VDO_2BAR_TABLE[] PROGMEM = "VDO_2BAR_TABLE";
static const char PSTR_VDO_2BAR_TABLE_LABEL[] PROGMEM = "VDO 2 Bar (table)";
static const char PSTR_VDO_5BAR_TABLE[] PROGMEM = "VDO_5BAR_TABLE";
static const char PSTR_VDO_5BAR_TABLE_LABEL[] PROGMEM = "VDO 5 Bar (table)";
static const char PSTR_VDO_10BAR_TABLE[] PROGMEM = "VDO_10BAR_TABLE";
static const char PSTR_VDO_10BAR_TABLE_LABEL[] PROGMEM = "VDO 10 Bar (table)";
static const char PSTR_VDO_10BAR_CURVE[] PROGMEM = "VDO_10BAR_CURVE";
static const char PSTR_VDO_10BAR_CURVE_LABEL[] PROGMEM = "VDO 10 Bar (curve fit)";
static const char PSTR_VDO_25BAR_TABLE[] PROGMEM = "VDO_25BAR_TABLE";
static const char PSTR_VDO_25BAR_TABLE_LABEL[] PROGMEM = "VDO 25 Bar Gear Oil (table)";
static const char PSTR_VDO_80PSI_TABLE[] PROGMEM = "VDO_80PSI_TABLE";
static const char PSTR_VDO_80PSI_TABLE_LABEL[] PROGMEM = "VDO 80 PSI Oil (US, descending, table)";

// ===== SENSOR ENTRIES (X-MACRO) =====
// X_SENSOR(name, label, description, readFunc, initFunc, measType, calType, defaultCal, minInterval, minVal, maxVal, hash, pinType)
#define PRESSURE_SENSORS \
    /* Linear pressure sensors */ \
    X_SENSOR(PSTR_GENERIC_BOOST, PSTR_GENERIC_BOOST_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &generic_boost_linear_cal, SENSOR_READ_INTERVAL_MS, -1.0, 3.0, 0x59C8, PIN_ANALOG) \
    X_SENSOR(PSTR_GENERIC_PRESSURE_150PSI, PSTR_GENERIC_PRESSURE_150PSI_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_PRESSURE, CAL_LINEAR, &generic_pressure_150psi_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10.34, 0xA67B, PIN_ANALOG) \
    X_SENSOR(PSTR_GENERIC_TPS, PSTR_GENERIC_TPS_LABEL, nullptr, readLinearSensor, nullptr, \
             MEASURE_LEVEL, CAL_LINEAR, &generic_tps_linear_cal, SENSOR_READ_INTERVAL_MS, 0.0, 100.0, 0xF738, PIN_ANALOG) \
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
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo5bar_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.0, 0x86BE, PIN_ANALOG) \
    /* VDO extended range table-based */ \
    X_SENSOR(PSTR_VDO_10BAR_TABLE, PSTR_VDO_10BAR_TABLE_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo10bar_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10.0, 0x682A, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_10BAR_CURVE, PSTR_VDO_10BAR_CURVE_LABEL, nullptr, readPressurePolynomial, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_POLYNOMIAL, &vdo10bar_polynomial_cal, SENSOR_READ_INTERVAL_MS, 0.0, 10.0, 0x0487, PIN_ANALOG) \
    X_SENSOR(PSTR_VDO_25BAR_TABLE, PSTR_VDO_25BAR_TABLE_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo25bar_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 25.0, 0xF310, PIN_ANALOG) \
    /* VDO US-market descending pressure */ \
    X_SENSOR(PSTR_VDO_80PSI_TABLE, PSTR_VDO_80PSI_TABLE_LABEL, nullptr, readPressureTableDescending, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &vdo80psi_table_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0x6008, PIN_ANALOG) \
    /* Smiths (British classics) */ \
    X_SENSOR(PSTR_SMITHS_OIL_PRESSURE_BP, PSTR_SMITHS_OIL_PRESSURE_BP_LABEL, PSTR_SMITHS_OIL_PRESSURE_BP_DESC, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &smiths_oil_bp_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0x0D29, PIN_ANALOG) \
    /* Stewart-Warner */ \
    X_SENSOR(PSTR_SW_OIL_PRESSURE, PSTR_SW_OIL_PRESSURE_LABEL, PSTR_SW_OIL_PRESSURE_DESC, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &sw_oil_pressure_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0x436A, PIN_ANALOG) \
    /* Jeep/AMC */ \
    X_SENSOR(PSTR_JEEP_4_0_OIL_GAUGE, PSTR_JEEP_4_0_OIL_GAUGE_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &jeep40_oil_gauge_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0x0156, PIN_ANALOG) \
    X_SENSOR(PSTR_JEEP_CJ_OIL_THIN, PSTR_JEEP_CJ_OIL_THIN_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &jeep_cj_oil_thin_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0xF9AA, PIN_ANALOG) \
    X_SENSOR(PSTR_JEEP_CJ_OIL_DEEP, PSTR_JEEP_CJ_OIL_DEEP_LABEL, nullptr, readPressureTable, nullptr, \
             MEASURE_PRESSURE, CAL_PRESSURE_TABLE, &jeep_cj_oil_deep_cal, SENSOR_READ_INTERVAL_MS, 0.0, 5.52, 0x2655, PIN_ANALOG)

#endif // SENSOR_LIBRARY_SENSORS_PRESSURE_H
