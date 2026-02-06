# CAN Hardware Abstraction Layer (HAL) Architecture

This document describes the CAN controller abstraction layer that enables preOBD to support multiple CAN controller types across different platforms.

## Table of Contents

1. [Overview](#overview)
2. [Design Goals](#design-goals)
3. [Architecture](#architecture)
4. [Controller Types](#controller-types)
5. [Platform Capabilities](#platform-capabilities)
6. [Hybrid Mode](#hybrid-mode)
7. [Implementation Details](#implementation-details)
8. [Adding New Controllers](#adding-new-controllers)

## Overview

### What is the CAN HAL?

The CAN HAL is a compile-time abstraction layer that provides a unified API for CAN bus operations regardless of the underlying hardware controller. It supports:

- **Native controllers** (FlexCAN, TWAI, bxCAN) - Integrated CAN peripherals in the microcontroller
- **External controllers** (MCP2515, MCP25625, SJA1000) - SPI-based CAN controllers
- **Hybrid mode** - Mixing different controller types on different buses

### Why Was This Needed?

**Before (Pre-HAL):**
- Platform-specific CAN code scattered throughout the codebase
- Manual conditional compilation (`#ifdef USE_FLEXCAN_NATIVE`)
- No way to mix controller types
- Adding new platforms required extensive changes

**After (With HAL):**
- Single unified API (`hal::can::begin()`, `hal::can::write()`, etc.)
- Platform detection happens automatically at compile-time
- Hybrid mode allows mixing controllers (e.g., ESP32 TWAI + MCP2515)
- Adding new controllers requires only implementing the HAL interface

## Design Goals

1. **Zero Runtime Overhead** - All platform selection happens at compile-time via templates/constexpr
2. **Type Safety** - Controller types are strongly typed enums
3. **Extensibility** - Easy to add new controller types without modifying existing code
4. **Explicit Configuration** - No hidden auto-detection, clear build flags
5. **Hybrid Support** - Allow mixing different controller types on different buses

## Architecture

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│                      Application Layer                       │
│         (bus_manager.cpp, output_can.cpp, etc.)             │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────────┐
│                     CAN HAL Interface                        │
│  namespace hal::can { begin(), write(), read(), ... }       │
│  Unified API independent of controller type                 │
└───────────────────────┬─────────────────────────────────────┘
                        │
         ┌──────────────┼──────────────┬──────────────┐
         ▼              ▼              ▼              ▼
┌────────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
│ FlexCAN        │ │ TWAI       │ │ MCP2515    │ │ bxCAN       │
│ Implementation │ │ Impl       │ │ Impl       │ │ (planned)   │
└────────────────┘ └────────────┘ └────────────┘ └─────────────┘
         │              │              │              │
         ▼              ▼              ▼              ▼
┌────────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
│ FlexCAN_T4     │ │ ESP32-TWAI │ │ autowp-    │ │ STM32 HAL   │
│ Library        │ │ Library    │ │ mcp2515    │ │             │
└────────────────┘ └────────────┘ └────────────┘ └─────────────┘
```

### File Structure

```
src/hal/
├── hal_can.h                    # Unified HAL interface
├── platform_caps.h              # Platform capability detection
├── platforms/
│   ├── can_flexcan.h           # Teensy FlexCAN implementation
│   ├── can_twai.h              # ESP32 TWAI implementation
│   ├── can_mcp2515.h           # MCP2515 SPI implementation
│   └── can_hybrid.h            # Hybrid mode dispatcher

src/lib/
├── can_controller_types.h       # Controller type enum and mapping
└── bus_defaults.h              # Default bus count per platform
```

## Controller Types

### Supported Controllers

```cpp
enum class CanControllerType : uint8_t {
    NONE = 0,       // No controller / disabled
    FLEXCAN = 1,    // Teensy native FlexCAN
    TWAI = 2,       // ESP32 native TWAI
    MCP2515 = 3,    // External SPI (MCP2515, MCP25625)
    BXCAN = 4,      // STM32 native bxCAN (future)
    SJA1000 = 5     // External SPI SJA1000 (future)
};
```

### Controller Capabilities

| Controller | Buses | Speed | Hardware | Transceiver | Notes |
|------------|-------|-------|----------|-------------|-------|
| **FlexCAN** | 2-3 | 1 Mbps | Teensy 3.6/4.0/4.1 | External | High performance, multiple buses |
| **TWAI** | 1 | 1 Mbps | ESP32/ESP32-S3 | External | ESP32 integrated peripheral |
| **MCP2515** | 1-2 | 1 Mbps | SPI (any platform) | External | Requires SPI pins |
| **bxCAN** | 1-2 | 1 Mbps | STM32F1/F4 | External | Planned support |

## Platform Capabilities

### Compile-Time Detection

Platform capabilities are detected via `src/hal/platform_caps.h`:

```cpp
// Automatically defined based on platform and build flags
#define PLATFORM_CAN_CONTROLLER "FlexCAN"  // or "TWAI", "SPI", "bxCAN"
#define PLATFORM_HAS_NATIVE_CAN 1          // 1 = integrated, 0 = external
#define PLATFORM_NEEDS_SPI_CAN 0           // 1 = requires SPI pin config
#define PLATFORM_SUPPORTS_HYBRID 0         // 1 = hybrid mode enabled
#define PLATFORM_EFFECTIVE_CAN_BUSES 2     // Total buses available
```

### Platform Detection Logic

```cpp
// FlexCAN detection
#if defined(USE_FLEXCAN_NATIVE) && defined(__IMXRT1062__)
    #define PLATFORM_CAN_CONTROLLER "FlexCAN"
    #define PLATFORM_HAS_NATIVE_CAN 1

// TWAI detection
#elif defined(ESP32)
    #define PLATFORM_CAN_CONTROLLER "TWAI"
    #define PLATFORM_HAS_NATIVE_CAN 1

// STM32 bxCAN detection
#elif defined(STM32F4xx) || defined(STM32F1xx)
    #define PLATFORM_CAN_CONTROLLER "bxCAN"
    #define PLATFORM_HAS_NATIVE_CAN 1

// Default: External SPI
#else
    #define PLATFORM_CAN_CONTROLLER "SPI"
    #define PLATFORM_HAS_NATIVE_CAN 0
    #define PLATFORM_NEEDS_SPI_CAN 1
#endif
```

## Hybrid Mode

### What is Hybrid Mode?

Hybrid mode allows different CAN controller types on different buses. For example:
- ESP32 TWAI (bus 0) + MCP2515 (bus 1)
- Teensy 4.1 with 3 FlexCAN buses (0-2) + MCP2515 (bus 3)

### Build Configuration

Enable via platformio.ini:

```ini
build_flags =
    -D ENABLE_CAN_HYBRID                        # Enable hybrid support
    -D CAN_BUS_0_TYPE=CanControllerType::TWAI   # Explicit bus 0 controller
    -D CAN_BUS_1_TYPE=CanControllerType::MCP2515 # Explicit bus 1 controller
```

### Controller Mapping

The mapping is defined at compile-time in `src/lib/can_controller_types.h`:

```cpp
// Bus 0 controller type
#if defined(CAN_BUS_0_TYPE)
    #define CAN_CONTROLLER_BUS_0 CAN_BUS_0_TYPE  // Explicit override
#elif defined(USE_FLEXCAN_NATIVE) && defined(__IMXRT1062__)
    #define CAN_CONTROLLER_BUS_0 CanControllerType::FLEXCAN  // Auto-detect
#elif defined(ESP32)
    #define CAN_CONTROLLER_BUS_0 CanControllerType::TWAI
#else
    #define CAN_CONTROLLER_BUS_0 CanControllerType::MCP2515
#endif

// Helper function
constexpr CanControllerType getBusControllerType(uint8_t bus) {
    switch (bus) {
        case 0: return CAN_CONTROLLER_BUS_0;
        case 1: return CAN_CONTROLLER_BUS_1;
        case 2: return CAN_CONTROLLER_BUS_2;
        case 3: return CAN_CONTROLLER_BUS_3;
        default: return CanControllerType::NONE;
    }
}
```

### Hybrid Dispatcher

In hybrid mode, the HAL dispatcher routes calls to the correct controller:

```cpp
// src/hal/platforms/can_hybrid.h
namespace hal { namespace can {

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    switch (getBusControllerType(bus)) {
        case CanControllerType::FLEXCAN:
            return flexcan::write(id, data, len, extended, bus);
        case CanControllerType::TWAI:
            return twai::write(id, data, len, extended, bus);
        case CanControllerType::MCP2515:
            return mcp2515::write(id, data, len, extended, bus);
        default:
            return false;
    }
}

}} // namespace hal::can
```

## Implementation Details

### HAL Interface

All controller implementations provide the same interface:

```cpp
namespace hal { namespace can {

// Initialize CAN controller for a given bus
// listenOnly: Enable passive monitoring mode (no ACK, no TX)
bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false);

// Shutdown CAN controller
void end(uint8_t bus = 0);

// Write a CAN frame
bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0);

// Read a CAN frame (non-blocking)
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0);

// Check if data is available
int available(uint8_t bus = 0);

// Set CAN filters (controller-specific)
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0);

}} // namespace hal::can
```

### Controller-Specific Implementation

Each controller provides its own implementation in `src/hal/platforms/can_*.h`:

**Example: FlexCAN Implementation**
```cpp
// src/hal/platforms/can_flexcan.h
#include <FlexCAN_T4.h>

namespace hal { namespace can { namespace flexcan {

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can0;
FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> can1;

bool begin(uint32_t baudrate, uint8_t bus, bool listenOnly = false) {
    FLEXCAN_RXQUEUE_TABLE mode = listenOnly ? LISTEN_ONLY : TX;
    if (bus == 0) {
        can0.begin();
        can0.setBaudRate(baudrate, mode);
        return true;
    } else if (bus == 1) {
        can1.begin();
        can1.setBaudRate(baudrate, mode);
        return true;
    }
    return false;
}

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    CAN_message_t msg;
    msg.id = id;
    msg.len = len;
    msg.flags.extended = extended ? 1 : 0;
    memcpy(msg.buf, data, len);

    if (bus == 0) return can0.write(msg) == 1;
    if (bus == 1) return can1.write(msg) == 1;
    return false;
}

}}} // namespace hal::can::flexcan
```

**Note on FlexCAN listen-only mode:** The FlexCAN_T4 library's `setBaudRate()` accepts an optional second parameter for operating mode (`TX`, `LISTEN_ONLY`, etc.). This allows setting baudrate and mode in a single call, which is used to support the `listenOnly` parameter in `hal::can::begin()`.

### Memory Footprint

| Component | Flash (bytes) | RAM (bytes) |
|-----------|---------------|-------------|
| HAL interface (constexpr dispatch) | ~500 | 0 |
| FlexCAN implementation | ~2KB | 512 per bus |
| TWAI implementation | ~1.5KB | 256 per bus |
| MCP2515 implementation | ~2KB | 128 per bus |
| Hybrid dispatcher | ~300 | 0 |

## Adding New Controllers

### Step 1: Define Controller Type

Add to `src/lib/can_controller_types.h`:

```cpp
enum class CanControllerType : uint8_t {
    // ... existing types ...
    SJA1000 = 5     // New controller type
};
```

### Step 2: Create Controller Implementation

Create `src/hal/platforms/can_sja1000.h`:

```cpp
#ifndef HAL_CAN_SJA1000_H
#define HAL_CAN_SJA1000_H

#include <SJA1000.h>  // Hypothetical library

namespace hal { namespace can { namespace sja1000 {

bool begin(uint32_t baudrate, uint8_t bus = 0);
void end(uint8_t bus = 0);
bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0);
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0);
int available(uint8_t bus = 0);
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0);

}}} // namespace hal::can::sja1000

