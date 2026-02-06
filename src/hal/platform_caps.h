/*
 * platform_caps.h - Platform Capability Detection
 * Part of the preOBD Hardware Abstraction Layer
 *
 * Provides compile-time platform capability detection for CAN controllers.
 * This header exposes boolean flags and string identifiers that describe
 * what CAN hardware is available on the current platform.
 *
 * Usage:
 *   #include "hal/platform_caps.h"
 *   #if PLATFORM_HAS_NATIVE_CAN
 *       // Use integrated CAN peripheral
 *   #endif
 *   #if PLATFORM_NEEDS_SPI_CAN
 *       // Define SPI CAN controller pins
 *   #endif
 *
 * This mirrors the logic in hal_can.h but exposes it as queryable capabilities.
 */

#ifndef HAL_PLATFORM_CAPS_H
#define HAL_PLATFORM_CAPS_H

#include <stdint.h>
#include "../lib/bus_defaults.h"

// ============================================================================
// CAN Controller Type Detection
// ============================================================================

// Detect FlexCAN (Teensy 3.x/4.x native CAN peripheral)
#if defined(USE_FLEXCAN_NATIVE) && (defined(__MK20DX256__) || defined(__MK64FX512__) || \
    defined(__MK66FX1M0__) || defined(__IMXRT1062__))

    #define PLATFORM_CAN_CONTROLLER "FlexCAN"
    #define PLATFORM_HAS_NATIVE_CAN 1
    #define PLATFORM_NEEDS_SPI_CAN 0
    // NUM_CAN_BUSES already defined in bus_defaults.h (2-3 for Teensy)

// Detect TWAI (ESP32 native CAN peripheral)
#elif defined(ESP32)

    #define PLATFORM_CAN_CONTROLLER "TWAI"
    #define PLATFORM_HAS_NATIVE_CAN 1
    #define PLATFORM_NEEDS_SPI_CAN 0
    // ESP32 has 1 native TWAI bus (NUM_CAN_BUSES defined in bus_defaults.h)

// Detect STM32 bxCAN (future support)
#elif defined(STM32F4xx) || defined(STM32F1xx)

    #define PLATFORM_CAN_CONTROLLER "bxCAN"
    #define PLATFORM_HAS_NATIVE_CAN 1
    #define PLATFORM_NEEDS_SPI_CAN 0
    // NUM_CAN_BUSES will be defined in bus_defaults.h for STM32 (1-2)

// Default: External SPI CAN controller (MCP2515, MCP25625, SJA1000, etc.)
#else

    #define PLATFORM_CAN_CONTROLLER "SPI"
    #define PLATFORM_HAS_NATIVE_CAN 0
    #define PLATFORM_NEEDS_SPI_CAN 1
    // NUM_CAN_BUSES defined in bus_defaults.h (0-2 for Arduino Mega/Uno)

#endif

// ============================================================================
// Hybrid Controller Mode Detection
// ============================================================================

#if defined(ENABLE_CAN_HYBRID)
    #define PLATFORM_CAN_MODE "Hybrid"
    #define PLATFORM_SUPPORTS_HYBRID 1
#else
    #define PLATFORM_CAN_MODE "Single"
    #define PLATFORM_SUPPORTS_HYBRID 0
#endif

// ============================================================================
// Effective CAN Bus Count
// ============================================================================

// In hybrid mode, the effective bus count may exceed native bus count
// (e.g., ESP32 with 1 TWAI + 1 MCP2515 = 2 buses total)
#if defined(ENABLE_CAN_HYBRID)
    // Hybrid mode can expand beyond native buses
    // Actual count depends on CAN_BUS_x_TYPE definitions
    #if defined(CAN_BUS_3_TYPE)
        #define PLATFORM_EFFECTIVE_CAN_BUSES 4
    #elif defined(CAN_BUS_2_TYPE)
        #define PLATFORM_EFFECTIVE_CAN_BUSES 3
    #elif defined(CAN_BUS_1_TYPE)
        #define PLATFORM_EFFECTIVE_CAN_BUSES 2
    #elif defined(CAN_BUS_0_TYPE)
        #define PLATFORM_EFFECTIVE_CAN_BUSES 1
    #else
        #define PLATFORM_EFFECTIVE_CAN_BUSES NUM_CAN_BUSES
    #endif
#else
    // Single controller mode: effective buses = native buses
    #define PLATFORM_EFFECTIVE_CAN_BUSES NUM_CAN_BUSES
#endif

// ============================================================================
// Platform Voltage
// ============================================================================

// Used for analog sensor calibration and pin compatibility
#if defined(__IMXRT1062__) || defined(__MK20DX256__) || defined(__MK64FX512__) || \
    defined(__MK66FX1M0__) || defined(ESP32) || defined(STM32F4xx) || defined(STM32F1xx)
    #define PLATFORM_VOLTAGE_3V3 1
    #define PLATFORM_VOLTAGE_5V 0
#else
    #define PLATFORM_VOLTAGE_3V3 0
    #define PLATFORM_VOLTAGE_5V 1
#endif

#endif // HAL_PLATFORM_CAPS_H
