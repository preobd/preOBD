/*
 * button_handler.cpp - Multi-function button handler implementation
 * Only compiled when ENABLE_MODE_BUTTON is defined in the board profile.
 */

#include "button_handler.h"
#include "../config.h"

#if ENABLE_MODE_BUTTON

#if !defined(MODE_BUTTON_PIN)
#error "MODE_BUTTON_PIN must be defined in the board profile when ENABLE_MODE_BUTTON is set"
#endif

#include "message_api.h"
#include "log_tags.h"

// Button timing constants
#define DEBOUNCE_MS 50              // Debounce delay (50ms)
#define LONG_PRESS_MS 2000          // Long press threshold (2 seconds)

// Button state tracking
static bool buttonState = false;
static bool lastRawState = false;
static uint32_t lastDebounceTime = 0;
static uint32_t pressStartTime = 0;
static bool pressHandled = false;

void initButtonHandler() {
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    buttonState = digitalRead(MODE_BUTTON_PIN) == LOW;
    lastRawState = buttonState;
    lastDebounceTime = millis();
    pressStartTime = 0;
    pressHandled = false;
    msg.debug.info(TAG_SYSTEM, "Button handler initialized");
}

ButtonPress updateButtonHandler() {
    uint32_t now = millis();

    bool rawState = (digitalRead(MODE_BUTTON_PIN) == LOW);

    if (rawState != lastRawState) {
        lastDebounceTime = now;
    }
    lastRawState = rawState;

    if ((now - lastDebounceTime) > DEBOUNCE_MS) {
        bool newState = rawState;

        if (newState && !buttonState) {
            pressStartTime = now;
            pressHandled = false;
        }

        if (newState && buttonState && !pressHandled && pressStartTime > 0) {
            uint32_t pressDuration = now - pressStartTime;
            if (pressDuration >= LONG_PRESS_MS) {
                pressHandled = true;
                return BUTTON_LONG_PRESS;
            }
        }

        if (!newState && buttonState) {
            if (pressStartTime > 0) {
                pressStartTime = 0;
                if (!pressHandled) {
                    buttonState = newState;
                    return BUTTON_SHORT_PRESS;
                }
                pressHandled = false;
            }
        }

        buttonState = newState;
    }

    return BUTTON_NONE;
}

bool isButtonPressed() {
    return buttonState;
}

#endif // ENABLE_MODE_BUTTON
