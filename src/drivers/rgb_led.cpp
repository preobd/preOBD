/*
 * rgb_led.cpp - RGB LED Status Indicator Implementation
 *
 * Non-blocking RGB LED controller with priority system: three PWM channels,
 * or a single NeoPixel where the board has one (RGB_NEOPIXEL_PIN)
 */

#include "rgb_led.h"

#if INCLUDE_LED

#include "../pinctrl/pin_registry.h"
#include "../pinctrl/pin_owner.h"
#include "../core/message_api.h"
#include "../core/log_tags.h"

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
// PLATFORM-SPECIFIC OUTPUT
// ============================================================================

#if defined(RGB_NEOPIXEL_PIN)
// One WS2812 on a single data pin, bit-banged with interrupts masked for the
// 24 bits (~30 us); writeColor() only runs when the color changes. Bits are
// timed on the DWT cycle counter. Not Adafruit_NeoPixel: on STM32 it times
// them by reprogramming SysTick, and restoring it costs millis()/micros() a
// tick per show() -- the CONFIG-mode pulse ran the clock ~28% fast and RPM
// read 25-50% low (#100).
#if !defined(ARDUINO_ARCH_STM32)
#error "RGB_NEOPIXEL_PIN: the WS2812 driver is STM32-only (DWT cycle counter + GPIO BSRR)"
#endif

static GPIO_TypeDef* neoPort;
static uint32_t neoMask;

// Adafruit_NeoPixel::setBrightness(b) scaled by (c * (b + 1)) >> 8; same here
// so the levels chosen with it on the bench still hold.
static inline uint8_t neoScale(uint8_t c) {
    return (uint8_t)(((uint16_t)c * (RGB_NEOPIXEL_BRIGHTNESS + 1)) >> 8);
}

static void writeColor(RGBColor c) {
    const uint8_t grb[3] = { neoScale(c.g), neoScale(c.r), neoScale(c.b) };
    // The caller dedupes before scaling, so a pulse calls this ~255 times a
    // second for only 9 distinct levels at brightness 8; skip the repeats.
    static uint8_t last[3] = { 0xFF, 0xFF, 0xFF };
    if (memcmp(grb, last, sizeof(grb)) == 0) return;
    memcpy(last, grb, sizeof(grb));
    const uint32_t period = SystemCoreClock / 800000;    // 1.25 us bit slot
    const uint32_t t0h = SystemCoreClock / 2500000;      // 0.4 us high = 0
    const uint32_t t1h = SystemCoreClock / 1250000;      // 0.8 us high = 1
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t start = DWT->CYCCNT - period;
    for (uint8_t i = 0; i < 3; i++) {
        for (uint8_t bit = 0x80; bit; bit >>= 1) {
            while (DWT->CYCCNT - start < period) {}
            start = DWT->CYCCNT;
            neoPort->BSRR = neoMask;
            const uint32_t high = (grb[i] & bit) ? t1h : t0h;
            while (DWT->CYCCNT - start < high) {}
            neoPort->BSRR = neoMask << 16;
        }
    }
    while (DWT->CYCCNT - start < period) {}
    __set_PRIMASK(primask);
    // The >280 us low latch is the gap until the next color change.
}

static void initOutput() {
    pinMode(RGB_NEOPIXEL_PIN, OUTPUT);
    digitalWrite(RGB_NEOPIXEL_PIN, LOW);
    neoPort = digitalPinToPort(RGB_NEOPIXEL_PIN);
    neoMask = STM_LL_GPIO_PIN(digitalPinToPinName(RGB_NEOPIXEL_PIN));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   // enable the cycle counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    delayMicroseconds(300);                           // reset/latch before the first frame
    writeColor({0, 0, 0});
}

#elif defined(ESP32)
// ESP32 uses LEDC peripheral for PWM
// Channel assignment: R=0, G=1, B=2
static const uint8_t LEDC_FREQ = 5000;  // 5kHz PWM frequency
static const uint8_t LEDC_RESOLUTION = 8;  // 8-bit resolution (0-255)

