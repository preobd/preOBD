/*
 * alarm_logic.h - Per-input alarm state evaluation
 *
 * Evaluates alarm conditions and updates Input.alarmContext and Input.flags.isInAlarm.
 * This module does NOT control hardware (buzzers, LEDs, etc.) - that's handled by output modules.
 *
 * Architecture:
 * - Each Input has its own alarm state machine (INIT → WARMUP → READY → ACTIVE)
 * - Warmup period prevents false alarms during cold start
 * - Persistence time prevents false alarms from transient sensor noise
 * - Alarm state stored in Input.flags.isInAlarm for consumption by output modules
 */

#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H

#include "input.h"

/**
 * Initialize alarm context for an input
 * Called when application is set or input is configured
 *
 * @param input Pointer to Input structure
 * @param now Current time in milliseconds (millis())
 * @param warmupTime_ms Warmup duration in milliseconds (from ApplicationPreset)
 * @param persistTime_ms Fault persistence time in milliseconds (from ApplicationPreset)
 */
void initInputAlarmContext(Input* input, uint32_t now, uint16_t warmupTime_ms, uint16_t persistTime_ms);

/**
 * Update alarm state for a single input
 * Runs the alarm state machine and updates Input.flags.isInAlarm
 *
 * @param input Pointer to Input structure
 * @param now Current time in milliseconds (millis())
 */
void updateInputAlarmState(Input* input, uint32_t now);

/**
 * Update alarm state for all enabled inputs
 * Convenience function that calls updateInputAlarmState() for all active inputs
 *
 * @param now Current time in milliseconds (millis())
 */
void updateAllInputAlarms(uint32_t now);

#endif // ALARM_LOGIC_H
