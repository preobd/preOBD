/*
 * standard_pids.h - Standard OBD-II PID Database
 *
 * PROGMEM table of common OBD-II Mode 01 PIDs with automatic name recognition,
 * data types, and conversion formulas. Used during CAN scanning to identify PIDs
 * and provide user-friendly names.
 *
 * Reference: SAE J1979 (OBD-II Diagnostic Standard)
 * Mode 01: Show Current Data
 *
 * Memory: ~2 KB flash (PROGMEM)
 */

#ifndef STANDARD_PIDS_H
#define STANDARD_PIDS_H

#include <Arduino.h>
#include "../sensor_types.h"

// ===== STANDARD PID INFO STRUCTURE =====

struct StandardPIDInfo {
    uint8_t pid;                    // OBD-II PID number (0x00-0xFF)
    const char* name;               // Full name (e.g., "Engine RPM")
    const char* abbr;               // Abbreviation for display (e.g., "RPM")
    uint8_t data_length;            // Response data length in bytes (1, 2, or 4)
    MeasurementType measurementType; // Physical quantity type
    float scale_factor;             // Conversion multiplier
    float offset;                   // Conversion additive offset
    const char* units;              // Unit string (e.g., "RPM", "CELSIUS")
};

// ===== PROGMEM STRING LITERALS =====

// Temperature
static const char PSTR_PID_CELSIUS[] PROGMEM = "CELSIUS";
static const char PSTR_PID_COOLANT_TEMP[] PROGMEM = "Coolant Temperature";
static const char PSTR_PID_CLT[] PROGMEM = "CLT";
static const char PSTR_PID_INTAKE_TEMP[] PROGMEM = "Intake Air Temperature";
static const char PSTR_PID_IAT[] PROGMEM = "IAT";
static const char PSTR_PID_OIL_TEMP[] PROGMEM = "Oil Temperature";
static const char PSTR_PID_OIL[] PROGMEM = "OIL";
static const char PSTR_PID_AMBIENT_TEMP[] PROGMEM = "Ambient Air Temperature";
static const char PSTR_PID_AMB[] PROGMEM = "AMB";
static const char PSTR_PID_CAT_TEMP_B1S1[] PROGMEM = "Catalyst Temp (B1S1)";
static const char PSTR_PID_CAT_B1S1[] PROGMEM = "CT11";
static const char PSTR_PID_TRANSMISSION_TEMP[] PROGMEM = "Transmission Temperature";
static const char PSTR_PID_TRANS[] PROGMEM = "TRAN";

// RPM / Speed
static const char PSTR_PID_RPM_UNITS[] PROGMEM = "RPM";
static const char PSTR_PID_ENGINE_RPM[] PROGMEM = "Engine RPM";
static const char PSTR_PID_RPM[] PROGMEM = "RPM";
static const char PSTR_PID_KPH[] PROGMEM = "KPH";
static const char PSTR_PID_VEHICLE_SPEED[] PROGMEM = "Vehicle Speed";
static const char PSTR_PID_VSS[] PROGMEM = "VSS";

// Pressure
static const char PSTR_PID_KPA[] PROGMEM = "KPA";
static const char PSTR_PID_FUEL_PRESSURE[] PROGMEM = "Fuel Pressure";
static const char PSTR_PID_FUEL_P[] PROGMEM = " FP";
static const char PSTR_PID_INTAKE_MAP[] PROGMEM = "Intake Manifold Pressure";
static const char PSTR_PID_MAP[] PROGMEM = "MAP";
static const char PSTR_PID_BAROMETRIC[] PROGMEM = "Barometric Pressure";
static const char PSTR_PID_BARO[] PROGMEM = "BARO";
static const char PSTR_PID_FUEL_RAIL_P[] PROGMEM = "Fuel Rail Pressure";
static const char PSTR_PID_FRP[] PROGMEM = "FRP";
static const char PSTR_PID_EVAP_PRESSURE[] PROGMEM = "Evap System Pressure";
static const char PSTR_PID_EVAP[] PROGMEM = "EVAP";

// Voltage
static const char PSTR_PID_VOLTS[] PROGMEM = "VOLTS";
static const char PSTR_PID_CONTROL_VOLTAGE[] PROGMEM = "Control Module Voltage";
static const char PSTR_PID_ECU_V[] PROGMEM = "ECUV";
static const char PSTR_PID_HYBRID_BATTERY[] PROGMEM = "Hybrid Battery Voltage";
static const char PSTR_PID_HV_BATT[] PROGMEM = "HVBAT";

// Percent / Ratio
static const char PSTR_PID_PERCENT[] PROGMEM = "PERCENT";
static const char PSTR_PID_ENGINE_LOAD[] PROGMEM = "Engine Load";
static const char PSTR_PID_LOAD[] PROGMEM = "LOAD";
static const char PSTR_PID_THROTTLE_POS[] PROGMEM = "Throttle Position";
static const char PSTR_PID_TPS[] PROGMEM = "TPS";
static const char PSTR_PID_FUEL_LEVEL[] PROGMEM = "Fuel Tank Level";
static const char PSTR_PID_FUEL_LVL[] PROGMEM = "FUEL";

