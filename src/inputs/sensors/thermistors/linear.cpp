/*
 * thermistors/linear.cpp - Wrapper for Linear Temperature Sensors
 *
 * This file is a convenience wrapper that points to the shared linear sensor
 * implementation. Linear temperature sensors (e.g., GENERIC_TEMP_LINEAR) use
 * the same voltage-to-value conversion logic as linear pressure sensors.
 *
 * The actual implementation is in: ../linear/linear_sensor.cpp
 *
 * This wrapper exists so developers can find linear temperature sensors by
 * looking in the thermistors/ directory, with clear signposting to the
 * shared implementation.
 */

// Linear temperature sensors use the shared linear sensor implementation
// See: src/inputs/sensors/linear/linear_sensor.cpp for implementation details
#include "../linear/linear_sensor.cpp"
