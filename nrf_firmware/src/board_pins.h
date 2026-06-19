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

#else
#error "board_pins.h: no board defined. Add -DBOARD_ITSYBITSY (or future BOARD_MDBT50Q)."
#endif

#endif // BOARD_PINS_H
