/*
 * test_mode.cpp - Test Mode Implementation
 *
 * Core implementation of test mode functionality including:
 * - Function pointer substitution for test value injection
 * - Scenario management and lifecycle
 * - Integration with input manager
 */

#include "../config.h"

#ifdef ENABLE_TEST_MODE

#include "test_mode.h"
#include "test_scenarios.h"
#include "../inputs/input_manager.h"
#include "../lib/message_api.h"
#include "../lib/log_tags.h"
#include <string.h>

// Forward declaration of test value generator
extern float generateTestValue(const InputTestConfig* config, unsigned long elapsedMs);

// Forward declaration of test read function
void readTestInput(Input* ptr);

// ===== GLOBAL TEST MODE STATE =====
TestModeState testModeState;

// ===== HELPER FUNCTIONS =====

// Find the test config for a specific input index in the current scenario
const InputTestConfig* findTestConfigForInput(uint8_t inputIndex) {
    if (!testModeState.isActive || testModeState.currentScenario == nullptr) {
        return nullptr;
    }

    // Read scenario from PROGMEM
    TestScenario scenario;
    memcpy_P(&scenario, testModeState.currentScenario, sizeof(TestScenario));

    // Search through input configs for this input
    for (uint8_t i = 0; i < scenario.numInputOverrides; i++) {
        InputTestConfig config;
        memcpy_P(&config, &scenario.inputConfigs[i], sizeof(InputTestConfig));

        if (config.inputIndex == inputIndex) {
            // Found it - but we need to return a pointer
            // For now, we'll use a static buffer (not thread-safe but OK for single-threaded Arduino)
            static InputTestConfig configBuffer;
            memcpy(&configBuffer, &config, sizeof(InputTestConfig));
            return &configBuffer;
        }
    }

    return nullptr;  // This input is not overridden in the current scenario
}

// ===== TEST READ FUNCTION =====
// This function replaces the normal sensor read functions during test mode
void readTestInput(Input* ptr) {
    // Find which input index this is
    uint8_t inputIndex = 0xFF;
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (&inputs[i] == ptr) {
            inputIndex = i;
            break;
        }
    }

    if (inputIndex == 0xFF || !testModeState.isActive) {
        // Invalid input or test mode not active
        ptr->value = NAN;
        return;
    }

    // Find test config for this input
    const InputTestConfig* config = findTestConfigForInput(inputIndex);
    if (config == nullptr) {
        // No test override for this input - use NaN or could call original function
        ptr->value = NAN;
        return;
    }

    // Check for forced NaN
    if (config->forceNaN) {
        ptr->value = NAN;
        return;
    }

    // Generate test value based on elapsed time
    unsigned long elapsed = millis() - testModeState.scenarioStartTime;
    ptr->value = generateTestValue(config, elapsed);

    // Force alarm if requested (override to exceed threshold)
    if (config->forceAlarm && ptr->flags.alarm) {
        ptr->value = ptr->maxValue + 10.0f;
    }
}

// ===== PUBLIC API IMPLEMENTATION =====

void initTestMode() {
    // Initialize test mode state
    testModeState.isActive = false;
    testModeState.currentScenario = nullptr;
    testModeState.currentScenarioIndex = 0;
    testModeState.scenarioStartTime = 0;

    // Clear original function pointer backup
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        testModeState.originalReadFunctions[i] = nullptr;
    }

    msg.debug.info(TAG_SYSTEM, "Test mode system initialized");
}

