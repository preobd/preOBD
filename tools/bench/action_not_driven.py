#!/usr/bin/env python3
"""#653 bench: the BLE action frame reports a relay that doesn't hold its pin.

    python3 tools/bench/action_not_driven.py /dev/cu.usbmodemXXXX [--pin 6] [--spare 19] [--free-serial 5]

--pin and --spare must be free GPIOs (not in SYSTEM PINS, not an input) with
nothing wired to them.

Needs a STAGE build whose pin table is exactly full, so the next relay's claim
is refused: in a worktree set MAX_PIN_REGISTRY in boards/<board>/board.h to
the "Registry: N" that SYSTEM PINS prints on this unit (17 on the bench
teensy40), then build and flash. Also needs the nRF52840 link advertising
(bleak is the BLE central: no phone or Chrome connected to it).

Cases, over USB and BLE at once:
  1. ACTION ADD RELAY <pin> is accepted but not driven: ACTION LIST says
     NOT DRIVEN, ACTION <id> ON is refused, and the BLE frame has bit 4 (0x10)
     of overrideFlags set with state OFF.
  2. A BLE ON write to it is confirmed with the frame unchanged (still OFF,
     still not driven).
  3. BUS SERIAL <n> DISABLE frees two entries; the next ACTION ADD syncs, the
     first relay claims its pin and a frame arrives with bit 4 clear.
Runs in CONFIG and never sends SAVE: cleanup deletes the actions and reboots,
which brings the serial port back from the stored config.
"""
import asyncio, re, sys, time
import serial
from bleak import BleakClient, BleakScanner

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
PIN = int(args[args.index("--pin") + 1]) if "--pin" in args else 6
FREE = int(args[args.index("--free-serial") + 1]) if "--free-serial" in args else 5
SPARE = int(args[args.index("--spare") + 1]) if "--spare" in args else 19  # the ACTION ADD that syncs

BASE = "4f424400-7072-654f-4244-00000000{:04x}"
SVC, ACTION = BASE.format(0x0001), BASE.format(0x0031)
NOT_DRIVEN = 0x10

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()
results = []


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    t, out = time.time(), b""
    while time.time() - t < wait:
        out += s.read(8192)
    lines = out.decode(errors="replace").splitlines()
    return "\n".join(l for l in lines if not CSV.match(l.strip()))


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name)
    if not ok and detail:
        print("     " + detail.replace("\n", "\n     "))


frames = []  # (time, actionId, state, overrideFlags)


def on_notify(_, data: bytearray):
    b = bytes(data)
    if len(b) >= 16 and b[0] == 2:
        frames.append((time.time(), b[1], b[3], b[11]))


def last_frame(aid, since):
    for f in reversed(frames):
        if f[1] == aid and f[0] >= since:
            return f
    return None


async def wait_frame(aid, since, pred, secs=8.0):
    t = time.time()
    while time.time() - t < secs:
        f = last_frame(aid, since)
        if f and pred(f):
            return f
        await asyncio.sleep(0.2)
    return last_frame(aid, since)


async def main():
    pins = cmd("SYSTEM PINS", 2)
    m = re.search(r"Registry: (\d+)", pins)
    print("SYSTEM PINS: " + (m.group(0) if m else "?"))
    cmd("CONFIG")

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in [u.lower() for u in ad.service_uuids] or (d.name or "").startswith("preOBD"),
        timeout=15)
    if dev is None:
        sys.exit("no preOBD BLE device advertising (is a phone or Chrome holding both nRF slots?)")
    async with BleakClient(dev) as c:
        await c.start_notify(ACTION, on_notify)

        t0 = time.time()
        add = cmd(f"ACTION ADD RELAY {PIN}")
        m = re.search(r"Action #(\d+)", add)
        aid = int(m.group(1)) if m else -1
        check("ACTION ADD reports the relay not driven", "not driven" in add, add)
        lst = cmd("ACTION LIST")
        check("ACTION LIST marks it NOT DRIVEN", "NOT DRIVEN" in lst, lst)
        on = cmd(f"ACTION {aid} ON")
        check("ACTION <id> ON refused: ERROR: not driven", "ERROR: not driven" in on, on)

        f = await wait_frame(aid, t0, lambda f: f[3] & NOT_DRIVEN)
        check("BLE frame: overrideFlags bit 4 set, state OFF",
              f is not None and (f[3] & NOT_DRIVEN) and f[2] == 0, repr(f))

        t1 = time.time()
        req = bytearray(16)
        req[0], req[1], req[10] = 2, aid, 1  # CMD_MANUAL_ON
        await c.write_gatt_char(ACTION, bytes(req), response=True)
        f = await wait_frame(aid, t1, lambda f: True, 4)
        check("BLE ON write confirmed, frame unchanged (OFF, not driven)",
              f is not None and (f[3] & NOT_DRIVEN) and f[2] == 0, repr(f))

        dis = cmd(f"BUS SERIAL {FREE} DISABLE", 2)
        print(f"BUS SERIAL {FREE} DISABLE: " + " / ".join(l.strip() for l in dis.splitlines()[1:3]))
        t2 = time.time()
        add2 = cmd(f"ACTION ADD RELAY {SPARE}")
        m2 = re.search(r"Action #(\d+)", add2)
        aid2 = int(m2.group(1)) if m2 else -1
        f = await wait_frame(aid, t2, lambda f: not (f[3] & NOT_DRIVEN))
        check("pin freed: the next sync claims it and the frame clears bit 4",
              f is not None and not (f[3] & NOT_DRIVEN), repr(f))
        lst = cmd("ACTION LIST")
        check("ACTION LIST no longer says NOT DRIVEN", "NOT DRIVEN" not in lst, lst)

        await c.stop_notify(ACTION)

    for i in (aid, aid2):
        if i >= 0:
            cmd(f"ACTION DEL {i}")
    print("rebooting to restore the stored config (nothing was saved)")
    s.write(b"SYSTEM REBOOT\r\n")
    time.sleep(1)

    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    sys.exit(1 if failed else 0)


asyncio.run(main())