#endif
```

### Step 3: Add to Platform Capabilities

Update `src/hal/platform_caps.h` if auto-detection is needed:

```cpp
#elif defined(USE_SJA1000)
    #define PLATFORM_CAN_CONTROLLER "SJA1000"
    #define PLATFORM_HAS_NATIVE_CAN 0
    #define PLATFORM_NEEDS_SPI_CAN 1
```

### Step 4: Add to Hybrid Dispatcher

Update `src/hal/platforms/can_hybrid.h`:

```cpp
#include "can_sja1000.h"

bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus) {
    switch (getBusControllerType(bus)) {
        // ... existing cases ...
        case CanControllerType::SJA1000:
            return sja1000::write(id, data, len, extended, bus);
        default:
            return false;
    }
}
```

### Step 5: Add Library Dependency

Update `platformio.ini`:

```ini
[sja1000_lib]
lib_deps =
    https://github.com/example/SJA1000-CAN.git

[env:custom_sja1000]
build_flags =
    -D ENABLE_CAN
    -D CAN_BUS_0_TYPE=CanControllerType::SJA1000
lib_deps =
    ${sja1000_lib.lib_deps}
```

### Step 6: Test

1. Build and flash
2. Verify controller initialization
3. Test send/receive operations
4. Test with different baud rates
5. Test filter configuration

## Best Practices

### For Application Code

1. **Use the unified HAL API** - Never call controller-specific functions directly
2. **Check return values** - Always verify `begin()` and `write()` return values
3. **Handle bus parameter** - Always pass the correct bus number
4. **Don't assume controller type** - HAL hides controller differences

### For Controller Implementations

1. **Match the HAL interface exactly** - All functions must have identical signatures
2. **Non-blocking operations** - `read()` and `available()` must never block
3. **Proper error handling** - Return false on failure, true on success
4. **Bus range checking** - Validate bus number before use
5. **Resource cleanup** - `end()` must release all resources

### For Hybrid Mode

1. **Explicit configuration** - Always use build flags to specify controller types
2. **Pin conflicts** - Ensure SPI pins don't overlap with other peripherals
3. **Per-bus baud rates** - Input and output buses can use different baud rates for mixed protocols
4. **Listen-only mode** - Use passive monitoring for vehicle ECU buses to avoid disrupting communication
5. **Test independently** - Verify each bus works before enabling hybrid mode

## Future Enhancements

### Planned Features

- **STM32 bxCAN support** - Native CAN for STM32F1/F4 platforms
- **MCP25625 support** - Integrated transceiver, simplified wiring
- **SJA1000 support** - Industrial-grade SPI CAN controller
- **DMA support** - Reduce CPU overhead for high-throughput applications
- **Timestamping** - Hardware timestamps for CAN frames
- **Error counters** - TX/RX error monitoring and reporting
- **Bus recovery** - Automatic recovery from bus-off state

### Potential Improvements

- **Runtime controller selection** - Switch controllers via serial commands (complex)
- **Per-bus statistics** - Frame count, error rate, bus load
- **Filter builder API** - Simplified filter configuration
- **CAN FD support** - Flexible data-rate CAN (Teensy 4.x, STM32)

## References

- [HAL Interface](../../src/hal/hal_can.h)
- [Platform Capabilities](../../src/hal/platform_caps.h)
- [Controller Types](../../src/lib/can_controller_types.h)
- [FlexCAN Implementation](../../src/hal/platforms/can_flexcan.h)
- [TWAI Implementation](../../src/hal/platforms/can_twai.h)
- [MCP2515 Implementation](../../src/hal/platforms/can_mcp2515.h)
- [Hybrid Dispatcher](../../src/hal/platforms/can_hybrid.h)
- [Build Configuration Guide](../guides/configuration/BUILD_CONFIGURATION_GUIDE.md)
- [CAN Transceiver Hardware Guide](../guides/hardware/CAN_TRANSCEIVER_GUIDE.md)
