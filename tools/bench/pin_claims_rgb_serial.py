#!/usr/bin/env python3
"""#646/#644 bench: the RGB LED and Serial1 (nRF link) touch their pins only
while they hold them; SYSTEM PINS lists every registry entry once; BUS SERIAL
DISABLE is refused on a console or framed port; Serial1's pins can be handed
to CAN2 without a reboot.

    python3 tools/bench/pin_claims_rgb_serial.py PORT pins [--rgb all|partial|none] [--serial1 on|off]
    python3 tools/bench/pin_claims_rgb_serial.py PORT disable
    python3 tools/bench/pin_claims_rgb_serial.py PORT testout [--rgb driven|skipped]
    python3 tools/bench/pin_claims_rgb_serial.py PORT ble
    python3 tools/bench/pin_claims_rgb_serial.py PORT can2

  pins     SYSTEM PINS: listed pin lines == Registry: N + inputs + relays not
           driven (#644), the RGB LED channels present as --rgb says, and
           Serial1's four pins (RX/TX/CTS/RTS) held or not as --serial1 says.
  disable  BUS SERIAL 1 DISABLE is refused while Serial1 is framed or a
           console, and nothing changes (BUS SERIAL / SYSTEM PINS identical).
  testout  CONFIG, TEST OUTPUT, RUN: the LED is driven, or reports
           "RGB LED skipped: not driven". The buzzer chirps.
  ble      SYSTEM DUMP JSON over BLE (bleak, CMD_TX/CMD_RX) parses and matches
           the USB dump: the nRF's flow control still paces a bulk reply.
  can2     (nRF unplugged, CAN2 on pins 0/1 wired to the bus with the ECU sim)
           FRAMED DISABLE, CONSOLE TRANSPORT NONE SECONDARY, DISABLE Serial1:
           pins 0-3 freed; CAN2 ENABLE on them, CAN input moved to CAN2, and
           the RPM input reads from the sim. Then CONFIG + SYSTEM REBOOT, which
           restores everything (nothing here SAVEs).

Stage builds for the registry-full cases: MAX_PIN_REGISTRY in a worktree's
boards/teensy40/board.h (a -D can't override it), clean build:
    16 -> RGB Red/Green held, Blue refused   (pins --rgb partial)
     9 -> Serial1 refused, LED not driven    (pins --rgb partial --serial1 off)
None of these modes SAVEs.
"""
import json, re, sys, time
import serial

args = sys.argv[1:]
MODES = ("pins", "disable", "testout", "ble", "can2")
if len(args) < 2 or args[1] not in MODES:
    sys.exit(__doc__)
port, mode = args[0], args[1]
def opt(name, default):
    return args[args.index(name) + 1] if name in args else default
CSV = re.compile(r"^([A-Za-z][A-Za-z0-9_]*),([-0-9.naNA]+),([^,]*)$")
results = []
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)


def check(ok, what):
    results.append(ok)
    print(("PASS " if ok else "FAIL ") + what)


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    t, out = time.time(), b""
    while time.time() - t < wait:
        out += s.read(8192)
        if out.rstrip().endswith(b"preOBD>"):
            break
    text = out.decode(errors="replace")
    return "\n".join(l for l in text.splitlines() if not CSV.match(l.strip()))


def system_pins():
    out = cmd("SYSTEM PINS")
    m = re.search(r"Registry: (\d+) \| Inputs: (\d+)", out)
    return out, (int(m.group(1)), int(m.group(2))) if m else (None, None)


def pins_mode():
    out, (reg, inputs) = system_pins()
    print(out)
    listed = len(re.findall(r"^  Pin ", out, re.M))
    not_driven = len(re.findall(r"\(not driven\)", out))
    check(reg is not None, "SYSTEM PINS has a Registry count")
    if reg is None:
        return
    check(listed == reg + inputs + not_driven,
          f"listed {listed} == registry {reg} + inputs {inputs} + not driven {not_driven} (#644)")
    # Pin lines only: boot messages flushed into the reply name channels too
    rgb = [c for c in ("Red", "Green", "Blue") if re.search(rf"^  Pin .*RGB LED {c}$", out, re.M)]
    want = opt("--rgb", "all")
    check({"all": len(rgb) == 3, "partial": 0 < len(rgb) < 3, "none": not rgb}[want],
          f"RGB channels listed {rgb} (want {want})")
    s1 = len(re.findall(r"Reserved  - Serial1$", out, re.M))
    want = opt("--serial1", "on")
    check(s1 == (4 if want == "on" else 0), f"Serial1 holds {s1} pins (want {want})")


def disable_mode():
    cmd("CONFIG")   # BUS SERIAL changes need CONFIG mode
    before = cmd("BUS SERIAL") + system_pins()[0]
    reply = cmd("BUS SERIAL 1 DISABLE")
    print(reply)
    check("ERROR: Serial1 is" in reply and " first" in reply, "DISABLE refused, naming the step first")
    after = cmd("BUS SERIAL") + system_pins()[0]
    check(before == after, "BUS SERIAL and SYSTEM PINS unchanged")
    cmd("RUN")


