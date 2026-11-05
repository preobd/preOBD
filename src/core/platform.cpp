/*
 * platform.cpp - Platform-specific initialization functions
 */

#include <Arduino.h>
#include "platform.h"
#include "message_api.h"
#include "log_tags.h"

void setupADC() {
    // Configure analog reference based on platform
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        // Arduino Uno/Nano - 5V system
        analogReference(DEFAULT);  // 5V reference
    #elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        // Arduino Mega - 5V system
        analogReference(DEFAULT);  // 5V reference
    #elif defined(__MK20DX256__) || defined(__MK20DX128__)
        // Teensy 3.1/3.2 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);  // Average 4 samples for stability
    #elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
        // Teensy 3.5/3.6 - 3.3V system
        analogReference(DEFAULT);  // 3.3V reference
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
    #elif defined(__IMXRT1062__)
        // Teensy 4.0/4.1 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        analogReadAveraging(4);
    #elif defined(ARDUINO_SAM_DUE)
        // Arduino Due - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
    #elif defined(ARDUINO_ARCH_STM32)
        // STM32 - 3.3V system. The core has no averaging; the sample time
        // is the compile-time ADC_SAMPLINGTIME (boards/<board>/board.ini).
        analogReadResolution(ADC_RESOLUTION);
    #elif defined(ESP32)
        // ESP32 - 3.3V system
        analogReadResolution(ADC_RESOLUTION);
        // Set attenuation for full 0-3.3V range
        // ADC_11db = 0-3.3V (default but less accurate)
        // ADC_6db = 0-2.2V (more accurate, use if voltage divider allows)
        analogSetAttenuation(ADC_11db);
        msg.debug.info(TAG_ADC, "Attenuation set to 11db (0-3.3V range)");
        msg.debug.info(TAG_ADC, "ESP32 ADC is non-linear, consider calibration");
    #else
        // Unknown platform - use default
        msg.debug.warn(TAG_SYSTEM, "Unknown platform, using default ADC settings");
        #ifdef ADC_RESOLUTION
        analogReadResolution(ADC_RESOLUTION);
        #endif
    #endif
}

#if defined(ARDUINO_ARCH_STM32)
// ---------------------------------------------------------------------------
// USB CDC receive (STM32 core 4.30000 / Arduino_Core_STM32 <= 3.0.0)
//
// When the core's 192-byte receive queue is full, USBD_CDC_Receive() calls
// USBD_CDC_ClearBuffer(), which re-arms the OUT endpoint with a zero-length
// NULL buffer: the host's next packets are ACKed and thrown away instead of
// NAKed. Anything sent faster than loop() drains it is lost, e.g. a
// 512-byte JSON IMPORT sent in one write (bench, #100). Upstream fix:
// stm32duino/Arduino_Core_STM32 PR #3058 (merged 2026-09-21, for 3.0.1),
// which removes the call. Until the PlatformIO package carries it, board.ini
// links USBD_CDC_ClearBuffer to this no-op (-Wl,--wrap), which leaves the
// endpoint unarmed, so the host is NAKed and waits.
//
// The core then re-arms only from USBSerial::read(), and the maintainer notes
// on that PR that the full-queue case still needs a fix, so loop() also
// re-arms here: a no-op unless reception is parked and the queue has room.
#if defined(USBD_USE_CDC)
extern "C" bool CDC_resume_receive(void);
extern "C" uint8_t __wrap_USBD_CDC_ClearBuffer(void* /*pdev*/) { return 0; /* USBD_OK */ }
#endif