bool startTestScenario(uint8_t scenarioIndex) {
    // Check if scenario index is valid
    uint8_t numScenarios = getNumTestScenarios();
    if (scenarioIndex >= numScenarios) {
        msg.control.print(F("ERROR: Invalid scenario index "));
        msg.control.print(scenarioIndex);
        msg.control.print(F(" (max: "));
        msg.control.print(numScenarios - 1);
        msg.control.println(F(")"));
        return false;
    }

    // Stop any currently running scenario first
    if (testModeState.isActive) {
        stopTestMode();
    }

    // Get pointer to scenario in PROGMEM
    testModeState.currentScenario = getTestScenario(scenarioIndex);
    testModeState.currentScenarioIndex = scenarioIndex;
    testModeState.scenarioStartTime = millis();

    // Read scenario info for printing
    TestScenario scenario;
    memcpy_P(&scenario, testModeState.currentScenario, sizeof(TestScenario));

    char nameBuffer[32];
    strncpy_P(nameBuffer, scenario.name, sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    msg.control.println(F("========================================"));
    msg.control.print(F("Starting test scenario "));
    msg.control.print(scenarioIndex);
    msg.control.print(F(": "));
    msg.control.println(nameBuffer);
    msg.control.print(F("Duration: "));
    msg.control.print(scenario.duration_ms / 1000);
    msg.control.println(F(" seconds"));
    msg.control.print(F("Input overrides: "));
    msg.control.println(scenario.numInputOverrides);
    msg.control.println(F("========================================"));

    // Backup original read functions and replace with test function
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (inputs[i].flags.isEnabled) {
            testModeState.originalReadFunctions[i] = inputs[i].readFunction;
            inputs[i].readFunction = readTestInput;
        }
    }

    // Mark test mode as active
    testModeState.isActive = true;

    return true;
}

void stopTestMode() {
    if (!testModeState.isActive) {
        return;  // Already stopped
    }

    msg.control.println(F("========================================"));
    msg.control.println(F("Stopping test mode"));
    msg.control.println(F("========================================"));

    // Restore original read functions
    for (uint8_t i = 0; i < MAX_INPUTS; i++) {
        if (testModeState.originalReadFunctions[i] != nullptr) {
            inputs[i].readFunction = testModeState.originalReadFunctions[i];
            testModeState.originalReadFunctions[i] = nullptr;
        }
    }

    // Clear test mode state
    testModeState.isActive = false;
    testModeState.currentScenario = nullptr;
    testModeState.scenarioStartTime = 0;
}

bool isTestModeActive() {
    return testModeState.isActive;
}

void updateTestMode() {
    if (!testModeState.isActive) {
        return;
    }

    // Check if scenario duration has elapsed
    TestScenario scenario;
    memcpy_P(&scenario, testModeState.currentScenario, sizeof(TestScenario));

    unsigned long elapsed = millis() - testModeState.scenarioStartTime;

    if (elapsed >= scenario.duration_ms) {
        // Scenario complete
        msg.control.println(F(""));
        msg.control.println(F("========================================"));
        msg.control.println(F("Test scenario complete"));
        msg.control.println(F("========================================"));

        // For now, just stop test mode
        // Future enhancement: could auto-advance to next scenario
        stopTestMode();
    }
}

void listTestScenarios() {
    uint8_t numScenarios = getNumTestScenarios();

    msg.control.println(F("========================================"));
    msg.control.println(F("Available Test Scenarios:"));
    msg.control.println(F("========================================"));

    for (uint8_t i = 0; i < numScenarios; i++) {
        const TestScenario* scenarioPtr = getTestScenario(i);
        TestScenario scenario;
        memcpy_P(&scenario, scenarioPtr, sizeof(TestScenario));

        char nameBuffer[32];
        strncpy_P(nameBuffer, scenario.name, sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        msg.control.print(i);
        msg.control.print(F(". "));
        msg.control.print(nameBuffer);
        msg.control.print(F(" ("));
        msg.control.print(scenario.duration_ms / 1000);
        msg.control.print(F("s, "));
        msg.control.print(scenario.numInputOverrides);
        msg.control.println(F(" inputs)"));
    }

    msg.control.println(F("========================================"));
}

uint8_t getNumTestScenarios() {
    return NUM_TEST_SCENARIOS;
}

const char* getTestScenarioName(uint8_t index) {
    if (index >= NUM_TEST_SCENARIOS) {
        return nullptr;
    }

    const TestScenario* scenarioPtr = getTestScenario(index);
    TestScenario scenario;
    memcpy_P(&scenario, scenarioPtr, sizeof(TestScenario));

    // Note: This returns a pointer to PROGMEM, caller must use _P functions
    return scenario.name;
}

#endif // ENABLE_TEST_MODE
