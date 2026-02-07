/*
 * can_read.cpp - CAN Sensor Read Function
 *
 * Read function for CAN-imported sensors.
 * Retrieves cached CAN frame data and extracts multi-byte values.
 */

#include <Arduino.h>
#include "../../input.h"
#include "../../../lib/sensor_types.h"
#include "can_frame_cache.h"

/**
 * Read CAN-imported sensor
 * Retrieves cached CAN frame and extracts value with proper byte order
 *
 * @param ptr   Pointer to Input struct
 */
void readCANSensor(Input* ptr) {
    if (!ptr) return;

    // Get calibration (custom or preset)
    const CANSensorCalibration* cal;
    if (ptr->flags.useCustomCalibration) {
        cal = (const CANSensorCalibration*)&ptr->customCalibration.can;
    } else {
        cal = (const CANSensorCalibration*)ptr->presetCalibration;
    }

    if (!cal) {
        ptr->value = NAN;
        return;
    }

    // Lookup cached CAN frame
    CANFrameEntry* entry = getCANCacheEntry(cal->source_can_id, cal->source_pid);

    // Check validity and timeout (2000ms default)
    if (!entry || !entry->valid || isCANDataStale(entry, 2000)) {
        ptr->value = NAN;
        return;
    }

    // Validate data offset and length
    if (cal->data_offset + cal->data_length > 8) {
        ptr->value = NAN;
        return;
    }

    // Extract multi-byte value from CAN frame
    uint32_t raw_value = 0;

    if (cal->is_big_endian) {
        // Big-endian (MSB first) - OBD-II standard
        for (uint8_t i = 0; i < cal->data_length; i++) {
            raw_value = (raw_value << 8) | entry->data[cal->data_offset + i];
        }
    } else {
        // Little-endian (LSB first) - some J1939 and custom protocols
        for (uint8_t i = 0; i < cal->data_length; i++) {
            raw_value |= ((uint32_t)entry->data[cal->data_offset + i] << (i * 8));
        }
    }

    // Apply scaling and offset
    // Formula: output = (raw * scale) + offset
    // Examples:
    //   RPM:         raw * 0.25 + 0.0     (OBD-II PID 0x0C)
    //   Temperature: raw * 1.0 + (-40.0)  (OBD-II PID 0x05, 0x0F)
    //   Speed:       raw * 1.0 + 0.0      (OBD-II PID 0x0D)
    ptr->value = (raw_value * cal->scale_factor) + cal->offset;
}
