// dfu_trigger.cpp -- BLEDfu service + NRF_DFU pin trigger.
//
// nRF self-update path is fully handled by Adafruit's BLEDfu service (Nordic
// Secure DFU over BLE) -- no host MCU involvement. The NRF_DFU input lets the
// host MCU force the nRF into DFU mode for recovery; on a rising edge we
// invoke the Nordic-supplied bootloader entry.

#include "dfu_trigger.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "board_pins.h"

// Magic value read by the Adafruit nRF52 bootloader at reset to enter
// OTA DFU mode (see Adafruit_nRF52_Bootloader/src/dfu_init.c).
// Writing to GPREGRET + NVIC_SystemReset is the portable way to invoke
// the bootloader from application code without depending on a private
// helper symbol whose name varies across BSP versions.
constexpr uint32_t kDfuMagicOtaAppl = 0xA8;

namespace {

BLEDfu g_dfu;

bool g_prevLevel = false;
uint32_t g_riseStartMs = 0;
constexpr uint32_t kDebounceMs = 50;

} // namespace

void dfuTriggerInit() {
    g_dfu.begin();
    pinMode(PIN_NRF_DFU_IN, INPUT_PULLDOWN);
    g_prevLevel = digitalRead(PIN_NRF_DFU_IN) == HIGH;
}

void dfuTriggerPoll() {
    const bool level = digitalRead(PIN_NRF_DFU_IN) == HIGH;

    if (level && !g_prevLevel) {
        g_riseStartMs = millis();
    } else if (level && g_prevLevel) {
        if ((millis() - g_riseStartMs) >= kDebounceMs) {
            // Sustained HIGH past debounce -- request bootloader and reset.
            NRF_POWER->GPREGRET = kDfuMagicOtaAppl;
            NVIC_SystemReset();
        }
    }
    g_prevLevel = level;
}
