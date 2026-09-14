/*
 * application_presets.cpp - Application Type Presets (data)
 *
 * See application_presets.h. The PROGMEM string literals and the
 * APPLICATION_PRESETS[] array itself live here, and only here, so there is
 * exactly one copy in the linked image regardless of how many translation
 * units include the header (#400).
 *
 * Placeholder entries (label = nullptr) reserve slots for unimplemented
 * applications.
 */

#include "application_presets.h"

// ===== STRING LITERALS IN PROGMEM =====
static const char PSTR_APP_NONE[] PROGMEM = "NONE";

static const char PSTR_CHT[] PROGMEM = "CHT";
static const char PSTR_CHT_ABBR[] PROGMEM = "CHT";
static const char PSTR_CHT_LABEL[] PROGMEM = "Cylinder Head Temperature";

static const char PSTR_EGT[] PROGMEM = "EGT";
static const char PSTR_EGT_ABBR[] PROGMEM = "EGT";
static const char PSTR_EGT_LABEL[] PROGMEM = "Exhaust Gas Temperature";

static const char PSTR_COOLANT_TEMP[] PROGMEM = "COOLANT_TEMP";
static const char PSTR_COOLANT_TEMP_ABBR[] PROGMEM = "ECT";
static const char PSTR_COOLANT_TEMP_LABEL[] PROGMEM = "Engine Coolant Temperature";

static const char PSTR_OIL_TEMP[] PROGMEM = "OIL_TEMP";
static const char PSTR_OIL_TEMP_ABBR[] PROGMEM = "EOT";
static const char PSTR_OIL_TEMP_LABEL[] PROGMEM = "Engine Oil Temperature";

static const char PSTR_TCASE_TEMP[] PROGMEM = "TCASE_TEMP";
static const char PSTR_TCASE_TEMP_ABBR[] PROGMEM = "TCFT";
static const char PSTR_TCASE_TEMP_LABEL[] PROGMEM = "Transfer Case Fluid Temperature";

static const char PSTR_AMBIENT_TEMP[] PROGMEM = "AMBIENT_TEMP";
static const char PSTR_AMBIENT_TEMP_ABBR[] PROGMEM = "AAT";
static const char PSTR_AMBIENT_TEMP_LABEL[] PROGMEM = "Ambient Air Temperature";

static const char PSTR_OIL_PRESSURE[] PROGMEM = "OIL_PRESSURE";
static const char PSTR_OIL_PRESSURE_ABBR[] PROGMEM = "EOP";
static const char PSTR_OIL_PRESSURE_LABEL[] PROGMEM = "Engine Oil Pressure";

static const char PSTR_BOOST_PRESSURE[] PROGMEM = "BOOST_PRESSURE";
static const char PSTR_BOOST_PRESSURE_ABBR[] PROGMEM = "BST";
static const char PSTR_BOOST_PRESSURE_LABEL[] PROGMEM = "Boost Pressure";

static const char PSTR_FUEL_PRESSURE[] PROGMEM = "FUEL_PRESSURE";
static const char PSTR_FUEL_PRESSURE_ABBR[] PROGMEM = "FPR";
static const char PSTR_FUEL_PRESSURE_LABEL[] PROGMEM = "Fuel Pressure";

static const char PSTR_BAROMETRIC_PRESSURE[] PROGMEM = "BAROMETRIC_PRESSURE";
static const char PSTR_BAROMETRIC_PRESSURE_ABBR[] PROGMEM = "BARO";
static const char PSTR_BAROMETRIC_PRESSURE_LABEL[] PROGMEM = "Barometric Pressure";

static const char PSTR_PRIMARY_BATTERY[] PROGMEM = "PRIMARY_BATTERY";
static const char PSTR_PRIMARY_BATTERY_ABBR[] PROGMEM = "BAT";
static const char PSTR_PRIMARY_BATTERY_LABEL[] PROGMEM = "Primary Battery Voltage";

