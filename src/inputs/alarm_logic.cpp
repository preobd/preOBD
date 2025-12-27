/*
 * alarm_logic.cpp - Per-input alarm state evaluation
 *
 * This module updates Input.alarmContext and Input.flags.isInAlarm
 * but does NOT control hardware (buzzers, LEDs, etc.)
 *
 * Hardware control is handled by output modules (output_alarm.cpp).
 *
 * ALARM STATE MACHINE:
 *
 *   DISABLED ──────────────────────────────────────┐
 *      │                                            │
 *      │ (alarm enabled)                            │
 *      ↓                                            │
 *   INIT (1 second stabilization)                  │
 *      │                                            │
 *      ↓                                            │
 *   WARMUP (sensor-specific duration)              │
 *      │                                            │
 *      │ (sensor valid + warmup expired)           │
 *      ↓                                            │
 *   READY ←──────────────────────┐                 │
 *      │                          │                 │
 *      │ (violation + persist)    │ (value normal) │
 *      ↓                          │                 │
 *   ACTIVE ────────────────────── ┘                │
 *      │                                            │
 *      │ (alarm disabled)                          │
 *      └────────────────────────────────────────────┘
 */

#include "alarm_logic.h"
#include "../config.h"
#include "input_manager.h"

// Initialize alarm context for an input
void initInputAlarmContext(Input* input, uint32_t now, uint16_t warmupTime_ms, uint16_t persistTime_ms) {
    // Set initial state based on whether alarm is enabled
    input->alarmContext.state = (input->flags.alarm) ? ALARM_INIT : ALARM_DISABLED;
    input->alarmContext.stateEntryTime = now;
    input->alarmContext.faultStartTime = 0;
    input->alarmContext.warmupTime_ms = warmupTime_ms;
    input->alarmContext.persistTime_ms = persistTime_ms;
    input->flags.isInAlarm = false;
    input->currentSeverity = SEVERITY_NORMAL;
}

// Evaluate alarm severity for an input
AlarmSeverity evaluateSeverity(Input* input, uint32_t now) {
    // Safety: NaN values are treated as normal
    if (isnan(input->value)) {
        return SEVERITY_NORMAL;
    }

    AlarmContext* ctx = &input->alarmContext;

    // During warmup/init/disabled, always return normal
    if (ctx->state == ALARM_WARMUP || ctx->state == ALARM_INIT || ctx->state == ALARM_DISABLED) {
        return SEVERITY_NORMAL;
    }

    bool alarmViolation = false;
    bool warningViolation = false;

    // Check high-side threshold (maxValue)
    if (input->maxValue < 999) {  // Threshold enabled (999 is disabled marker)
        float warningMax = input->maxValue * (WARNING_THRESHOLD_PERCENT / 100.0);
        if (input->value >= input->maxValue) {
            alarmViolation = true;
        } else if (input->value >= warningMax) {
            warningViolation = true;
        }
    }

    // Check low-side threshold (minValue)
    if (input->minValue > -999) {  // Threshold enabled (-999 is disabled marker)
        float warningMin = input->minValue + (input->minValue * (100 - WARNING_THRESHOLD_PERCENT) / 100.0);
        if (input->value <= input->minValue) {
            alarmViolation = true;
        } else if (input->value <= warningMin) {
            warningViolation = true;
        }
    }

    // Return worst-case severity
    if (alarmViolation && ctx->faultStartTime > 0 && (now - ctx->faultStartTime >= ctx->persistTime_ms)) {
        return SEVERITY_ALARM;
    } else if (warningViolation || (alarmViolation && ctx->faultStartTime > 0)) {
        return SEVERITY_WARNING;  // Warning triggers immediately OR during alarm persist wait
    }

    return SEVERITY_NORMAL;
}

// Update alarm state for a single input
void updateInputAlarmState(Input* input, uint32_t now) {
    // Quick exit if alarm disabled or input not enabled
    if (!input->flags.alarm || !input->flags.isEnabled) {
        input->flags.isInAlarm = false;
        input->alarmContext.state = ALARM_DISABLED;
        return;
    }

    AlarmContext* ctx = &input->alarmContext;

    // ===== STATE MACHINE =====
    switch (ctx->state) {
        case ALARM_DISABLED:
            // Should not reach here if flags.alarm is true, but handle gracefully
            input->flags.isInAlarm = false;
            return;

        case ALARM_INIT:
            // Brief initialization period to allow sensor reads to stabilize
            // This prevents alarm triggers from uninitialized sensor values
            if (now - ctx->stateEntryTime > 1000) {  // 1 second init period
                ctx->state = ALARM_WARMUP;
                ctx->stateEntryTime = now;
            }
            input->flags.isInAlarm = false;
            return;

        case ALARM_WARMUP:
            // Block alarms until warmup period expires
            // Also require sensor to produce valid readings (not NaN)
            // This prevents false alarms during cold start
            if (!isnan(input->value) &&
                (now - ctx->stateEntryTime > ctx->warmupTime_ms)) {
                ctx->state = ALARM_READY;
                ctx->stateEntryTime = now;
            }
            input->flags.isInAlarm = false;
            return;

        case ALARM_READY:
        case ALARM_ACTIVE:
            // Fall through to alarm qualification logic below
            break;
    }

    // ===== ALARM QUALIFICATION LOGIC =====
    // Only active when state is READY or ACTIVE

    // Check if currently violating threshold (with NaN safety)
    bool violating = false;
    if (!isnan(input->value)) {
        violating = (input->value >= input->maxValue || input->value <= input->minValue);
    }

    if (violating) {
        // Start fault timer if not already started
        if (ctx->faultStartTime == 0) {
            ctx->faultStartTime = now;
        }

        // Check if fault has persisted long enough
        // This prevents false alarms from transient sensor noise/spikes
        if (now - ctx->faultStartTime >= ctx->persistTime_ms) {
            input->flags.isInAlarm = true;
            ctx->state = ALARM_ACTIVE;
        }
        // else: violation detected but not persistent yet, wait
    } else {
        // Value returned to normal - reset fault timer
        ctx->faultStartTime = 0;
        input->flags.isInAlarm = false;

        // Transition back to READY state if we were in ACTIVE
        if (ctx->state == ALARM_ACTIVE) {
            ctx->state = ALARM_READY;
        }
    }

    // Update severity level (new feature)
    input->currentSeverity = evaluateSeverity(input, now);
}

// Update alarm state for all enabled inputs
void updateAllInputAlarms(uint32_t now) {
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            updateInputAlarmState(&inputs[i], now);
        }
    }
}
