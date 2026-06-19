// board_pins.h -- all pin assignments for the nRF firmware.
//
// Changing hardware targets means updating this file only. Phase 1 supports
// BOARD_ITSYBITSY (Adafruit ItsyBitsy nRF52840 Express); the MDBT50Q
// production target is added in Phase 3.

#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#if defined(BOARD_ITSYBITSY)

// UART to host MCU. Uses Serial1 (BSP-default TX/RX pads on the ItsyBitsy).
// No CTS/RTS in Phase 1: the ItsyBitsy variant doesn't expose hardware flow
// control through the standard Serial1 API. HW flow control is a Phase 2
// task once we drop to nrfx_uarte directly.

// GPIO to host MCU.
//   PIN_NRF_IRQ_OUT: driven HIGH while BLE connected, LOW while disconnected.
//                    Host MCU configures this as an input with EXTI on both
//                    edges (or polls).
//   PIN_NRF_DFU_IN:  host MCU drives HIGH to request nRF DFU mode. Internal
//                    pull-down so an unconnected pin is a safe LOW.
//   PIN_STATUS_LED:  local BLE state indicator during bring-up.
#define PIN_NRF_IRQ_OUT   9
#define PIN_NRF_DFU_IN    5
#define PIN_STATUS_LED    LED_BUILTIN

// Phase 3 -- software-paced flow control on the host-MCU UART link.
//   PIN_NRF_RTS_OUT: nRF drives, host reads as its CTS-in.
//                    HIGH = "pause your TX, my buffer is filling up".
//   PIN_NRF_CTS_IN:  host drives, nRF reads. HIGH = "pause your TX".
//                    Configured as INPUT_PULLDOWN so an unconnected line
//                    reads LOW (always-OK to send).
// Both are opt-in via build flags in platformio.ini -- uncomment and fill
// in the GPIOs you wired. Leave both undefined for a no-op build identical
// to Phase 2.

#else
#error "board_pins.h: no board defined. Add -DBOARD_ITSYBITSY (or future BOARD_MDBT50Q)."
#endif

#endif // BOARD_PINS_H
