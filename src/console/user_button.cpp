/*
 * user_button.cpp - The user button: debounce and short/long press detection
 * Only compiled when INCLUDE_USER_BUTTON is nonzero (boards/resolve.h).
 */

#include "user_button.h"
#include "../config.h"

#if INCLUDE_USER_BUTTON

#if !defined(USER_BUTTON_PIN)
#error "USER_BUTTON_PIN must be defined in the board profile when INCLUDE_USER_BUTTON is set"
#endif

#include "../core/message_api.h"
#include "../core/log_tags.h"
#include "../config/system_config.h"   // builtinPinHeld()

// Button timing constants
#define DEBOUNCE_MS 50              // Debounce delay (50ms)
#define LONG_PRESS_MS 2000          // Long press threshold (2 seconds)

// Button state tracking
static bool buttonState = false;
static bool lastRawState = false;
static uint32_t lastDebounceTime = 0;
static uint32_t pressStartTime = 0;
static bool pressHandled = false;

// The button configures and reads its pin only while it holds the claim
// (§12.5). Refused, the pin is a sensor's: a pull-up would skew it, and its
// transitions would read as presses that silence alarms (#646).
static bool held = false;

void initUserButton() {
    held = builtinPinHeld(BPK_USER_BUTTON);
    buttonState = false;
    if (held) {
        pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
        buttonState = digitalRead(USER_BUTTON_PIN) == LOW;
    }
    lastRawState = buttonState;
    lastDebounceTime = millis();
    pressStartTime = 0;
    pressHandled = false;
    msg.debug.info(TAG_SYSTEM, "User button initialized");
}

ButtonPress updateUserButton() {
    if (!held) return BUTTON_NONE;
    uint32_t now = millis();

    bool rawState = (digitalRead(USER_BUTTON_PIN) == LOW);

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

bool isUserButtonPressed() {
    return buttonState;
}

bool userButtonDownNow() {
    return held && digitalRead(USER_BUTTON_PIN) == LOW;
}

#endif // INCLUDE_USER_BUTTON