// ---------------------------------------------------------------------------
// I2C master jams after a NACK (STM32 core 4.30000 / Arduino_Core_STM32
// <= 3.0.0, #589)
//
// Wire's HAL_I2C_ErrorCallback() (twi.c) re-enables slave listen mode on a
// *master* after any I2C error: `if (obj->isMaster)` where it means
// `!obj->isMaster`. A NACKed data transfer (a sensor or display unplugged)
// leaves the peripheral in LISTEN, and every later transaction on the bus,
// to any device, then waits out Wire's 100 ms timeout: a loose BME280
// connector would starve the watchdog. Upstream fix: stm32duino/
// Arduino_Core_STM32 issue #3043, PR #3044 (merged 2026-08-11, after 3.0.0).
//
// The callback itself can't be wrapped (the HAL file that calls it defines a
// weak default), but its call into HAL_I2C_EnableListen_IT() can: board.ini
// links that to this (-Wl,--wrap). Every other caller in twi.c is a slave
// path, so refusing the call on a master handle is exactly upstream's fix.
// Wire.cpp begins a master with own address MASTER_ADDRESS (0x01), which
// i2c_init() stores shifted into Init.OwnAddress1.
#if defined(STM32_WIRE_LISTEN_WRAP) && defined(HAL_I2C_MODULE_ENABLED)
static const uint32_t WIRE_MASTER_OWN_ADDRESS = 0x01 << 1;
extern "C" HAL_StatusTypeDef __real_HAL_I2C_EnableListen_IT(I2C_HandleTypeDef* hi2c);
extern "C" HAL_StatusTypeDef __wrap_HAL_I2C_EnableListen_IT(I2C_HandleTypeDef* hi2c) {
    if (hi2c && hi2c->Init.OwnAddress1 == WIRE_MASTER_OWN_ADDRESS) return HAL_OK;
    return __real_HAL_I2C_EnableListen_IT(hi2c);
}
#endif

void platformLoopService() {
#if defined(USBD_USE_CDC)
    CDC_resume_receive();
#endif
}

int platformAnalogRead(uint8_t pin) {
    for (uint8_t i = 0; i < NUM_ANALOG_INPUTS; i++) {
        if (analogInputPin[i] == pin) return analogRead(PNUM_ANALOG_BASE + i);
    }
    return 0;   // not an analog pin
}
#endif

#if defined(ARDUINO_ARCH_STM32) && defined(STM32F4xx)
// ---------------------------------------------------------------------------
// Reboot into the ROM DFU bootloader
//
// Jumping straight to the ROM from a running sketch is unreliable: the ROM
// expects the reset clock tree (HSI), no live interrupts and a clean USB
// peripheral. So platformRebootToBootloader() only leaves a flag in a backup
// register (which survives a reset, unlike RAM, which startup zeroes) and
// resets. The next boot finds the flag in .preinit_array, which runs before
// any constructor, the core's premain() and so its clock setup included, and
// jumps from there, with the chip still in its reset state.

// RTC backup register 19, the last one: RTC alarms/calibration don't use it.
static constexpr uint32_t BOOTLOADER_MAGIC = 0xDF00B007UL;
static constexpr uint32_t SYSTEM_MEMORY = 0x1FFF0000UL;   // F405/F407 ROM bootloader

void platformRebootToBootloader() {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP19R = BOOTLOADER_MAGIC;
    delay(100);   // let the CLI reply drain over USB
    NVIC_SystemReset();
}

static void bootloaderJumpCheck() {
    __HAL_RCC_PWR_CLK_ENABLE();
    if (RTC->BKP19R != BOOTLOADER_MAGIC) return;
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP19R = 0;   // once only: the next reset boots the firmware again

    // Map system memory at address 0, as BOOT0 would, then start the ROM
    // with its own stack pointer and reset vector.
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    const uint32_t sp = *(volatile uint32_t*)SYSTEM_MEMORY;
    const uint32_t pc = *(volatile uint32_t*)(SYSTEM_MEMORY + 4);
    __set_MSP(sp);
    ((void (*)(void))pc)();
    while (1) {}
}

__attribute__((section(".preinit_array"), used))
static void (*const s_bootloaderJumpCheck)(void) = bootloaderJumpCheck;
#endif
