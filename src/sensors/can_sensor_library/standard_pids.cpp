/*
 * standard_pids.cpp - the Service 01 PID table (see standard_pids.h for the
 * sources every row was checked against, #509)
 */

#include "standard_pids.h"

// Names and abbreviations (abbr fits Input::abbrName, 7 chars)
static const char PSTR_PID_ENGINE_LOAD[] PROGMEM = "Engine Load";
static const char PSTR_PID_LOAD[] PROGMEM = "LOAD";
static const char PSTR_PID_COOLANT_TEMP[] PROGMEM = "Engine Coolant Temperature";
static const char PSTR_PID_CLT[] PROGMEM = "ECT";
static const char PSTR_PID_FUEL_PRESSURE[] PROGMEM = "Fuel Pressure";
static const char PSTR_PID_FUEL_P[] PROGMEM = "FPR";
static const char PSTR_PID_INTAKE_MAP[] PROGMEM = "Intake Manifold Pressure";
static const char PSTR_PID_MAP[] PROGMEM = "MAP";
static const char PSTR_PID_ENGINE_RPM[] PROGMEM = "Engine RPM";
static const char PSTR_PID_RPM[] PROGMEM = "RPM";
static const char PSTR_PID_VEHICLE_SPEED[] PROGMEM = "Vehicle Speed";
static const char PSTR_PID_VSS[] PROGMEM = "VSS";
static const char PSTR_PID_INTAKE_TEMP[] PROGMEM = "Intake Air Temperature";
static const char PSTR_PID_IAT[] PROGMEM = "IAT";
static const char PSTR_PID_MAF[] PROGMEM = "Mass Air Flow";
static const char PSTR_PID_MAF_ABBR[] PROGMEM = "MAF";
static const char PSTR_PID_THROTTLE_POS[] PROGMEM = "Throttle Position";
static const char PSTR_PID_TPS[] PROGMEM = "TPS";
static const char PSTR_PID_RUNTIME[] PROGMEM = "Run Time Since Start";
static const char PSTR_PID_RUN_TIME[] PROGMEM = "RUNTM";
static const char PSTR_PID_DISTANCE_MIL[] PROGMEM = "Distance w/ MIL On";
static const char PSTR_PID_DIST_MIL[] PROGMEM = "DMIL";
static const char PSTR_PID_FUEL_RAIL_P[] PROGMEM = "Fuel Rail Gauge Pressure";
static const char PSTR_PID_FRP[] PROGMEM = "FRP";
static const char PSTR_PID_FUEL_LEVEL[] PROGMEM = "Fuel Tank Level";
static const char PSTR_PID_FUEL_LVL[] PROGMEM = "FUEL";
static const char PSTR_PID_DIST_CLEAR[] PROGMEM = "Distance Since Codes Cleared";
static const char PSTR_PID_DCLR[] PROGMEM = "DCLR";
static const char PSTR_PID_BAROMETRIC[] PROGMEM = "Barometric Pressure";
static const char PSTR_PID_BARO[] PROGMEM = "BARO";
static const char PSTR_PID_CONTROL_VOLTAGE[] PROGMEM = "Control Module Voltage";
static const char PSTR_PID_ECU_V[] PROGMEM = "ECUV";
static const char PSTR_PID_REL_THROTTLE[] PROGMEM = "Relative Throttle Position";
static const char PSTR_PID_RTPS[] PROGMEM = "RTPS";
static const char PSTR_PID_AMBIENT_TEMP[] PROGMEM = "Ambient Air Temperature";
static const char PSTR_PID_AMB[] PROGMEM = "AAT";
static const char PSTR_PID_TIME_MIL[] PROGMEM = "Time Run w/ MIL On";
static const char PSTR_PID_TMIL[] PROGMEM = "TMIL";
static const char PSTR_PID_TIME_CLEAR[] PROGMEM = "Time Since Codes Cleared";
static const char PSTR_PID_TCLR[] PROGMEM = "TCLR";
static const char PSTR_PID_PEDAL[] PROGMEM = "Relative Accel Pedal Position";
static const char PSTR_PID_APP[] PROGMEM = "APP";
static const char PSTR_PID_OIL_TEMP[] PROGMEM = "Engine Oil Temperature";
static const char PSTR_PID_OIL[] PROGMEM = "EOT";
static const char PSTR_PID_FUEL_RATE[] PROGMEM = "Engine Fuel Rate";
static const char PSTR_PID_FRATE[] PROGMEM = "FRATE";
static const char PSTR_PID_MAF_A[] PROGMEM = "Mass Air Flow (Sensor A)";
static const char PSTR_PID_MAFA[] PROGMEM = "MAFA";
static const char PSTR_PID_ECT_1[] PROGMEM = "Engine Coolant Temp (Sensor 1)";
static const char PSTR_PID_ECT1[] PROGMEM = "ECT1";
static const char PSTR_PID_FRICTION[] PROGMEM = "Engine Friction Torque";
static const char PSTR_PID_FRIC[] PROGMEM = "FRIC";
static const char PSTR_PID_ODOMETER[] PROGMEM = "Odometer";
static const char PSTR_PID_ODO[] PROGMEM = "ODO";

