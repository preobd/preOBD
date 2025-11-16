/*
 * sensor_library.h - Predefined sensor configurations
 * 
 * This is the "sensor catalog" - choose from these presets in config.h
 * 
 * FOR BASIC SETUP: Just pick the sensor ID that matches your hardware
 * FOR ADVANCED users: Use these as starting points or define custom calibrations
 */

#ifndef SENSOR_LIBRARY_H
#define SENSOR_LIBRARY_H

// ===== TEMPERATURE SENSORS =====

// Thermocouples
#define K_TYPE_THERMOCOUPLE_MAX6675     1
#define K_TYPE_THERMOCOUPLE_MAX31855    2

// VDO Thermistors - Lookup Table Method (Most Accurate)
#define VDO_120C_LOOKUP                 10  // VDO 120°C coolant/transfer case sensor
#define VDO_150C_LOOKUP                 11  // VDO 150°C oil temp sensor

// VDO Thermistors - Steinhart-Hart Method (Faster, Good Accuracy)
#define VDO_120C_STEINHART              12  // VDO 120°C with Steinhart-Hart
#define VDO_150C_STEINHART              13  // VDO 150°C with Steinhart-Hart

// Generic NTC Thermistors (Common from Amazon/eBay)
#define GENERIC_NTC_10K_3950            20  // 10KΩ NTC, β=3950 (very common)
#define GENERIC_NTC_10K_3435            21  // 10KΩ NTC, β=3435
#define GENERIC_NTC_10K_3380            22  // 10KΩ NTC, β=3380

// Custom/User-Defined
#define CUSTOM_THERMISTOR_LOOKUP        29  // User provides lookup table
#define CUSTOM_THERMISTOR_STEINHART     30  // User provides coefficients

// ===== PRESSURE SENSORS =====

// VDO Pressure Sensors - Polynomial Method (Factory Calibration)
#define VDO_5BAR_PRESSURE               40  // VDO 5-bar oil pressure (0-5 bar)
#define VDO_2BAR_PRESSURE               41  // VDO 2-bar boost pressure (0-2 bar)

// Generic Linear Pressure Sensors
#define GENERIC_0_5V_5BAR               42  // Generic 0.5-4.5V linear, 0-5 bar
#define GENERIC_0_5V_10BAR              43  // Generic 0.5-4.5V linear, 0-10 bar
#define GENERIC_0_5V_100PSI             44  // Generic 0.5-4.5V linear, 0-100 psi

// Specific Sensors
#define MPX4250AP_PRESSURE              45  // Freescale (NXP) MPX4250AP (20-250kPa)

// Custom/User-Defined
#define CUSTOM_PRESSURE_LINEAR          48  // User provides voltage/pressure ranges
#define CUSTOM_PRESSURE_POLYNOMIAL      49  // User provides polynomial coefficients

// ===== VOLTAGE SENSORS =====

// Standard Battery Monitoring
#define STANDARD_12V_DIVIDER            50  // Auto-configured for board voltage
#define STANDARD_24V_DIVIDER            51  // For 24V truck systems

// Custom Voltage Dividers
#define CUSTOM_VOLTAGE_100K_10K         52  // 100kΩ / 10kΩ divider
#define CUSTOM_VOLTAGE_100K_22K         53  // 100kΩ / 22kΩ divider (3.3V systems)
#define CUSTOM_VOLTAGE_47K_10K          54  // 47kΩ / 10kΩ divider

// Direct Voltage Reading (no divider)
#define DIRECT_VOLTAGE_5V               55  // Direct 0-5V reading
#define DIRECT_VOLTAGE_3V3              56  // Direct 0-3.3V reading

// Custom/User-Defined
#define CUSTOM_VOLTAGE_DIVIDER          59  // User provides R1/R2

// ===== RPM SENSORS =====

#define W_PHASE_RPM_12_POLE             60  // 12-pole alternator
#define W_PHASE_RPM_14_POLE             61  // 14-pole alternator
#define W_PHASE_RPM_16_POLE             62  // 16-pole alternator
#define W_PHASE_RPM_CUSTOM              69  // Custom pole count

// ===== ENVIRONMENTAL SENSORS =====

// BME280 (I2C sensor with temp, pressure, humidity)
#define BME280_AMBIENT_TEMPERATURE      80
#define BME280_BAROMETRIC_PRESSURE      81
#define BME280_RELATIVE_HUMIDITY        82
#define BME280_ESTIMATED_ALTITUDE       83

// ===== DIGITAL SENSORS =====

// Float Switches
#define DIGITAL_FLOAT_SWITCH            90  // Generic float switch (coolant level, etc.)

// ===== USAGE EXAMPLES =====
/*
 * In config.h, simply pick a sensor:
 * 
 * #define ENABLE_COOLANT_TEMP
 * #define COOLANT_SENSOR_TYPE    VDO_120C_LOOKUP
 * #define COOLANT_TEMP_INPUT     A2
 * 
 * That's it! The system looks up all the calibration data automatically.
 * 
 * Advanced users can override with custom calibrations (see advanced_config.h)
 */

#endif
