/*
 * pressure/linear.cpp - Wrapper for Linear Pressure Sensors
 *
 * This file is a convenience wrapper that points to the shared linear sensor
 * implementation. Linear pressure sensors (e.g., GENERIC_BOOST, AEM 150 PSI,
 * MPX4250AP) use the same voltage-to-value conversion logic as linear
 * temperature sensors.
 *
 * The actual implementation is in: ../linear/linear_sensor.cpp
 *
 * This wrapper exists so developers can find linear pressure sensors by
 * looking in the pressure/ directory, with clear signposting to the
 * shared implementation.
 */

// Linear pressure sensors use the shared linear sensor implementation
// See: src/inputs/sensors/linear/linear_sensor.cpp for implementation details
#include "../linear/linear_sensor.cpp"