static void initOutput() {
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
static void initOutput() {
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

#if !defined(RGB_NEOPIXEL_PIN)

// Apply common anode inversion if needed
static inline uint8_t applyPolarity(uint8_t value) {
#ifdef RGB_COMMON_ANODE
    return 255 - value;  // Invert for common anode
#else
    return value;  // Direct for common cathode
#endif
}

static void writeColor(RGBColor c) {
    writePWM(RGB_PIN_R, applyPolarity(c.r));
    writePWM(RGB_PIN_G, applyPolarity(c.g));
    writePWM(RGB_PIN_B, applyPolarity(c.b));
}
#endif

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

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

    // Callers re-assert their state on every pass (the alarm output does,
    // every 50 ms). Re-requesting what is already showing must not restart
    // it: resetting effectStartTime each time pinned a blink in its "on"
    // half, so alarm and warning blinks showed solid on every board.
    const RGBRequest& a = rgbState.active;
    if (rgbState.priorityActive[slot] && a.priority == priority && a.effect == effect &&
        a.period_ms == period_ms && a.color.r == color.r && a.color.g == color.g &&
        a.color.b == color.b) {
        return;
    }

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
#if defined(RGB_NEOPIXEL_PIN)
    // On-board pixel (board.h): one data pin, BUILTIN rank like the others.
    const uint8_t d = RGB_NEOPIXEL_PIN;
    if (!claimPins(&d, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "RGB LED (NeoPixel)")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED NeoPixel pin %d conflict - LED disabled", RGB_NEOPIXEL_PIN);
        return;
    }
#else
    // Validate all three before registering any (all-or-nothing): a conflict
    // on G must not leave R claimed with the LED never actually enabled.
    // Checked/claimed one channel at a time so a conflict names the specific
    // pin at fault. RGB pins are wiring.ini physical facts (#288), BUILTIN rank.
    const uint8_t r = RGB_PIN_R, g = RGB_PIN_G, b = RGB_PIN_B;
    if (pinsConflict(&r, 1, PIN_RANK_BUILTIN, "RGB LED Red")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Red pin %d conflict - LED disabled", RGB_PIN_R);
        return;
    }
    if (pinsConflict(&g, 1, PIN_RANK_BUILTIN, "RGB LED Green")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Green pin %d conflict - LED disabled", RGB_PIN_G);
        return;
    }
    if (pinsConflict(&b, 1, PIN_RANK_BUILTIN, "RGB LED Blue")) {
        msg.debug.warn(TAG_SYSTEM, "RGB LED Blue pin %d conflict - LED disabled", RGB_PIN_B);
        return;
    }

    // All clear — claim each channel (re-check is idempotent; nothing else
    // could have taken the pins between the check above and here).
    claimPins(&r, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "RGB LED Red");
    claimPins(&g, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "RGB LED Green");
    claimPins(&b, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "RGB LED Blue");

#endif

    // Initialize output hardware
    initOutput();

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

#if defined(RGB_NEOPIXEL_PIN)
    msg.debug.info(TAG_SYSTEM, "RGB LED indicator initialized (NeoPixel on pin %d)", RGB_NEOPIXEL_PIN);
#else
    msg.debug.info(TAG_SYSTEM, "RGB LED indicator initialized (pins R=%d G=%d B=%d)",
                   RGB_PIN_R, RGB_PIN_G, RGB_PIN_B);
#endif
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

bool rgbLedGetActive(RGBColor* color, RGBEffect* effect, RGBPriority* priority) {
    if (!rgbState.initialized || rgbState.active.effect == EFFECT_OFF) return false;
    if (color)    *color    = rgbState.active.color;
    if (effect)   *effect   = rgbState.active.effect;
    if (priority) *priority = rgbState.active.priority;
    return true;
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

    // Only write the LED when the color changed (reduce overhead)
    if (output.r != rgbState.currentR ||
        output.g != rgbState.currentG ||
        output.b != rgbState.currentB) {

        writeColor(output);

        rgbState.currentR = output.r;
        rgbState.currentG = output.g;
        rgbState.currentB = output.b;
    }
}

#endif // INCLUDE_LED
