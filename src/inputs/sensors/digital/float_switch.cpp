/*
 * float_switch.cpp - Digital Float Switch Reading
 *
 * Implements digital input reading for float switches.
 * Commonly used for coolant level monitoring.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"
#include "../../../lib/message_api.h"
#include "../../../lib/log_tags.h"

// ===== INITIALIZATION =====

/**
 * Initialize digital float switch
 *
 * Sets up a digital pin with internal pullup resistor.
 * Most float switches are normally-closed and need pullup.
 *
 * @param ptr  Pointer to Input structure containing pin configuration
 */
void initFloatSwitch(Input* ptr) {
    pinMode(ptr->pin, INPUT_PULLUP);  // Most float switches need pullup
    msg.debug.info(TAG_SENSOR, "Digital input on pin %d for %s", ptr->pin, ptr->abbrName);
}

// ===== READING =====

/**
 * Read digital float switch
 *
 * Reads digital state from pin and handles normal orientation.
 * Supports both normally-closed (NC) and normally-open (NO) switches
 * via COOLANT_LEVEL_INVERTED compile-time flag.
 *
 * @param ptr  Pointer to Input structure to store switch state
 *
 * Return values:
 * - 1.0 = Float UP (coolant OK)
 * - 0.0 = Float DOWN (low coolant)
 *
 * @note Default (NC): Float UP = CLOSED = HIGH, Float DOWN = OPEN = LOW
 * @note Inverted (NO): Float UP = OPEN = LOW, Float DOWN = CLOSED = HIGH
 */
void readDigitalFloatSwitch(Input *ptr) {
    // Read digital state from the pin
    float rawValue = (float)digitalRead(ptr->pin);

    // Support both normally closed (NC) and normally open (NO) switches
    #ifdef COOLANT_LEVEL_INVERTED
    // Normally open: Float UP (ok) = OPEN = LOW, Float DOWN (low) = CLOSED = HIGH
    ptr->value = 1.0 - rawValue;  // Invert the reading
    #else
    // Normally closed (default): Float UP (ok) = CLOSED = HIGH, Float DOWN (low) = OPEN = LOW
    ptr->value = rawValue;
    #endif
}
