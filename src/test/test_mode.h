/*
 * test_mode.h - Test Mode System for openEMS
 *
 * Provides a comprehensive testing framework that simulates sensor inputs
 * without requiring physical sensors. Uses function pointer substitution
 * to inject test values while preserving all existing sensor reading code.
 *
 * Features:
 * - Pre-defined test scenarios (normal operation, alarms, faults, etc.)
 * - Dynamic time-based value generation (ramps, waves, random walks)
 * - Zero overhead when ENABLE_TEST_MODE is not defined
 */

#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <Arduino.h>
#include "../inputs/input.h"
#include "../inputs/input_manager.h"  // For MAX_INPUTS

// ===== TEST VALUE TYPES =====
// Different ways to generate test values over time
enum TestValueType {
    TEST_STATIC,        // Constant value
    TEST_RAMP_UP,       // Linear increase from value1 to value2
    TEST_RAMP_DOWN,     // Linear decrease from value2 to value1
    TEST_SINE_WAVE,     // Sinusoidal oscillation between value1 and value2
    TEST_SQUARE_WAVE,   // Step changes between value1 and value2
    TEST_RANDOM,        // Random walk within bounds [value1, value2]
    TEST_NAN            // Always return NaN (sensor fault simulation)
};

// ===== PER-INPUT TEST CONFIGURATION =====
// Defines how to simulate one input during a test scenario
struct InputTestConfig {
    uint8_t inputIndex;         // Which input in the inputs[] array (0-MAX_INPUTS)
    TestValueType valueType;    // Type of value generation
    float value1;               // Start value / static value / min value
    float value2;               // End value / max value (unused for STATIC/NAN)
    float period_ms;            // Period for oscillations/ramps (milliseconds)
    bool forceAlarm;            // Force value to exceed alarm threshold
    bool forceNaN;              // Override valueType and force NaN
};

// ===== TEST SCENARIO =====
// A complete test scenario with multiple input configurations
// Stored in PROGMEM to save RAM
struct TestScenario {
    const char* name;                       // Scenario name (e.g., "Normal Operation")
    uint32_t duration_ms;                   // Scenario duration in milliseconds (use uint32_t for >65535)
    uint8_t numInputOverrides;              // Number of inputs being simulated
    const InputTestConfig* inputConfigs;    // Pointer to input config array (in PROGMEM)
};

// ===== TEST MODE STATE =====
// Global test mode state (maintained in RAM during test execution)
struct TestModeState {
    bool isActive;                          // Is test mode currently running?
    const TestScenario* currentScenario;    // Pointer to current scenario (PROGMEM)
    uint8_t currentScenarioIndex;           // Index of current scenario
    unsigned long scenarioStartTime;        // millis() when scenario started
    void (*originalReadFunctions[MAX_INPUTS])(Input*);  // Backup of original function pointers
};

// ===== PUBLIC API =====

// Initialize test mode system (must be called before using test mode)
void initTestMode();

// Start a specific test scenario by index
// Returns true if successful, false if invalid index
bool startTestScenario(uint8_t scenarioIndex);

// Stop test mode and restore original sensor reading functions
void stopTestMode();

// Check if test mode is currently active
bool isTestModeActive();

// Update test mode (called in main loop)
// Checks for scenario completion, updates elapsed time, etc.
void updateTestMode();

// List all available test scenarios to Serial
void listTestScenarios();

// Get total number of available scenarios
uint8_t getNumTestScenarios();

// Get name of a specific scenario
const char* getTestScenarioName(uint8_t index);

#endif // TEST_MODE_H
