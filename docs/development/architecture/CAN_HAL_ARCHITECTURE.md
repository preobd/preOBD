# CAN Hardware Abstraction Layer (HAL) Architecture

This document describes the CAN controller abstraction layer that enables preOBD to support multiple CAN controller types across different platforms.

## Table of Contents

1. [Overview](#overview)
2. [Design Goals](#design-goals)
3. [Architecture](#architecture)
4. [Controller Types](#controller-types)
5. [Platform Capabilities](#platform-capabilities)
6. [Implementation Details](#implementation-details)
7. [Adding New Controllers](#adding-new-controllers)

## Overview

### What is the CAN HAL?

The CAN HAL is a compile-time abstraction layer that provides a unified API for CAN bus operations regardless of the underlying hardware controller. It supports:

- **Native controllers** (FlexCAN, TWAI, bxCAN) - Integrated CAN peripherals in the microcontroller
- **External controllers** (MCP2515, MCP25625, SJA1000) - SPI-based CAN controllers

Each build drives exactly one controller type: every CAN bus on a board uses
the same driver.

### Why Was This Needed?

**Before (Pre-HAL):**
- Platform-specific CAN code scattered throughout the codebase
- Manual conditional compilation (`#ifdef USE_FLEXCAN_NATIVE`)
- Adding new platforms required extensive changes

**After (With HAL):**
- Single unified API (`hal::can::begin()`, `hal::can::write()`, etc.)
- Platform detection happens automatically at compile-time
- Adding new controllers requires only implementing the HAL interface

## Design Goals

1. **Zero Runtime Overhead** - All platform selection happens at compile-time via templates/constexpr
2. **One Driver per Build** - `hal_can.h` includes exactly one controller implementation
3. **Extensibility** - Easy to add new controller types without modifying existing code
4. **Explicit Configuration** - No hidden auto-detection, clear build flags

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
│ Implementation │ │ Impl       │ │ Impl       │ │ Impl        │
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
│   ├── can_flexcan.h           # Teensy FlexCAN: declarations only
│   ├── can_flexcan.cpp         # Teensy FlexCAN: driver objects + bodies (the only FlexCAN_T4 includer)
│   ├── can_twai.h              # ESP32 TWAI implementation
│   ├── can_mcp2515.h           # MCP2515 SPI implementation
│   ├── can_mcp2515.cpp         # MCP2515 driver objects + "started" flags (one definition)
│   └── can_bxcan.h / .cpp      # STM32F4 bxCAN implementation
└── bus_defaults.h               # Default bus count per platform
```

## Controller Types

### Supported Controllers

`src/hal/hal_can.h` includes exactly one implementation, picked by platform:
FlexCAN on Teensy (`USE_FLEXCAN_NATIVE`), TWAI on ESP32, bxCAN on STM32F4,
and the MCP2515 SPI driver everywhere else.

### Controller Capabilities

| Controller | Buses | Speed | Hardware | Transceiver | Notes |
|------------|-------|-------|----------|-------------|-------|
| **FlexCAN** | 2-3 | 1 Mbps | Teensy 3.6/4.0/4.1 | External | High performance, multiple buses |
| **TWAI** | 1 | 1 Mbps | ESP32/ESP32-S3 | External | ESP32 integrated peripheral |
| **MCP2515** | 1-2 | 1 Mbps | SPI (any platform) | External | Requires SPI pins |
| **bxCAN** | 1 | 1 Mbps | STM32F4 (`feather_f405`) | External | CAN1 |

## Platform Capabilities

### Compile-Time Detection

Platform capabilities are detected via `src/hal/platform_caps.h`:

```cpp
// Automatically defined based on platform and build flags
#define PLATFORM_CAN_CONTROLLER "FlexCAN"  // or "TWAI", "SPI", "bxCAN"
#define PLATFORM_HAS_NATIVE_CAN 1          // 1 = integrated, 0 = external
#define PLATFORM_NEEDS_SPI_CAN 0           // 1 = requires SPI pin config
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

Each controller is implemented under `src/hal/platforms/`.

**Driver objects are defined once, in a `.cpp`, never in a header.** A
driver object (or any state, like a "started" flag) defined `static` in a
header gives every file that includes `hal_can.h` its own copy: CAN input
and output then drive the same controller through different objects. On
Teensy 4 that cost 10.5 KB of RAM1 per copy (#555).

**FlexCAN_T4 may be included by one file only, `can_flexcan.cpp`.** The
library keeps its ISR target (`_CAN1`) and ISR as `static`s in its own
header, while the linker merges its template code across files. With the
library in two files, `begin()` can install an ISR that reads another
file's `_CAN1`, still null: the interrupt is never serviced and re-fires
until the watchdog resets the board. `can_flexcan.h` therefore only
declares the HAL functions, and `tools/check_layering.py` fails a commit
that includes `FlexCAN_T4.h` anywhere else.

**Example: FlexCAN (abridged from `can_flexcan.cpp`)**
```cpp
#include "../hal_can.h"     // declares hal::can::begin(), write(), ...
#include <FlexCAN_T4.h>     // here and nowhere else

namespace hal { namespace can {
namespace detail {
    // RX is polled from the mailboxes, so the library's RX ring is never
    // filled: keep it at its smallest
    static FlexCAN_T4<CAN1, RX_SIZE_2, TX_SIZE_16> canBus0;
}

FLASHMEM bool begin(uint32_t baudrate, uint8_t bus, bool listenOnly) {
    switch (bus) {
        case 0: detail::initBus(detail::canBus0, baudrate, listenOnly); return true;
        default: return false;   // CAN2/CAN3: #554
    }
}
}} // namespace hal::can
```

**Note on FlexCAN listen-only mode:** The FlexCAN_T4 library's `setBaudRate()` accepts an optional second parameter for operating mode (`TX`, `LISTEN_ONLY`, etc.). This allows setting baudrate and mode in a single call, which is used to support the `listenOnly` parameter in `hal::can::begin()`.

### Memory Footprint

| Component | Flash (bytes) | RAM (bytes) |
|-----------|---------------|-------------|
| FlexCAN implementation | ~2KB | 3,664 per bus on Teensy 4.1 (RX_SIZE_2, TX_SIZE_16; measured, #555) |
| TWAI implementation | ~1.5KB | 256 per bus |
| MCP2515 implementation | ~2KB | 128 per bus |

## Adding New Controllers

### Step 1: Create Controller Implementation

Create `src/hal/platforms/can_sja1000.h`:

```cpp
#ifndef HAL_CAN_SJA1000_H
#define HAL_CAN_SJA1000_H

#include <SJA1000.h>  // Hypothetical library

namespace hal { namespace can {

bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false);
bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0);
bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0);
void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0);
bool abortUnackedTx(uint8_t bus = 0);   // drop pending TX when nothing ACKs (#506)

}} // namespace hal::can

#endif
```

### Step 2: Add to Platform Capabilities

Update `src/hal/platform_caps.h` if auto-detection is needed:

```cpp
#elif defined(USE_SJA1000)
    #define PLATFORM_CAN_CONTROLLER "SJA1000"
    #define PLATFORM_HAS_NATIVE_CAN 0
    #define PLATFORM_NEEDS_SPI_CAN 1
```

### Step 3: Select It in the HAL

Add a branch to the platform chain in `src/hal/hal_can.h`:

```cpp
#elif defined(USE_SJA1000)
    #include "platforms/can_sja1000.h"
```

### Step 4: Add Library Dependency

Add the library to the board's `boards/<board>/board.ini` `lib_deps`, and
declare the board facts in `boards/<board>/board.h`:

```c
#define SUPPORTS_CAN 1
#define USE_SJA1000  1
```

### Step 5: Test

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

### For Multi-Bus Setups

1. **Pin conflicts** - Ensure SPI pins don't overlap with other peripherals
2. **Per-bus baud rates** - Input and output buses can use different baud rates for mixed protocols
3. **Listen-only mode** - Use passive monitoring for vehicle ECU buses to avoid disrupting communication

## Future Enhancements

### Planned Features

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

- [HAL Interface](../../../src/hal/hal_can.h)
- [Platform Capabilities](../../../src/hal/platform_caps.h)
- [FlexCAN Implementation](../../../src/hal/platforms/can_flexcan.h)
- [TWAI Implementation](../../../src/hal/platforms/can_twai.h)
- [MCP2515 Implementation](../../../src/hal/platforms/can_mcp2515.h)
- [Build Configuration Guide](../../guides/configuration/BUILD_CONFIGURATION_GUIDE.md)
- [CAN Transceiver Hardware Guide](../../guides/hardware/CAN_TRANSCEIVER_GUIDE.md)
