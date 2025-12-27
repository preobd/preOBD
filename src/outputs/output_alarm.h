/*
 * output_alarm.h - Alarm output module
 *
 * Hardware control for alarm outputs (buzzer, LEDs, etc.)
 * This is a true output module that integrates with output_manager.
 */

#ifndef OUTPUT_ALARM_H
#define OUTPUT_ALARM_H

#include "../inputs/input.h"

// ===== OUTPUT MODULE INTERFACE =====
// These functions are called by output_manager

/**
 * Initialize alarm output hardware
 * Sets up buzzer pin, silence button, etc.
 */
void initAlarmOutput();

/**
 * Send alarm output for a specific input
 * Required by output_manager interface, but not used
 * (Alarm decision is made by scanning ALL inputs in updateAlarmOutput)
 *
 * @param input Pointer to Input (unused but required by interface)
 */
void sendAlarmOutput(Input* input);

/**
 * Update alarm output (called every loop)
 * Scans all inputs for alarm state, handles silence button, controls buzzer
 */
void updateAlarmOutput();

// ===== QUERY FUNCTIONS =====
// These functions allow other modules (LCD, etc.) to check alarm status

/**
 * Get worst-case alarm severity across all enabled inputs
 * @return AlarmSeverity (NORMAL, WARNING, or ALARM)
 */
AlarmSeverity getSystemSeverity();

/**
 * Check if any input is currently in alarm
 * @return true if at least one enabled input has isInAlarm flag set
 */
bool isAnyAlarmActive();

/**
 * Check if alarm output is currently silenced
 * @return true if silence button was pressed and timeout hasn't expired
 */
bool isAlarmSilenced();

#endif // OUTPUT_ALARM_H