static const char PSTR_AUXILIARY_BATTERY[] PROGMEM = "AUXILIARY_BATTERY";
static const char PSTR_AUXILIARY_BATTERY_ABBR[] PROGMEM = "AUX";
static const char PSTR_AUXILIARY_BATTERY_LABEL[] PROGMEM = "Auxiliary Battery Voltage";

static const char PSTR_COOLANT_LEVEL[] PROGMEM = "COOLANT_LEVEL";
static const char PSTR_COOLANT_LEVEL_ABBR[] PROGMEM = "CLVL";
static const char PSTR_COOLANT_LEVEL_LABEL[] PROGMEM = "Engine Coolant Level";

static const char PSTR_HUMIDITY[] PROGMEM = "HUMIDITY";
static const char PSTR_HUMIDITY_ABBR[] PROGMEM = " RH";
static const char PSTR_HUMIDITY_LABEL[] PROGMEM = "Relative Humidity";

static const char PSTR_ELEVATION[] PROGMEM = "ELEVATION";
static const char PSTR_ELEVATION_ABBR[] PROGMEM = "ELEV";
static const char PSTR_ELEVATION_LABEL[] PROGMEM = "Elevation";

static const char PSTR_ENGINE_RPM[] PROGMEM = "ENGINE_RPM";
static const char PSTR_ENGINE_RPM_ABBR[] PROGMEM = "RPM";
static const char PSTR_ENGINE_RPM_LABEL[] PROGMEM = "Engine RPM";

static const char PSTR_VEHICLE_SPEED[] PROGMEM = "VEHICLE_SPEED";
static const char PSTR_VEHICLE_SPEED_ABBR[] PROGMEM = "VSS";
static const char PSTR_VEHICLE_SPEED_LABEL[] PROGMEM = "Vehicle Speed";

static const char PSTR_FUEL_LEVEL[] PROGMEM = "FUEL_LEVEL";
static const char PSTR_FUEL_LEVEL_ABBR[] PROGMEM = "FUEL";
static const char PSTR_FUEL_LEVEL_LABEL[] PROGMEM = "Fuel Tank Level";

static const char PSTR_HYDRAULIC_PRESSURE[] PROGMEM = "HYDRAULIC_PRESSURE";
static const char PSTR_HYDRAULIC_PRESSURE_ABBR[] PROGMEM = "HYP";
static const char PSTR_HYDRAULIC_PRESSURE_LABEL[] PROGMEM = "Hydraulic Pressure";

static const char PSTR_BRAKE_PRESSURE[] PROGMEM = "BRAKE_PRESSURE";
static const char PSTR_BRAKE_PRESSURE_ABBR[] PROGMEM = "BRP";
static const char PSTR_BRAKE_PRESSURE_LABEL[] PROGMEM = "Brake Pressure";

static const char PSTR_TRANS_TEMP[] PROGMEM = "TRANS_TEMP";
static const char PSTR_TRANS_TEMP_ABBR[] PROGMEM = "TFT";
static const char PSTR_TRANS_TEMP_LABEL[] PROGMEM = "Transmission Fluid Temperature";

static const char PSTR_INTAKE_AIR_TEMP[] PROGMEM = "INTAKE_AIR_TEMP";
static const char PSTR_INTAKE_AIR_TEMP_ABBR[] PROGMEM = "IAT";
static const char PSTR_INTAKE_AIR_TEMP_LABEL[] PROGMEM = "Intake Air Temperature";

static const char PSTR_MAP_PRESSURE[] PROGMEM = "MAP_PRESSURE";
static const char PSTR_MAP_PRESSURE_ABBR[] PROGMEM = "MAP";
static const char PSTR_MAP_PRESSURE_LABEL[] PROGMEM = "Manifold Absolute Pressure";

static const char PSTR_THROTTLE_POSITION[] PROGMEM = "THROTTLE_POSITION";
static const char PSTR_THROTTLE_POSITION_ABBR[] PROGMEM = "TPS";
static const char PSTR_THROTTLE_POSITION_LABEL[] PROGMEM = "Throttle Position";

