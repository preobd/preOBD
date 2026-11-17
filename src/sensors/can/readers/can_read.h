/*
 * can_read.h - CAN Sensor Read Function Header
 */

#ifndef CAN_READ_H
#define CAN_READ_H

// Forward declaration
struct Input;

// Read function for CAN-imported sensors
void readCANSensor(Input* ptr);

#endif // CAN_READ_H
