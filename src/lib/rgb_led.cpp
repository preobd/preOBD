/*
 * rgb_led.cpp - RGB LED Status Indicator Implementation
 *
 * Non-blocking PWM-based RGB LED controller with priority system
 */

#include "rgb_led.h"

#ifdef ENABLE_LED

#include "pin_registry.h"
#include "message_api.h"
#include "log_tags.h"

// ============================================================================
// INTERNAL STATE
// ============================================================================

struct RGBRequest {
    RGBColor color;
    RGBEffect effect;
    uint16_t period_ms;
    RGBPriority priority;
};

static struct {
    // Current active request
    RGBRequest active;

    // Timing for effects
    uint32_t effectStartTime;

    // Current PWM output values
    uint8_t currentR, currentG, currentB;

    // Priority stack - stores request for each priority level
    // When high priority releases, we restore lower priority state
    RGBRequest priorityStack[5];  // One for each priority level (0, 10, 20, 30, 40)
    bool priorityActive[5];       // Track which priorities have active requests

    // Initialization flag
    bool initialized;
} rgbState;

// ============================================================================
// PLATFORM-SPECIFIC PWM
// ============================================================================

#ifdef ESP32
// ESP32 uses LEDC peripheral for PWM
// Channel assignment: R=0, G=1, B=2
static const uint8_t LEDC_FREQ = 5000;  // 5kHz PWM frequency
static const uint8_t LEDC_RESOLUTION = 8;  // 8-bit resolution (0-255)

static void initPWM() {
    // Configure LEDC channels
    ledcSetup(0, LEDC_FREQ, LEDC_RESOLUTION);  // Red channel
    ledcSetup(1, LEDC_FREQ, LEDC_RESOLUTION);  // Green channel
    ledcSetup(2, LEDC_FREQ, LEDC_RESOLUTION);  // Blue channel

    // Attach pins to channels
    ledcAttachPin(RGB_PIN_R, 0);
    ledcAttachPin(RGB_PIN_G, 1);
    ledcAttachPin(RGB_PIN_B, 2);
}

static void writePWM(uint8_t pin, uint8_t value) {
    // Determine channel from pin
    uint8_t channel;
    if (pin == RGB_PIN_R) channel = 0;
    else if (pin == RGB_PIN_G) channel = 1;
    else channel = 2;

    ledcWrite(channel, value);
}

#else
// Teensy and Arduino use standard analogWrite
static void initPWM() {
    pinMode(RGB_PIN_R, OUTPUT);
    pinMode(RGB_PIN_G, OUTPUT);
    pinMode(RGB_PIN_B, OUTPUT);

    analogWrite(RGB_PIN_R, 0);
    analogWrite(RGB_PIN_G, 0);
    analogWrite(RGB_PIN_B, 0);
}

static void writePWM(uint8_t pin, uint8_t value) {
    analogWrite(pin, value);
}
#endif

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Apply common anode inversion if needed
static inline uint8_t applyPolarity(uint8_t value) {
#ifdef RGB_COMMON_ANODE
    return 255 - value;  // Invert for common anode
#else
    return value;  // Direct for common cathode
#endif
}

// Convert priority enum to array slot (0-4)
static inline uint8_t priorityToSlot(RGBPriority priority) {
    return priority / 10;  // 0->0, 10->1, 20->2, 30->3, 40->4
}

// Find highest active priority and activate its request
static void activateHighestPriority() {
    // Scan from highest to lowest priority
    for (int8_t i = 4; i >= 0; i--) {
        if (rgbState.priorityActive[i]) {
            rgbState.active = rgbState.priorityStack[i];
            rgbState.effectStartTime = millis();
            return;
        }
    }

    // No active requests - turn off
    rgbState.active.effect = EFFECT_OFF;
    rgbState.active.priority = PRIORITY_IDLE;
}

