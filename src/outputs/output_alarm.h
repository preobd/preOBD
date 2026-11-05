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

// Silence the buzzer and release the alarm/warning LED (output disabled, #610)
void stopAlarmOutput();

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
 * Check if any sounding alarm is currently acknowledged (silenced).
 * @return true while at least one ACTIVE ALARM rule holds an acknowledgement
 */
bool isAlarmSilenced();

// ===== ACKNOWLEDGEMENT (#433, ARCHITECTURE §11.4) =====
// The mode button's short press and the `ALARM SILENCE` command both land
// here, so the two cannot diverge. Acknowledges every rule sounding the buzzer
// NOW; a rule that trips later still sounds. Returns how many rules are acked.
uint8_t alarmAcknowledge();   // returns how many alarms were newly acknowledged

// Expire lapsed acknowledgements against the configured policy. Runs from the
// alarm output module's update, and from the ALARM command so the reported
// state is current even if that output module is disabled.
void alarmAckHousekeeping();

// Policy, decoded from systemConfig.alarmSilence (a legacy 0 = today's
// default: buzzer only, SILENCE_DURATION — 0 ms there means until clear).
#define ALARM_SILENCE_LED_BIT     0x80   // SCOPE ALL: acked alarm shows steady, not blinking
#define ALARM_SILENCE_DUR_MASK    0x7F
#define ALARM_SILENCE_DUR_DEFAULT 0      // SILENCE_DURATION (config.h)
#define ALARM_SILENCE_DUR_CLEAR   127    // hold until the rule returns to normal
#define ALARM_SILENCE_DUR_STEP_S  5      // codes 1..126 = n * 5 s (5 s .. 630 s)
#define ALARM_SILENCE_DUR_MAX_S   (126 * ALARM_SILENCE_DUR_STEP_S)
uint32_t alarmSilenceDurationMs();       // 0 = until clear
bool     alarmSilenceIncludesLed();
// Encode a policy edit. `seconds` 0 = until clear; must otherwise be a multiple
// of ALARM_SILENCE_DUR_STEP_S in [5, 630]. Returns false (config untouched) if not.
bool     alarmSilenceSetDuration(uint16_t seconds);
void     alarmSilenceSetLed(bool includeLed);

#endif // OUTPUT_ALARM_H