// Time
static const char PSTR_PID_SECONDS[] PROGMEM = "SECONDS";
static const char PSTR_PID_RUNTIME[] PROGMEM = "Engine Runtime";
static const char PSTR_PID_RUN_TIME[] PROGMEM = "RUNHR";

// Flow
static const char PSTR_PID_GPH[] PROGMEM = "GPH";
static const char PSTR_PID_MAF[] PROGMEM = "Mass Airflow";
static const char PSTR_PID_MAF_ABBR[] PROGMEM = "MAF";

// Distance
static const char PSTR_PID_KM[] PROGMEM = "KM";
static const char PSTR_PID_DISTANCE_MIL[] PROGMEM = "Distance w/ MIL On";
static const char PSTR_PID_DIST_MIL[] PROGMEM = "DMIL";

// ===== STANDARD PID TABLE (PROGMEM) =====

static const PROGMEM StandardPIDInfo STANDARD_PID_TABLE[] = {
    // 0x04: Engine Load
    {0x04, PSTR_PID_ENGINE_LOAD, PSTR_PID_LOAD, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0x05: Coolant Temperature
    {0x05, PSTR_PID_COOLANT_TEMP, PSTR_PID_CLT, 1, MEASURE_TEMPERATURE, 1.0, -40.0, PSTR_PID_CELSIUS},

    // 0x0A: Fuel Pressure
    {0x0A, PSTR_PID_FUEL_PRESSURE, PSTR_PID_FUEL_P, 1, MEASURE_PRESSURE, 3.0, 0.0, PSTR_PID_KPA},

    // 0x0B: Intake Manifold Pressure
    {0x0B, PSTR_PID_INTAKE_MAP, PSTR_PID_MAP, 1, MEASURE_PRESSURE, 1.0, 0.0, PSTR_PID_KPA},

    // 0x0C: Engine RPM
    {0x0C, PSTR_PID_ENGINE_RPM, PSTR_PID_RPM, 2, MEASURE_RPM, 0.25, 0.0, PSTR_PID_RPM_UNITS},

    // 0x0D: Vehicle Speed
    {0x0D, PSTR_PID_VEHICLE_SPEED, PSTR_PID_VSS, 1, MEASURE_SPEED, 1.0, 0.0, PSTR_PID_KPH},

    // 0x0F: Intake Air Temperature
    {0x0F, PSTR_PID_INTAKE_TEMP, PSTR_PID_IAT, 1, MEASURE_TEMPERATURE, 1.0, -40.0, PSTR_PID_CELSIUS},

    // 0x10: MAF Air Flow
    {0x10, PSTR_PID_MAF, PSTR_PID_MAF_ABBR, 2, MEASURE_DIGITAL, 0.01, 0.0, PSTR_PID_GPH},

    // 0x11: Throttle Position
    {0x11, PSTR_PID_THROTTLE_POS, PSTR_PID_TPS, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0x1F: Engine Run Time
    {0x1F, PSTR_PID_RUNTIME, PSTR_PID_RUN_TIME, 2, MEASURE_DIGITAL, 1.0, 0.0, PSTR_PID_SECONDS},

    // 0x21: Distance with MIL On
    {0x21, PSTR_PID_DISTANCE_MIL, PSTR_PID_DIST_MIL, 2, MEASURE_DIGITAL, 1.0, 0.0, PSTR_PID_KM},

    // 0x23: Fuel Rail Pressure
    {0x23, PSTR_PID_FUEL_RAIL_P, PSTR_PID_FRP, 2, MEASURE_PRESSURE, 10.0, 0.0, PSTR_PID_KPA},

    // 0x2F: Fuel Tank Level
    {0x2F, PSTR_PID_FUEL_LEVEL, PSTR_PID_FUEL_LVL, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0x33: Barometric Pressure
    {0x33, PSTR_PID_BAROMETRIC, PSTR_PID_BARO, 1, MEASURE_PRESSURE, 1.0, 0.0, PSTR_PID_KPA},

    // 0x42: Control Module Voltage
    {0x42, PSTR_PID_CONTROL_VOLTAGE, PSTR_PID_ECU_V, 2, MEASURE_VOLTAGE, 0.001, 0.0, PSTR_PID_VOLTS},

    // 0x45: Relative Throttle Position
    {0x45, PSTR_PID_THROTTLE_POS, PSTR_PID_TPS, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0x46: Ambient Air Temperature
    {0x46, PSTR_PID_AMBIENT_TEMP, PSTR_PID_AMB, 1, MEASURE_TEMPERATURE, 1.0, -40.0, PSTR_PID_CELSIUS},

    // 0x5A: Accelerator Pedal Position
    {0x5A, PSTR_PID_THROTTLE_POS, PSTR_PID_TPS, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0x5C: Oil Temperature
    {0x5C, PSTR_PID_OIL_TEMP, PSTR_PID_OIL, 1, MEASURE_TEMPERATURE, 1.0, -40.0, PSTR_PID_CELSIUS},

    // 0x5E: Engine Fuel Rate
    {0x5E, PSTR_PID_MAF, PSTR_PID_MAF_ABBR, 2, MEASURE_DIGITAL, 0.05, 0.0, PSTR_PID_GPH},

    // 0x66: Mass Air Flow Sensor
    {0x66, PSTR_PID_MAF, PSTR_PID_MAF_ABBR, 5, MEASURE_DIGITAL, 0.03125, 0.0, PSTR_PID_GPH},

    // 0x67: Engine Coolant Temperature
    {0x67, PSTR_PID_COOLANT_TEMP, PSTR_PID_CLT, 2, MEASURE_TEMPERATURE, 0.1, -40.0, PSTR_PID_CELSIUS},

    // 0x6F: Turbo/Supercharger Inlet Pressure (Boost)
    {0x6F, PSTR_PID_INTAKE_MAP, PSTR_PID_MAP, 2, MEASURE_PRESSURE, 0.01, 0.0, PSTR_PID_KPA},

    // 0x78: Exhaust Gas Temperature (Bank 1, Sensor 1)
    {0x78, PSTR_PID_CAT_TEMP_B1S1, PSTR_PID_CAT_B1S1, 2, MEASURE_TEMPERATURE, 0.1, -40.0, PSTR_PID_CELSIUS},

    // 0x7F: Engine Run Time
    {0x7F, PSTR_PID_RUNTIME, PSTR_PID_RUN_TIME, 4, MEASURE_DIGITAL, 1.0, 0.0, PSTR_PID_SECONDS},

    // 0x8E: Engine Friction - Percent Torque
    {0x8E, PSTR_PID_ENGINE_LOAD, PSTR_PID_LOAD, 1, MEASURE_DIGITAL, 1.0, -125.0, PSTR_PID_PERCENT},

    // 0xA4: Transmission Temperature
    {0xA4, PSTR_PID_TRANSMISSION_TEMP, PSTR_PID_TRANS, 1, MEASURE_TEMPERATURE, 1.0, -40.0, PSTR_PID_CELSIUS},

    // 0xA6: Odometer
    {0xA6, PSTR_PID_DISTANCE_MIL, PSTR_PID_DIST_MIL, 4, MEASURE_DIGITAL, 0.1, 0.0, PSTR_PID_KM},

    // 0xAA: Hybrid Battery Pack Remaining Life
    {0xAA, PSTR_PID_FUEL_LEVEL, PSTR_PID_FUEL_LVL, 1, MEASURE_DIGITAL, 0.392157, 0.0, PSTR_PID_PERCENT},

    // 0xC3: Fuel Injection Timing
    {0xC3, PSTR_PID_RUNTIME, PSTR_PID_RUN_TIME, 2, MEASURE_DIGITAL, 0.01, -210.0, PSTR_PID_SECONDS},

    // 0xC4: Fuel Consumption
    {0xC4, PSTR_PID_MAF, PSTR_PID_MAF_ABBR, 4, MEASURE_DIGITAL, 0.05, 0.0, PSTR_PID_GPH}
};

constexpr uint8_t NUM_STANDARD_PIDS = sizeof(STANDARD_PID_TABLE) / sizeof(STANDARD_PID_TABLE[0]);

// ===== HELPER FUNCTIONS =====

/**
 * Lookup standard PID info by PID number
 * Linear search through PROGMEM table
 *
 * @param pid   OBD-II PID to lookup
 * @return      Pointer to StandardPIDInfo in PROGMEM, or nullptr if not found
 */
inline const StandardPIDInfo* lookupStandardPID(uint8_t pid) {
    for (uint8_t i = 0; i < NUM_STANDARD_PIDS; i++) {
        uint8_t table_pid = pgm_read_byte(&STANDARD_PID_TABLE[i].pid);
        if (table_pid == pid) {
            return &STANDARD_PID_TABLE[i];
        }
    }
    return nullptr;  // Not found
}

/**
 * Read PID name from PROGMEM into RAM buffer
 *
 * @param pid_info  Pointer to StandardPIDInfo in PROGMEM
 * @param buffer    Destination buffer (at least 32 bytes)
 * @param max_len   Maximum buffer length
 */
inline void getStandardPIDName(const StandardPIDInfo* pid_info, char* buffer, uint8_t max_len) {
    if (!pid_info || !buffer) return;
    const char* name = (const char*)pgm_read_ptr(&pid_info->name);
    strncpy_P(buffer, name, max_len - 1);
    buffer[max_len - 1] = '\0';
}

/**
 * Read PID abbreviation from PROGMEM into RAM buffer
 *
 * @param pid_info  Pointer to StandardPIDInfo in PROGMEM
 * @param buffer    Destination buffer (at least 8 bytes)
 * @param max_len   Maximum buffer length
 */
inline void getStandardPIDAbbr(const StandardPIDInfo* pid_info, char* buffer, uint8_t max_len) {
    if (!pid_info || !buffer) return;
    const char* abbr = (const char*)pgm_read_ptr(&pid_info->abbr);
    strncpy_P(buffer, abbr, max_len - 1);
    buffer[max_len - 1] = '\0';
}

#endif // STANDARD_PIDS_H
