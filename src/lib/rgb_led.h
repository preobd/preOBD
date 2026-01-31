/*
 * rgb_led.h - RGB LED Status Indicator Module
 *
 * Provides a priority-based RGB LED controller with support for:
 * - Multiple color states (alarm, warning, normal, CONFIG mode)
 * - Non-blocking effects (solid, blink, pulse/breathing)
 * - Priority system (alarms override mode indication)
 * - Common cathode/anode support
 * - Platform-specific PWM (Teensy, ESP32, Arduino)
 */

#ifndef RGB_LED_H
#define RGB_LED_H

#include <Arduino.h>
#include "../config.h"

// ============================================================================
// USER-CONFIGURABLE COLORS
// ============================================================================
// Customize these RGB values (0-255) for your preference or accessibility needs
// Examples: colorblind-friendly palettes, personal preferences

// Normal operation (default: green)
#define RGB_COLOR_NORMAL_R   0
#define RGB_COLOR_NORMAL_G   255
#define RGB_COLOR_NORMAL_B   0

// Warning alarm (default: yellow/orange)
#define RGB_COLOR_WARNING_R  255
#define RGB_COLOR_WARNING_G  180
#define RGB_COLOR_WARNING_B  0

// Critical alarm (default: red)
#define RGB_COLOR_ALARM_R    255
#define RGB_COLOR_ALARM_G    0
#define RGB_COLOR_ALARM_B    0

// CONFIG mode indication (default: blue)
#define RGB_COLOR_CONFIG_R   0
#define RGB_COLOR_CONFIG_G   0
#define RGB_COLOR_CONFIG_B   255

// Activity indicator (SD write, pairing, etc.) (default: cyan)
#define RGB_COLOR_ACTIVITY_R 0
#define RGB_COLOR_ACTIVITY_G 255
#define RGB_COLOR_ACTIVITY_B 255

// ============================================================================
// EFFECT PREFERENCES
// ============================================================================
// Enable/disable visual effects - set to false if flashing is undesirable

#define RGB_ALARM_USE_BLINK  true   // true = blink on alarm, false = solid
#define RGB_CONFIG_USE_PULSE true   // true = pulse in CONFIG, false = solid

// ============================================================================
// EFFECT TIMING
// ============================================================================
// Adjust these values to change blink/pulse speeds (milliseconds)

#define RGB_BLINK_PERIOD_MS 500     // Standard blink: 1Hz (500ms on, 500ms off)
#define RGB_FAST_BLINK_MS   200     // Fast blink: 2.5Hz (urgent alarms)
#define RGB_PULSE_PERIOD_MS 2000    // Breathing cycle: 0.5Hz

// ============================================================================
// RGB COLOR STRUCTURE
// ============================================================================

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    RGBColor(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0)
        : r(red), g(green), b(blue) {}
};

// Standard color constants
static const RGBColor RGB_OFF     = {0, 0, 0};
static const RGBColor RGB_RED     = {255, 0, 0};
static const RGBColor RGB_GREEN   = {0, 255, 0};
static const RGBColor RGB_BLUE    = {0, 0, 255};
static const RGBColor RGB_YELLOW  = {255, 180, 0};
static const RGBColor RGB_ORANGE  = {255, 80, 0};
static const RGBColor RGB_CYAN    = {0, 255, 255};
static const RGBColor RGB_MAGENTA = {255, 0, 255};
static const RGBColor RGB_WHITE   = {255, 255, 255};

// User-configured colors (from defines above)
static const RGBColor RGB_COLOR_NORMAL   = {RGB_COLOR_NORMAL_R, RGB_COLOR_NORMAL_G, RGB_COLOR_NORMAL_B};
static const RGBColor RGB_COLOR_WARNING  = {RGB_COLOR_WARNING_R, RGB_COLOR_WARNING_G, RGB_COLOR_WARNING_B};
static const RGBColor RGB_COLOR_ALARM    = {RGB_COLOR_ALARM_R, RGB_COLOR_ALARM_G, RGB_COLOR_ALARM_B};
static const RGBColor RGB_COLOR_CONFIG   = {RGB_COLOR_CONFIG_R, RGB_COLOR_CONFIG_G, RGB_COLOR_CONFIG_B};
static const RGBColor RGB_COLOR_ACTIVITY = {RGB_COLOR_ACTIVITY_R, RGB_COLOR_ACTIVITY_G, RGB_COLOR_ACTIVITY_B};

// ============================================================================
// EFFECT TYPES
// ============================================================================

enum RGBEffect : uint8_t {
    EFFECT_OFF = 0,       // LED off
    EFFECT_SOLID,         // Steady color
    EFFECT_BLINK,         // On/off blinking
    EFFECT_PULSE          // Smooth fade in/out (breathing)
};

// ============================================================================
// PRIORITY LEVELS
// ============================================================================
// Higher priority sources override lower priority
// This ensures alarms always take precedence over mode indication

enum RGBPriority : uint8_t {
    PRIORITY_IDLE = 0,        // Default/background state
    PRIORITY_MODE = 10,       // System mode indication (CONFIG vs RUN)
    PRIORITY_ACTIVITY = 20,   // SD write, pairing, task feedback
    PRIORITY_WARNING = 30,    // Warning level alarm
    PRIORITY_ALARM = 40       // Critical alarm (highest priority)
};

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Initialize RGB LED hardware
 * Configures PWM pins and registers with pin_registry
 * Must be called once in setup() before using other functions
 */
void initRGBLed();

/**
 * Set LED to solid color
 * @param color Target color (RGBColor struct)
 * @param priority Priority level - only takes effect if >= current active priority
 */
void rgbLedSolid(RGBColor color, RGBPriority priority);

/**
 * Set LED to blinking effect
 * @param color Target color (blinks between color and off)
 * @param period_ms Full blink cycle time in milliseconds
 * @param priority Priority level
 */
void rgbLedBlink(RGBColor color, uint16_t period_ms, RGBPriority priority);

/**
 * Set LED to pulsing/breathing effect
 * @param color Target color at peak brightness
 * @param period_ms Full pulse cycle time in milliseconds
 * @param priority Priority level
 */
void rgbLedPulse(RGBColor color, uint16_t period_ms, RGBPriority priority);

/**
 * Release control at specified priority level
 * Allows lower priority sources to take over
 * @param priority Priority level to release
 */
void rgbLedRelease(RGBPriority priority);

/**
 * Force LED off (emergency/debug use)
 */
void rgbLedOff();

/**
 * Update LED output - handles effects timing
 * MUST be called every loop iteration for effects to work
 */
void updateRGBLed();

/**
 * Get current active priority
 * Useful for debugging or status checks
 */
RGBPriority rgbLedGetPriority();

/**
 * Check if LED is currently active (not off)
 */
bool rgbLedIsActive();

#endif // RGB_LED_H