static const char PSTR_DIFF_TEMP[] PROGMEM = "DIFF_TEMP";
static const char PSTR_DIFF_TEMP_ABBR[] PROGMEM = "DFT";
static const char PSTR_DIFF_TEMP_LABEL[] PROGMEM = "Differential Fluid Temperature";

static const char PSTR_GEAR_OIL_TEMP[] PROGMEM = "GEAR_OIL_TEMP";
static const char PSTR_GEAR_OIL_TEMP_ABBR[] PROGMEM = "GFT";
static const char PSTR_GEAR_OIL_TEMP_LABEL[] PROGMEM = "Gearbox Fluid Temperature";

static const char PSTR_OIL_LEVEL[] PROGMEM = "OIL_LEVEL";
static const char PSTR_OIL_LEVEL_ABBR[] PROGMEM = "OLVL";
static const char PSTR_OIL_LEVEL_LABEL[] PROGMEM = "Engine Oil Level";

static const char PSTR_INTERCOOLER_TEMP[] PROGMEM = "INTERCOOLER_TEMP";
static const char PSTR_INTERCOOLER_TEMP_ABBR[] PROGMEM = "ICT";
static const char PSTR_INTERCOOLER_TEMP_LABEL[] PROGMEM = "Intercooler Temperature";

// ===== APPLICATION PRESETS (PROGMEM - Flash Memory) =====
//
// To add a new application:
// 1. Add PROGMEM strings for name and label above
// 2. Add ApplicationPreset entry at END of this array
// 3. Compute nameHash using Python one-liner in header
// 4. Bump NUM_APPLICATION_PRESETS in application_presets.h
//
const PROGMEM ApplicationPreset APPLICATION_PRESETS[] = {
    // Index 0: APP_NONE (placeholder)
    {

        .name = PSTR_APP_NONE,
        .abbreviation = nullptr,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_NONE,
        .defaultUnits = 0,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x2F75,  // djb2_hash("NONE")
        .warmupTime_ms = 0,
        .persistTime_ms = 0,
        .group = APP_GROUP_OTHER
    },

    // ===== TEMPERATURE APPLICATIONS =====
    // Index 1: CHT - Cylinder Head Temperature
    {

        .name = PSTR_CHT,
        .abbreviation = PSTR_CHT_ABBR,
        .label = PSTR_CHT_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_MAX31855,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 260.0,
        .obd2pid = 0xC8,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xD984,  // djb2_hash("CHT")
        .warmupTime_ms = 30000,  // 30 seconds warmup
        .persistTime_ms = 2000,  // 2 seconds persistence
        .group = APP_GROUP_ENGINE
    },

    // Index 2: EGT - Exhaust Gas Temperature
    {

        .name = PSTR_EGT,
        .abbreviation = PSTR_EGT_ABBR,
        .label = PSTR_EGT_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_MAX31855,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 600.0,
        .obd2pid = 0x78,
        .obd2length = 2,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xE1E5,  // djb2_hash("EGT")
        .warmupTime_ms = 20000,  // 20 seconds warmup
        .persistTime_ms = 2000,  // 2 seconds persistence
        .group = APP_GROUP_EXHAUST
    },

    // Index 3: COOLANT_TEMP - Engine Coolant Temperature
    {

        .name = PSTR_COOLANT_TEMP,
        .abbreviation = PSTR_COOLANT_TEMP_ABBR,
        .label = PSTR_COOLANT_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_120C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 100.0,
        .obd2pid = 0x05,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xB5AA,  // djb2_hash("COOLANT_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENGINE
    },

    // Index 4: OIL_TEMP - Engine Oil Temperature
    {

        .name = PSTR_OIL_TEMP,
        .abbreviation = PSTR_OIL_TEMP_ABBR,
        .label = PSTR_OIL_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_150C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 150.0,
        .obd2pid = 0x5C,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xB5BE,  // djb2_hash("OIL_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENGINE
    },

    // Index 5: TCASE_TEMP - Transfer Case Temperature
    {

        .name = PSTR_TCASE_TEMP,
        .abbreviation = PSTR_TCASE_TEMP_ABBR,
        .label = PSTR_TCASE_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_120C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 100.0,
        .obd2pid = 0xC9,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x1BEA,  // djb2_hash("TCASE_TEMP")
        .warmupTime_ms = 60000,  // 60 seconds warmup
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_DRIVETRAIN
    },

    // Index 6: AMBIENT_TEMP - Ambient Air Temperature (BME280)
    {

        .name = PSTR_AMBIENT_TEMP,
        .abbreviation = PSTR_AMBIENT_TEMP_ABBR,
        .label = PSTR_AMBIENT_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_TEMP,
        .defaultUnits = 0,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x46,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x323A,  // djb2_hash("AMBIENT_TEMP")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENVIRONMENT
    },

    // ===== PRESSURE APPLICATIONS =====
    // Index 7: OIL_PRESSURE - Engine Oil Pressure
    {

        .name = PSTR_OIL_PRESSURE,
        .abbreviation = PSTR_OIL_PRESSURE_ABBR,
        .label = PSTR_OIL_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_5BAR_CURVE,
        .defaultUnits = 2,
        .defaultMinValue = 1.0,
        .defaultMaxValue = 5.0,
        .obd2pid = 0xCA,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0x2361,  // djb2_hash("OIL_PRESSURE")
        .warmupTime_ms = 5000,  // 5 seconds warmup
        .persistTime_ms = 1000,  // 1 second persistence
        .group = APP_GROUP_ENGINE
    },

    // Index 8: BOOST_PRESSURE - Boost/Intake Pressure
    {

        .name = PSTR_BOOST_PRESSURE,
        .abbreviation = PSTR_BOOST_PRESSURE_ABBR,
        .label = PSTR_BOOST_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_2BAR_CURVE,
        .defaultUnits = 2,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 2.0,
        .obd2pid = 0x6F,
        .obd2length = 2,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xC084,  // djb2_hash("BOOST_PRESSURE")
        .warmupTime_ms = 1000,  // 1 second warmup
        .persistTime_ms = 500,  // 0.5 seconds persistence
        .group = APP_GROUP_INDUCTION
    },

    // Index 9: FUEL_PRESSURE
    {

        .name = PSTR_FUEL_PRESSURE,
        .abbreviation = PSTR_FUEL_PRESSURE_ABBR,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_NONE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x0A,  // Standard PID: Fuel Pressure (gauge) (matches standard_pids.h:112)
        .obd2length = 1,  // 1 byte (scale: 3.0 kPa, range: 0-765 kPa)
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xA889,  // djb2_hash("FUEL_PRESSURE")
        .warmupTime_ms = 2000,  // 2 seconds warmup
        .persistTime_ms = 1000,  // 1 second persistence
        .group = APP_GROUP_ENGINE
    },

    // Index 10: BAROMETRIC_PRESSURE - Barometric Pressure (BME280)
    {

        .name = PSTR_BAROMETRIC_PRESSURE,
        .abbreviation = PSTR_BAROMETRIC_PRESSURE_ABBR,
        .label = PSTR_BAROMETRIC_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_PRESSURE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x33,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xFF65,  // djb2_hash("BAROMETRIC_PRESSURE")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENVIRONMENT
    },

    // ===== VOLTAGE APPLICATIONS =====
    // Index 11: PRIMARY_BATTERY - Primary Battery Voltage
    {

        .name = PSTR_PRIMARY_BATTERY,
        .abbreviation = PSTR_PRIMARY_BATTERY_ABBR,
        .label = PSTR_PRIMARY_BATTERY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VOLTAGE_DIVIDER,
        .defaultUnits = 6,
        .defaultMinValue = 10.0,
        .defaultMaxValue = 15.0,
        .obd2pid = 0xCB,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_VOLTAGE,
        .nameHash = 0xD063,  // djb2_hash("PRIMARY_BATTERY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 1000,  // 1 second persistence
        .group = APP_GROUP_ELECTRICAL
    },

    // Index 12: AUXILIARY_BATTERY - Auxiliary Battery Voltage
    {

        .name = PSTR_AUXILIARY_BATTERY,
        .abbreviation = PSTR_AUXILIARY_BATTERY_ABBR,
        .label = PSTR_AUXILIARY_BATTERY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VOLTAGE_DIVIDER,
        .defaultUnits = 6,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0xCC,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_VOLTAGE,
        .nameHash = 0x01F7,  // djb2_hash("AUXILIARY_BATTERY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 1000,  // 1 second persistence
        .group = APP_GROUP_ELECTRICAL
    },

    // ===== DIGITAL APPLICATIONS =====
    // Index 13: COOLANT_LEVEL - Coolant Level (Float Switch)
    {

        .name = PSTR_COOLANT_LEVEL,
        .abbreviation = PSTR_COOLANT_LEVEL_ABBR,
        .label = PSTR_COOLANT_LEVEL_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_FLOAT_SWITCH,
        .defaultUnits = 8,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 1.0,
        .obd2pid = 0xA2,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_DIGITAL,
        .nameHash = 0xCC0C,  // djb2_hash("COOLANT_LEVEL")
        .warmupTime_ms = 5000,  // 5 seconds warmup
        .persistTime_ms = 2000,  // 2 seconds persistence
        .group = APP_GROUP_ENGINE
    },

    // ===== ENVIRONMENTAL APPLICATIONS =====
    // Index 14: HUMIDITY - Relative Humidity (BME280)
    {

        .name = PSTR_HUMIDITY,
        .abbreviation = PSTR_HUMIDITY_ABBR,
        .label = PSTR_HUMIDITY_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_HUMIDITY,
        .defaultUnits = 8,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0,
        .obd2length = 0,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_HUMIDITY,
        .nameHash = 0x1612,  // djb2_hash("HUMIDITY")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENVIRONMENT
    },

    // Index 15: ELEVATION - Elevation (BME280)
    {

        .name = PSTR_ELEVATION,
        .abbreviation = PSTR_ELEVATION_ABBR,
        .label = PSTR_ELEVATION_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_BME280_ELEVATION,
        .defaultUnits = 9,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0xA1,
        .obd2length = 2,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_ELEVATION,
        .nameHash = 0xC26C,  // djb2_hash("ELEVATION")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 5000,  // 5 seconds persistence
        .group = APP_GROUP_ENVIRONMENT
    },

    // ===== RPM APPLICATIONS =====
    // Index 16: ENGINE_RPM
    {

        .name = PSTR_ENGINE_RPM,
        .abbreviation = PSTR_ENGINE_RPM_ABBR,
        .label = nullptr,
        .description = nullptr,
        .defaultSensor = SENSOR_W_PHASE_RPM,
        .defaultUnits = 7,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,
        .obd2pid = 0x0C,  // Standard PID: Engine RPM (matches standard_pids.h:118)
        .obd2length = 2,  // 2 bytes (scale: 0.25, range: 0-16,383.75 RPM)
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = false,
        .expectedMeasurementType = MEASURE_RPM,
        .nameHash = 0x4429,  // djb2_hash("ENGINE_RPM")
        .warmupTime_ms = 2000,  // 2 seconds warmup
        .persistTime_ms = 0,  // No persistence needed
        .group = APP_GROUP_ENGINE
    },

    // ===== PRESSURE APPLICATIONS (EXTENDED) =====
    // Index 17: HYDRAULIC_PRESSURE - Hydraulic System Pressure (0-10 bar)
    {
        .name = PSTR_HYDRAULIC_PRESSURE,
        .abbreviation = PSTR_HYDRAULIC_PRESSURE_ABBR,
        .label = PSTR_HYDRAULIC_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_10BAR_TABLE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 10.0,
        .obd2pid = 0xCD,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0xADC2,  // djb2_hash("HYDRAULIC_PRESSURE")
        .warmupTime_ms = 2000,
        .persistTime_ms = 1000,
        .group = APP_GROUP_OTHER
    },

    // Index 18: BRAKE_PRESSURE - Brake System Pressure (0-10 bar)
    {
        .name = PSTR_BRAKE_PRESSURE,
        .abbreviation = PSTR_BRAKE_PRESSURE_ABBR,
        .label = PSTR_BRAKE_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_10BAR_TABLE,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 10.0,
        .obd2pid = 0xCE,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0x96C2,  // djb2_hash("BRAKE_PRESSURE")
        .warmupTime_ms = 1000,
        .persistTime_ms = 500,
        .group = APP_GROUP_OTHER
    },

    // ===== LEVEL APPLICATIONS =====
    // Index 19: FUEL_LEVEL - Fuel Tank Level (0-100%)
    {
        .name = PSTR_FUEL_LEVEL,
        .abbreviation = PSTR_FUEL_LEVEL_ABBR,
        .label = PSTR_FUEL_LEVEL_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_FUEL_LEVEL_180,
        .defaultUnits = 8,  // PERCENT
        .defaultMinValue = 10.0,   // Warn below 10% (low fuel)
        .defaultMaxValue = 100.0,
        .obd2pid = 0x2F,   // SAE J1979 PID 0x2F: Fuel Tank Level Input
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PERCENT,
        .nameHash = 0x24C8,  // djb2_hash("FUEL_LEVEL")
        .warmupTime_ms = 2000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_ENGINE
    },

    // ===== SPEED APPLICATIONS =====
    // Index 20: VEHICLE_SPEED
    {
        .name = PSTR_VEHICLE_SPEED,
        .abbreviation = PSTR_VEHICLE_SPEED_ABBR,
        .label = PSTR_VEHICLE_SPEED_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_HALL_SPEED,
        .defaultUnits = 11,  // KPH
        .defaultMinValue = 0.0,
        .defaultMaxValue = 0.0,  // No alarm by default (informational only)
        .obd2pid = 0x0D,  // OBD-II PID 0x0D: Vehicle Speed
        .obd2length = 1,  // Single byte response
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_SPEED,
        .nameHash = 0x46F5,  // djb2_hash("VEHICLE_SPEED")
        .warmupTime_ms = 0,  // No warmup needed
        .persistTime_ms = 0,  // No persistence needed
        .group = APP_GROUP_DRIVETRAIN
    },

    // ===== TEMPERATURE APPLICATIONS (EXTENDED) =====
    // Index 21: TRANS_TEMP - Automatic Transmission Temperature
    {
        .name = PSTR_TRANS_TEMP,
        .abbreviation = PSTR_TRANS_TEMP_ABBR,
        .label = PSTR_TRANS_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_150C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 130.0,
        .obd2pid = 0xA4,  // SAE J1979: Transmission Temperature (matches standard_pids.h)
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xC402,  // djb2_hash("TRANS_TEMP")
        .warmupTime_ms = 60000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_DRIVETRAIN
    },

    // Index 22: INTAKE_AIR_TEMP - Intake Air Temperature (IAT)
    {
        .name = PSTR_INTAKE_AIR_TEMP,
        .abbreviation = PSTR_INTAKE_AIR_TEMP_ABBR,
        .label = PSTR_INTAKE_AIR_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_120C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 60.0,
        .obd2pid = 0x0F,  // SAE J1979: Intake Air Temperature (matches standard_pids.h)
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0xC3F1,  // djb2_hash("INTAKE_AIR_TEMP")
        .warmupTime_ms = 10000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_INDUCTION
    },

    // ===== PRESSURE APPLICATIONS (EXTENDED) =====
    // Index 23: MAP_PRESSURE - Manifold Absolute Pressure
    {
        .name = PSTR_MAP_PRESSURE,
        .abbreviation = PSTR_MAP_PRESSURE_ABBR,
        .label = PSTR_MAP_PRESSURE_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_MPX4250AP,
        .defaultUnits = 2,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 2.5,
        .obd2pid = 0x0B,  // SAE J1979: Intake Manifold Absolute Pressure (matches standard_pids.h)
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PRESSURE,
        .nameHash = 0x031B,  // djb2_hash("MAP_PRESSURE")
        .warmupTime_ms = 1000,
        .persistTime_ms = 500,
        .group = APP_GROUP_INDUCTION
    },

    // ===== LEVEL/POSITION APPLICATIONS =====
    // Index 24: THROTTLE_POSITION - Throttle Position Sensor (TPS)
    {
        .name = PSTR_THROTTLE_POSITION,
        .abbreviation = PSTR_THROTTLE_POSITION_ABBR,
        .label = PSTR_THROTTLE_POSITION_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_GENERIC_TPS,
        .defaultUnits = 8,  // PERCENT
        .defaultMinValue = 0.0,
        .defaultMaxValue = 100.0,
        .obd2pid = 0x11,  // SAE J1979: Throttle Position (matches standard_pids.h)
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_PERCENT,
        .nameHash = 0x7E2F,  // djb2_hash("THROTTLE_POSITION")
        .warmupTime_ms = 0,
        .persistTime_ms = 0,
        .group = APP_GROUP_ENGINE
    },

    // ===== TEMPERATURE APPLICATIONS (DRIVETRAIN) =====
    // Index 25: DIFF_TEMP - Differential Temperature
    {
        .name = PSTR_DIFF_TEMP,
        .abbreviation = PSTR_DIFF_TEMP_ABBR,
        .label = PSTR_DIFF_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_150C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 120.0,
        .obd2pid = 0xCF,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x5353,  // djb2_hash("DIFF_TEMP")
        .warmupTime_ms = 60000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_DRIVETRAIN
    },

    // Index 26: GEAR_OIL_TEMP - Gear Oil Temperature (manual transmission / gearbox)
    {
        .name = PSTR_GEAR_OIL_TEMP,
        .abbreviation = PSTR_GEAR_OIL_TEMP_ABBR,
        .label = PSTR_GEAR_OIL_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_VDO_150C_STEINHART,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 120.0,
        .obd2pid = 0xD0,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x003C,  // djb2_hash("GEAR_OIL_TEMP")
        .warmupTime_ms = 60000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_DRIVETRAIN
    },

    // ===== DIGITAL APPLICATIONS (EXTENDED) =====
    // Index 27: OIL_LEVEL - Engine Oil Level (float switch)
    {
        .name = PSTR_OIL_LEVEL,
        .abbreviation = PSTR_OIL_LEVEL_ABBR,
        .label = PSTR_OIL_LEVEL_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_FLOAT_SWITCH,
        .defaultUnits = 8,
        .defaultMinValue = 0.0,
        .defaultMaxValue = 1.0,
        .obd2pid = 0xD1,
        .obd2length = 1,
        .defaultAlarmEnabled = true,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_DIGITAL,
        .nameHash = 0xCEA0,  // djb2_hash("OIL_LEVEL")
        .warmupTime_ms = 10000,
        .persistTime_ms = 3000,
        .group = APP_GROUP_ENGINE
    },

    // ===== TEMPERATURE APPLICATIONS (FORCED INDUCTION) =====
    // Index 28: INTERCOOLER_TEMP - Intercooler / Charge Air Temperature
    {
        .name = PSTR_INTERCOOLER_TEMP,
        .abbreviation = PSTR_INTERCOOLER_TEMP_ABBR,
        .label = PSTR_INTERCOOLER_TEMP_LABEL,
        .description = nullptr,
        .defaultSensor = SENSOR_GENERIC_TEMP_LINEAR,
        .defaultUnits = 0,
        .defaultMinValue = -1.0,
        .defaultMaxValue = 60.0,
        .obd2pid = 0xD2,
        .obd2length = 1,
        .defaultAlarmEnabled = false,
        .defaultDisplayEnabled = true,
        .expectedMeasurementType = MEASURE_TEMPERATURE,
        .nameHash = 0x2500,  // djb2_hash("INTERCOOLER_TEMP")
        .warmupTime_ms = 10000,
        .persistTime_ms = 5000,
        .group = APP_GROUP_INDUCTION
    }
};

static_assert(sizeof(APPLICATION_PRESETS) / sizeof(APPLICATION_PRESETS[0]) == NUM_APPLICATION_PRESETS,
              "NUM_APPLICATION_PRESETS (application_presets.h) has drifted from APPLICATION_PRESETS's real extent");