// Units: registry names/aliases of each PID's J1979 unit
static const char PSTR_U_PERCENT[] PROGMEM = "PERCENT";
static const char PSTR_U_C[] PROGMEM = "C";
static const char PSTR_U_KPA[] PROGMEM = "KPA";
static const char PSTR_U_RPM[] PROGMEM = "RPM";
static const char PSTR_U_KPH[] PROGMEM = "KPH";
static const char PSTR_U_V[] PROGMEM = "V";
static const char PSTR_U_GPS[] PROGMEM = "GPS";
static const char PSTR_U_S[] PROGMEM = "S";
static const char PSTR_U_MIN[] PROGMEM = "MIN";
static const char PSTR_U_KM[] PROGMEM = "KM";
static const char PSTR_U_LPH[] PROGMEM = "LPH";

// Sorted by PID. Formula column is J1979's, A = first byte after the PID.
const PROGMEM StandardPIDInfo STANDARD_PID_TABLE[] = {
    //  PID   name                     abbr               off len type               scale      offset   unit
    {0x04, PSTR_PID_ENGINE_LOAD,     PSTR_PID_LOAD,      0, 1, MEASURE_PERCENT,     0.392157,    0.0, PSTR_U_PERCENT}, // 100A/255
    {0x05, PSTR_PID_COOLANT_TEMP,    PSTR_PID_CLT,       0, 1, MEASURE_TEMPERATURE, 1.0,       -40.0, PSTR_U_C},       // A-40
    {0x0A, PSTR_PID_FUEL_PRESSURE,   PSTR_PID_FUEL_P,    0, 1, MEASURE_PRESSURE,    3.0,         0.0, PSTR_U_KPA},     // 3A (gauge)
    {0x0B, PSTR_PID_INTAKE_MAP,      PSTR_PID_MAP,       0, 1, MEASURE_PRESSURE,    1.0,         0.0, PSTR_U_KPA},     // A (absolute)
    {0x0C, PSTR_PID_ENGINE_RPM,      PSTR_PID_RPM,       0, 2, MEASURE_RPM,         0.25,        0.0, PSTR_U_RPM},     // (256A+B)/4
    {0x0D, PSTR_PID_VEHICLE_SPEED,   PSTR_PID_VSS,       0, 1, MEASURE_SPEED,       1.0,         0.0, PSTR_U_KPH},     // A
    {0x0F, PSTR_PID_INTAKE_TEMP,     PSTR_PID_IAT,       0, 1, MEASURE_TEMPERATURE, 1.0,       -40.0, PSTR_U_C},       // A-40
    {0x10, PSTR_PID_MAF,             PSTR_PID_MAF_ABBR,  0, 2, MEASURE_MASS_FLOW,   0.01,        0.0, PSTR_U_GPS},     // (256A+B)/100 g/s
    {0x11, PSTR_PID_THROTTLE_POS,    PSTR_PID_TPS,       0, 1, MEASURE_PERCENT,     0.392157,    0.0, PSTR_U_PERCENT}, // 100A/255
    {0x1F, PSTR_PID_RUNTIME,         PSTR_PID_RUN_TIME,  0, 2, MEASURE_TIME,        1.0,         0.0, PSTR_U_S},       // 256A+B s
    {0x21, PSTR_PID_DISTANCE_MIL,    PSTR_PID_DIST_MIL,  0, 2, MEASURE_DISTANCE,    1.0,         0.0, PSTR_U_KM},      // 256A+B km
    {0x23, PSTR_PID_FUEL_RAIL_P,     PSTR_PID_FRP,       0, 2, MEASURE_PRESSURE,    10.0,        0.0, PSTR_U_KPA},     // 10(256A+B) (gauge)
    {0x2F, PSTR_PID_FUEL_LEVEL,      PSTR_PID_FUEL_LVL,  0, 1, MEASURE_PERCENT,     0.392157,    0.0, PSTR_U_PERCENT}, // 100A/255
    {0x31, PSTR_PID_DIST_CLEAR,      PSTR_PID_DCLR,      0, 2, MEASURE_DISTANCE,    1.0,         0.0, PSTR_U_KM},      // 256A+B km
    {0x33, PSTR_PID_BAROMETRIC,      PSTR_PID_BARO,      0, 1, MEASURE_PRESSURE,    1.0,         0.0, PSTR_U_KPA},     // A
    {0x42, PSTR_PID_CONTROL_VOLTAGE, PSTR_PID_ECU_V,     0, 2, MEASURE_VOLTAGE,     0.001,       0.0, PSTR_U_V},       // (256A+B)/1000
    {0x45, PSTR_PID_REL_THROTTLE,    PSTR_PID_RTPS,      0, 1, MEASURE_PERCENT,     0.392157,    0.0, PSTR_U_PERCENT}, // 100A/255
    {0x46, PSTR_PID_AMBIENT_TEMP,    PSTR_PID_AMB,       0, 1, MEASURE_TEMPERATURE, 1.0,       -40.0, PSTR_U_C},       // A-40
    {0x4D, PSTR_PID_TIME_MIL,        PSTR_PID_TMIL,      0, 2, MEASURE_TIME,        1.0,         0.0, PSTR_U_MIN},     // 256A+B min
    {0x4E, PSTR_PID_TIME_CLEAR,      PSTR_PID_TCLR,      0, 2, MEASURE_TIME,        1.0,         0.0, PSTR_U_MIN},     // 256A+B min
    {0x5A, PSTR_PID_PEDAL,           PSTR_PID_APP,       0, 1, MEASURE_PERCENT,     0.392157,    0.0, PSTR_U_PERCENT}, // 100A/255
    {0x5C, PSTR_PID_OIL_TEMP,        PSTR_PID_OIL,       0, 1, MEASURE_TEMPERATURE, 1.0,       -40.0, PSTR_U_C},       // A-40
    {0x5E, PSTR_PID_FUEL_RATE,       PSTR_PID_FRATE,     0, 2, MEASURE_FUEL_RATE,   0.05,        0.0, PSTR_U_LPH},     // (256A+B)/20 L/h
    {0x66, PSTR_PID_MAF_A,           PSTR_PID_MAFA,      1, 2, MEASURE_MASS_FLOW,   0.03125,     0.0, PSTR_U_GPS},     // A=support, (256B+C)/32 g/s
    {0x67, PSTR_PID_ECT_1,           PSTR_PID_ECT1,      1, 1, MEASURE_TEMPERATURE, 1.0,       -40.0, PSTR_U_C},       // A=support, B-40
    {0x8E, PSTR_PID_FRICTION,        PSTR_PID_FRIC,      0, 1, MEASURE_PERCENT,     1.0,      -125.0, PSTR_U_PERCENT}, // A-125 (-125..130 %)
    {0xA6, PSTR_PID_ODOMETER,        PSTR_PID_ODO,       0, 4, MEASURE_DISTANCE,    0.1,         0.0, PSTR_U_KM},      // 32-bit /10 km
};

static_assert(sizeof(STANDARD_PID_TABLE) / sizeof(STANDARD_PID_TABLE[0]) == NUM_STANDARD_PIDS,
              "NUM_STANDARD_PIDS (standard_pids.h) has drifted from STANDARD_PID_TABLE");
