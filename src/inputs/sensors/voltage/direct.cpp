/*
 * direct.cpp - Direct Voltage Reading
 *
 * Implements direct voltage reading without a divider network.
 * Used when input voltage is within the ADC reference voltage range.
 */

#include "../../../config.h"
#include "../../../lib/platform.h"
#include "../../input.h"

/**
 * Read voltage directly (no divider)
 *
 * Measures voltage directly at an analog input pin.
 * Input voltage must not exceed AREF_VOLTAGE.
 *
 * @param ptr  Pointer to Input structure to store voltage reading
 *
 * Formula: V = ADC * (AREF / ADC_MAX)
 *
 * @note Returns NAN if reading is below threshold (sensor disconnected)
 */
void readVoltageDirect(Input *ptr) {
    int reading = analogRead(ptr->pin);

    if (reading < 10) {
        ptr->value = NAN;
        return;
    }

    // Direct voltage reading: V = ADC * (AREF / ADC_MAX)
    float voltage = (reading * AREF_VOLTAGE / (float)ADC_MAX_VALUE);

    ptr->value = voltage;
}