def testout_mode():
    cmd("CONFIG")
    reply = cmd("TEST OUTPUT", wait=6)
    cmd("RUN")
    print(reply)
    want = opt("--rgb", "driven")
    if want == "driven":
        check("RGB LED -> RED" in reply, "TEST OUTPUT drives the LED")
    else:
        check("RGB LED skipped: not driven" in reply and "-> RED" not in reply,
              "TEST OUTPUT skips the LED: not driven")


def ble_mode():
    import asyncio
    from bleak import BleakClient, BleakScanner
    usb = cmd("SYSTEM DUMP JSON", wait=8)
    usb_json = usb[usb.index("{"):usb.rindex("}") + 1]
    SVC = "4f424400-7072-654f-4244-000000000001"
    TX = "4f424400-7072-654f-4244-000000000010"
    RX = "4f424400-7072-654f-4244-000000000011"

    async def run():
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: SVC in [u.lower() for u in ad.service_uuids] or (d.name or "").startswith("preOBD"),
            timeout=15)
        if not dev:
            return None
        buf = bytearray()
        async with BleakClient(dev) as c:
            await c.start_notify(RX, lambda _, d: buf.extend(d))
            await asyncio.sleep(1.0)
            buf.clear()
            await c.write_gatt_char(TX, b"SYSTEM DUMP JSON\r\n", response=True)
            t = time.time()
            while time.time() - t < 30:
                await asyncio.sleep(0.25)
                if buf.rstrip().endswith(b"preOBD>"):
                    break
        return buf.decode(errors="replace")

    text = asyncio.run(run())
    check(text is not None, "found the nRF over BLE")
    if text is None:
        return
    try:
        ble_json = text[text.index("{"):text.rindex("}") + 1]
        json.loads(ble_json)
        check(True, f"BLE dump parses ({len(ble_json)} B)")
    except ValueError:
        check(False, "BLE dump parses")
        return
    # uptime-like fields may differ: compare without the firmware block
    strip = lambda j: {k: v for k, v in json.loads(j).items() if k != "firmware"}
    check(strip(ble_json) == strip(usb_json), "BLE dump == USB dump")


def can2_mode():
    cmd("CONFIG")   # BUS and CONSOLE TRANSPORT changes need CONFIG mode
    print(cmd("CONSOLE STATUS"))
    framed = "[FRAMED]" in cmd("BUS SERIAL")
    print(cmd("BUS SERIAL 1 FRAMED DISABLE"))
    print(cmd("CONSOLE TRANSPORT NONE SECONDARY"))
    reply = cmd("BUS SERIAL 1 DISABLE")
    print(reply)
    if framed:
        # Still framed until a reboot, and this script never SAVEs: the
        # refusal is the check. Unframed units (or a SAVEd + rebooted one)
        # go on to the handover below.
        check("framed until a reboot" in reply, "DISABLE refused while the link is framed until a reboot")
        cmd("CONFIG")
        s.write(b"SYSTEM REBOOT\r\n")
        print("rebooted to restore the saved config (nothing was SAVEd)")
        return
    check("Serial1 disabled" in reply, "DISABLE accepted once Serial1 carries no commands")
    out, _ = system_pins()
    check(not re.search(r"Serial1$", out, re.M), "pins 0-3 no longer held by Serial1")
    reply = cmd("BUS CAN CAN2 ENABLE", wait=3)
    print(reply)
    check("ERROR" not in reply, "BUS CAN CAN2 ENABLE accepted")
    print(cmd("BUS CAN CAN2 INPUT ENABLE", wait=3))
    out = cmd("BUS CAN")
    print(out)
    check(re.search(r"CAN2: NORMAL.*up.*CAN input", out) is not None, "CAN2 up and CAN input on it")
    out, _ = system_pins()
    check(re.search(r"Pin  [01]: Reserved  - CAN2", out) is not None, "pins 0/1 held by CAN2")
    # RPM from the sim through CAN2: read the CSV stream for a while (RUN)
    cmd("RUN")
    s.reset_input_buffer()
    t, rpm = time.time(), []
    while time.time() - t < 6:
        for l in s.read(8192).decode(errors="replace").splitlines():
            m = CSV.match(l.strip())
            if m and m.group(1).upper().startswith("RPM"):
                rpm.append(m.group(2))
    print("RPM samples:", rpm[-5:])
    check(any(v.lower() not in ("nan", "0", "0.0", "0.00") for v in rpm), "RPM reads from the sim over CAN2")
    cmd("CONFIG")
    s.write(b"SYSTEM REBOOT\r\n")
    print("rebooted to restore the saved config (nothing was SAVEd)")


{"pins": pins_mode, "disable": disable_mode, "testout": testout_mode,
 "ble": ble_mode, "can2": can2_mode}[mode]()
print(f"\n{sum(results)}/{len(results)} passed")
sys.exit(0 if all(results) else 1)
