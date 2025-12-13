/*
 * button_handler.cpp - Multi-function button handler implementation
 */

#include "button_handler.h"
#include "../config.h"

// Button timing constants
#define DEBOUNCE_MS 50              // Debounce delay (50ms)
#define LONG_PRESS_MS 2000          // Long press threshold (2 seconds)

// Button state tracking
static bool buttonState = false;           // Current debounced state (true = pressed)
static bool lastRawState = false;          // Last raw reading
static uint32_t lastDebounceTime = 0;      // Last time input changed
static uint32_t pressStartTime = 0;        // When button was first pressed
static bool pressHandled = false;          // Flag to prevent re-triggering

void initButtonHandler() {
    pinMode(MODE_BUTTON, INPUT_PULLUP);
    buttonState = digitalRead(MODE_BUTTON) == LOW;
    lastRawState = buttonState;
    lastDebounceTime = millis();
    pressStartTime = 0;
    pressHandled = false;
}

ButtonPress updateButtonHandler() {
    uint32_t now = millis();

    // Read raw button state (LOW = pressed with INPUT_PULLUP)
    bool rawState = (digitalRead(MODE_BUTTON) == LOW);

    // Detect state change
    if (rawState != lastRawState) {
        lastDebounceTime = now;
    }
    lastRawState = rawState;

    // Debounce: only update state if stable for DEBOUNCE_MS
    if ((now - lastDebounceTime) > DEBOUNCE_MS) {
        bool newState = rawState;

        // Detect button press (transition from not pressed to pressed)
        if (newState && !buttonState) {
            // Button just pressed
            pressStartTime = now;
            pressHandled = false;
        }

        // Detect button release (transition from pressed to not pressed)
        if (!newState && buttonState) {
            // Button just released
            if (!pressHandled && pressStartTime > 0) {
                uint32_t pressDuration = now - pressStartTime;
                pressStartTime = 0;

                // Determine press type
                if (pressDuration >= LONG_PRESS_MS) {
                    buttonState = newState;
                    return BUTTON_LONG_PRESS;
                } else {
                    buttonState = newState;
                    return BUTTON_SHORT_PRESS;
                }
            }
        }

        buttonState = newState;
    }

    return BUTTON_NONE;
}

bool isButtonPressed() {
    return buttonState;
}