// Set request at priority level
static void setRequest(RGBColor color, RGBEffect effect, uint16_t period_ms, RGBPriority priority) {
    uint8_t slot = priorityToSlot(priority);
    if (slot >= 5) return;  // Safety check

    // Store in priority stack
    rgbState.priorityStack[slot] = {color, effect, period_ms, priority};
    rgbState.priorityActive[slot] = true;

    // Activate if this is highest priority
    if (priority >= rgbState.active.priority) {
        rgbState.active = rgbState.priorityStack[slot];
        rgbState.effectStartTime = millis();
    }
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

void initRGBLed() {
    // Validate pins don't conflict
    if (!validateNoPinConflict(RGB_PIN_R, PIN_OUTPUT, "RGB LED Red")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Red pin %d conflict - LED disabled", RGB_PIN_R);
        return;
    }
    if (!validateNoPinConflict(RGB_PIN_G, PIN_OUTPUT, "RGB LED Green")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Green pin %d conflict - LED disabled", RGB_PIN_G);
        return;
    }
    if (!validateNoPinConflict(RGB_PIN_B, PIN_OUTPUT, "RGB LED Blue")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Blue pin %d conflict - LED disabled", RGB_PIN_B);
        return;
    }

    // Register pins
    registerPin(RGB_PIN_R, PIN_OUTPUT, "RGB LED Red");
    registerPin(RGB_PIN_G, PIN_OUTPUT, "RGB LED Green");
    registerPin(RGB_PIN_B, PIN_OUTPUT, "RGB LED Blue");

    // Initialize PWM hardware
    initPWM();

    // Initialize state
    rgbState.active.effect = EFFECT_OFF;
    rgbState.active.priority = PRIORITY_IDLE;
    rgbState.effectStartTime = millis();
    rgbState.currentR = 0;
    rgbState.currentG = 0;
    rgbState.currentB = 0;

    for (uint8_t i = 0; i < 5; i++) {
        rgbState.priorityActive[i] = false;
    }

    rgbState.initialized = true;

    msg.debug.info(TAG_SYSTEM, "RGB LED indicator initialized (pins R=%d G=%d B=%d)",
                   RGB_PIN_R, RGB_PIN_G, RGB_PIN_B);
}

void rgbLedSolid(RGBColor color, RGBPriority priority) {
    if (!rgbState.initialized) return;
    setRequest(color, EFFECT_SOLID, 0, priority);
}

void rgbLedBlink(RGBColor color, uint16_t period_ms, RGBPriority priority) {
    if (!rgbState.initialized) return;
    setRequest(color, EFFECT_BLINK, period_ms, priority);
}

void rgbLedPulse(RGBColor color, uint16_t period_ms, RGBPriority priority) {
    if (!rgbState.initialized) return;
    setRequest(color, EFFECT_PULSE, period_ms, priority);
}

void rgbLedRelease(RGBPriority priority) {
    if (!rgbState.initialized) return;

    uint8_t slot = priorityToSlot(priority);
    if (slot >= 5) return;

    rgbState.priorityActive[slot] = false;

    // If we just released the active priority, find next highest
    if (priority == rgbState.active.priority) {
        activateHighestPriority();
    }
}

void rgbLedOff() {
    if (!rgbState.initialized) return;

    // Clear all priority requests
    for (uint8_t i = 0; i < 5; i++) {
        rgbState.priorityActive[i] = false;
    }

    activateHighestPriority();
}

RGBPriority rgbLedGetPriority() {
    return rgbState.active.priority;
}

bool rgbLedIsActive() {
    return rgbState.active.effect != EFFECT_OFF;
}

void updateRGBLed() {
    if (!rgbState.initialized) return;

    uint32_t now = millis();
    uint32_t elapsed = now - rgbState.effectStartTime;

    RGBColor output = RGB_OFF;

    switch (rgbState.active.effect) {
        case EFFECT_OFF:
            output = RGB_OFF;
            break;

        case EFFECT_SOLID:
            output = rgbState.active.color;
            break;

        case EFFECT_BLINK: {
            // Square wave: on for half period, off for half
            uint16_t period = rgbState.active.period_ms;
            if (period == 0) {
                // Fallback to solid if period is 0
                output = rgbState.active.color;
            } else {
                uint32_t phase = elapsed % period;
                output = (phase < period / 2) ? rgbState.active.color : RGB_OFF;
            }
            break;
        }

        case EFFECT_PULSE: {
            // Triangular wave for smooth breathing
            uint16_t period = rgbState.active.period_ms;
            if (period == 0) {
                // Fallback to solid if period is 0
                output = rgbState.active.color;
            } else {
                uint32_t phase = elapsed % period;
                uint16_t halfPeriod = period / 2;

                uint8_t brightness;
                if (phase < halfPeriod) {
                    // Ramp up: 0 -> 255
                    brightness = (phase * 255) / halfPeriod;
                } else {
                    // Ramp down: 255 -> 0
                    brightness = ((period - phase) * 255) / halfPeriod;
                }

                // Apply brightness to color
                output.r = (rgbState.active.color.r * brightness) / 255;
                output.g = (rgbState.active.color.g * brightness) / 255;
                output.b = (rgbState.active.color.b * brightness) / 255;
            }
            break;
        }
    }

    // Only update PWM if values changed (reduce overhead)
    if (output.r != rgbState.currentR ||
        output.g != rgbState.currentG ||
        output.b != rgbState.currentB) {

        writePWM(RGB_PIN_R, applyPolarity(output.r));
        writePWM(RGB_PIN_G, applyPolarity(output.g));
        writePWM(RGB_PIN_B, applyPolarity(output.b));

        rgbState.currentR = output.r;
        rgbState.currentG = output.g;
        rgbState.currentB = output.b;
    }
}

#endif // ENABLE_LED
