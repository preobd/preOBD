"""
PlatformIO pre-build check: say when an STM32 core workaround has become
unnecessary (#100). Two are checked; see the end of this docstring for SPI.

preOBD works around a USB CDC receive bug in the STM32 Arduino core: when its
receive queue is full, USBD_CDC_Receive() calls USBD_CDC_ClearBuffer(), which
arms the OUT endpoint with a zero-length buffer and so drops the host's
packets. board.ini links that call to a no-op (-Wl,--wrap=USBD_CDC_ClearBuffer)
and src/core/platform.cpp holds the replacement. Upstream fixed it in
stm32duino/Arduino_Core_STM32 PR #3058 (merged 2026-09-21, 3.0.1/3.1.0) by
removing the call.

So: look for the call in the core this build actually uses. While it is
there, stay silent. Once it is gone, print what to remove, every build,
until someone does.

SPI: SPIClass::begin() in core 3.0.0 calls configSpi(defaultSettings), which
skips spi_init() because the settings match the cached default, so SPI is
never initialized. src/drivers/bus_manager.cpp (initSPIBus) configures 1 MHz
first to force it. Upstream fixed it in stm32duino PR #3049 (merged
2026-08-20) by calling spi_init() from begin() directly.
"""
Import("env")  # noqa: F821  (SCons global)

import os

core = env.PioPlatform().get_package_dir("framework-arduinoststm32")  # noqa: F821
src = os.path.join(core or "", "libraries", "USBDevice", "src", "cdc", "usbd_cdc_if.c")

if os.path.isfile(src):
    with open(src, encoding="utf-8", errors="replace") as f:
        text = f.read()
    if "USBD_CDC_ClearBuffer(" not in text:
        print(
            "\n*** stm32_core_workarounds: this STM32 core no longer calls USBD_CDC_ClearBuffer\n"
            "*** (upstream PR #3058 landed). Remove the workaround: the -Wl,--wrap line in\n"
            "*** boards/feather_f405/board.ini and __wrap_USBD_CDC_ClearBuffer in\n"
            "*** src/core/platform.cpp, then re-run the JSON IMPORT size sweep (TODO.md).\n"
            "*** Keep the loop()-side CDC_resume_receive() until upstream's NAK follow-up\n"
            "*** (resume when the queue drains) has landed too.\n"
        )

spi = os.path.join(core or "", "libraries", "SPI", "src", "SPI.cpp")
if os.path.isfile(spi):
    with open(spi, encoding="utf-8", errors="replace") as f:
        text = f.read()
    if "configSpi(defaultSettings)" not in text:
        print(
            "\n*** stm32_core_workarounds: this STM32 core's SPI.begin() initializes the\n"
            "*** peripheral (upstream PR #3049 landed). Remove the 1 MHz beginTransaction()\n"
            "*** before SPI.begin() in initSPIBus(), src/drivers/bus_manager.cpp.\n"
        )
