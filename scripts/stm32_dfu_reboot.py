"""
PlatformIO pre-upload step for STM32 DFU uploads (#100): put a running
preOBD into the ROM bootloader first, so no BOOT0 jumper is needed.

The STM32 core has no auto-reset into DFU, so before dfu-util runs this
sends `CONFIG` + `SYSTEM REBOOT BOOTLOADER` to the board's USB serial port
and waits for the ST DFU device (0483:df11) to appear. If the board is
already in DFU (jumper, or a previous attempt), or no preOBD port answers,
it does nothing and dfu-util runs as usual: a build too broken to answer
still needs the jumper.

The port is the `--upload-port`/`upload_port` if set, else the first USB
serial port with ST's vendor ID.
"""
Import("env")  # noqa: F821  (SCons global)

import subprocess
import time

ST_VID = 0x0483
DFU_PID = 0xDF11


def _dfu_present(dfu_util, env):
    # env["ENV"] carries PlatformIO's PATH, where its own dfu-util lives.
    try:
        out = subprocess.run([dfu_util, "-l"], capture_output=True, text=True,
                             timeout=10, env=env["ENV"]).stdout
    except Exception:
        return False
    return "0483:df11" in out


def _find_port(env):
    port = env.subst("$UPLOAD_PORT")
    if port:
        return port
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    for p in list_ports.comports():
        if p.vid == ST_VID and p.pid != DFU_PID:
            return p.device
    return None


def reboot_to_dfu(source, target, env):
    # The platform quotes the path for its own shell command line.
    dfu_util = env.subst("$UPLOADER").strip('"') or "dfu-util"
    if _dfu_present(dfu_util, env):
        return
    port = _find_port(env)
    if not port:
        print("stm32_dfu_reboot: no board port found; expecting DFU via the BOOT0 jumper")
        return
    try:
        import serial
        with serial.Serial(port, 115200, timeout=0.2) as s:
            s.write(b"CONFIG\r\n")
            time.sleep(0.3)
            s.write(b"SYSTEM REBOOT BOOTLOADER\r\n")
            time.sleep(0.3)
    except Exception as e:  # the port vanishing mid-write is the reboot itself
        print(f"stm32_dfu_reboot: {port}: {e}")
    deadline = time.time() + 10
    while time.time() < deadline:
        if _dfu_present(dfu_util, env):
            print(f"stm32_dfu_reboot: {port} rebooted into DFU")
            return
        time.sleep(0.5)
    print("stm32_dfu_reboot: no DFU device after 10 s; set the BOOT0 jumper and reset")


if env.subst("$UPLOAD_PROTOCOL") == "dfu":  # noqa: F821
    env.AddPreAction("upload", reboot_to_dfu)  # noqa: F821
