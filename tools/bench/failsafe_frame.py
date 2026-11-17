#!/usr/bin/env python3
"""#672 bench: the BLE action frame says when the failsafe alone holds a relay ON,
and whether that is a rule with no trusted reading yet or a dead sensor.

    python3 tools/bench/failsafe_frame.py /dev/cu.usbmodemXXXX [--pins 19,6]

Uses two spare GPIOs (default 19 for the input, 6 for the relay; neither may
be listed by SYSTEM PINS, and nothing should be wired to them). The input's
reading is driven with TEST INJECT, so no sensor hardware is needed. Needs the
nRF52840 link advertising (bleak is the BLE central: no phone or Chrome
connected to it).

A FAILSAFE ON relay follows RULE ABOVE 100 HYST 10. In RUN, in order:
  1. Reading 95 (inside the hysteresis band, so the rule can't settle): ON,
     overrideFlags bit 5 (0x20) set, bit 2 clear; ACTION LIST says FAILSAFE.
  2. Reading 20: the rule settles clear, the relay goes OFF, flags 0.
  3. Reading NAN: OFF for about 2 s, then ON with bits 5 and 2 (0x24).
  4. ACTION <id> ON (operator force): ON, bit 5 clear. AUTO: bit 5 back.
  5. Reading 120: the rule trips, ON with bit 3 (0x08), bit 5 clear.
  6. CONFIG: rule-driven relays are held OFF, bit 5 clear.
Over every frame captured: bit 5 never appears with state OFF, with an
override, or with bit 3.

Never sends SAVE: it snapshots SYSTEM DUMP JSON first, re-imports it at the
end, and checks the dump came back identical.
"""
import asyncio, copy, json, os, re, sys, time
import serial
from bleak import BleakClient, BleakScanner

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
pins = [19, 6]
if "--pins" in args:
    pins = [int(x) for x in args[args.index("--pins") + 1].split(",")]
IN, RELAY = pins

BASE = "4f424400-7072-654f-4244-00000000{:04x}"
SVC, ACTION = BASE.format(0x0001), BASE.format(0x0031)
OVR_MASK, FAULTED, RULE_ACTIVE, FAILSAFE = 0x03, 0x04, 0x08, 0x20

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.05)
time.sleep(0.3)
s.reset_input_buffer()
results = []


def read_for(seconds, until=None):
    t, out = time.time(), b""
    while time.time() - t < seconds:
        out += s.read(8192)
        if until and until in out:
            break
    lines = out.decode(errors="replace").splitlines()
    return "\n".join(l for l in lines if not CSV.match(l.strip()))


def cmd(c, wait=1.0, until=None):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    return read_for(wait, until)


def dump():
    text = cmd("SYSTEM DUMP JSON", 4)
    i, j = text.find("{"), text.rfind("}")
    return json.loads(text[i:j + 1])


def comparable(d):
    d = copy.deepcopy(d)
    for k in ("firmware", "exportedAt", "timestamp", "mode"):
        d.pop(k, None)
    return d


def import_doc(doc):
    payload = json.dumps(doc, separators=(",", ":")).encode()
    s.reset_input_buffer()
    s.write(f"JSON IMPORT {len(payload)}\n".encode())
    head = read_for(3, b"bytes")
    if "Ready for" not in head:
        return head
    s.write(payload)
    return read_for(6, b"preOBD>")


def check(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name)
    if not ok and detail:
        print("     " + str(detail).replace("\n", "\n     "))


def pin_holder(p):
    for line in cmd("SYSTEM PINS", 1.5).splitlines():
        mm = re.match(r"\s*Pin\s+(\S+):\s*(.*)", line)
        if mm and mm.group(1) == str(p):
            return mm.group(2).strip()
    return "Available"


def relay_line(aid):
    text = cmd("ACTION LIST", 0.5, b"preOBD>")
    for line in text.splitlines():
        if re.search(rf"#{aid}\b", line):
            return line
    return text


def cli_failsafe(line):          # the marker after the state, not "failsafe=ON"
    return re.search(r"-> ON\s+FAILSAFE\b", line) is not None


frames = []  # (time, actionId, state, overrideFlags)


def on_notify(_, data: bytearray):
    b = bytes(data)
    if len(b) >= 16 and b[0] == 2:
        frames.append((time.time(), b[1], b[3], b[11]))


def last_frame(aid, since=0):
    for f in reversed(frames):
        if f[1] == aid and f[0] >= since:
            return f
    return None


async def wait_frame(aid, pred, secs=8.0):
    """The newest frame for `aid`, once it satisfies pred (state, flags)."""
    t = time.time()
    while time.time() - t < secs:
        f = last_frame(aid)
        if f and pred(f[2], f[3]):
            return f
        await asyncio.sleep(0.1)
    return last_frame(aid)


def show(f):
    return "no frame" if f is None else f"state {'ON' if f[2] else 'OFF'}, overrideFlags 0x{f[3]:02x}"


async def main():
    cmd("CONFIG", 1.5)
    base = dump()
    snap = time.strftime(os.path.expanduser("~/.preobd/failsafe-frame-%Y%m%d-%H%M%S.json"))
    os.makedirs(os.path.dirname(snap), exist_ok=True)
    with open(snap, "w") as f:
        json.dump(base, f, indent=1)
    print(f"baseline saved to {snap}")
    for p in pins:
        if "Available" not in pin_holder(p):
            sys.exit(f"pin {p} is not free on this unit:\n{pin_holder(p)}")

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in [u.lower() for u in ad.service_uuids] or (d.name or "").startswith("preOBD"),
        timeout=15)
    if dev is None:
        sys.exit("no preOBD BLE device advertising (is a phone or Chrome holding both nRF slots?)")

    aid = -1
    try:
        async with BleakClient(dev) as c:
            await c.start_notify(ACTION, on_notify)

            r = cmd(f"SET {IN} COOLANT_TEMP VDO_120C_STEINHART")
            r += cmd(f"SET {IN} ALARM OFF")          # no buzzer: drop the default alarm rule
            check("input set", "ERROR" not in r, r)
            r = cmd(f"TEST INJECT {IN} 95")          # in the band before the rule is armed
            check("reading injected in CONFIG", "ERROR" not in r, r)
            r = cmd(f"RULE ADD {IN} ABOVE 100 HYST 10 NONE")
            rid = [x["id"] for x in dump()["rules"] if x.get("input") == IN]
            check("control rule added", "ERROR" not in r and len(rid) == 1, r)
            r = cmd(f"ACTION ADD RELAY {RELAY} FAILSAFE ON")
            ids = [a["id"] for a in dump()["actions"] if a["target"] == RELAY]
            check("relay added (FAILSAFE ON)", "added" in r and len(ids) == 1, r)
            aid = ids[0]
            r = cmd(f"ACTION {aid} RULE {rid[0]}")
            check("relay follows the rule", "ERROR" not in r, r)

            f = await wait_frame(aid, lambda st, fl: True, 3)
            check("0. in CONFIG the relay is held OFF and the frame has no bit 5",
                  f is not None and f[2] == 0 and not (f[3] & FAILSAFE), show(f))

            cmd("RUN", 1.5)
            cmd(f"TEST INJECT {IN} 95", 0.3)
            f = await wait_frame(aid, lambda st, fl: st == 1 and fl & FAILSAFE)
            check("1. unsettled rule: ON, bit 5 set, bit 2 clear (0x20)",
                  f is not None and f[2] == 1 and f[3] == FAILSAFE, show(f))
            line = relay_line(aid)
            check("   ACTION LIST says FAILSAFE", cli_failsafe(line), line)
            await asyncio.sleep(3.0)                 # in the band it must not settle
            f = last_frame(aid)
            check("   still held after 3 s", f is not None and f[2] == 1 and f[3] == FAILSAFE, show(f))

            cmd(f"TEST INJECT {IN} 20", 0.3)
            f = await wait_frame(aid, lambda st, fl: st == 0)
            check("2. the rule settles clear: OFF, flags 0x00",
                  f is not None and f[2] == 0 and f[3] == 0, show(f))
            line = relay_line(aid)
            check("   ACTION LIST drops FAILSAFE", not cli_failsafe(line), line)

            t0 = time.time()
            cmd(f"TEST INJECT {IN} NAN", 0.2)
            f = await wait_frame(aid, lambda st, fl: st == 1 and fl & FAILSAFE, 6)
            check("3. dead sensor: ON, bits 5 and 2 (0x24)",
                  f is not None and f[2] == 1 and f[3] == (FAILSAFE | FAULTED), show(f))
            if f is not None and f[2] == 1:
                dt = f[0] - t0
                print(f"     failsafe frame {dt:.2f} s after the NAN")
                check("   after about 2 s, not at once", 1.5 <= dt <= 3.2, f"{dt:.2f} s")
            line = relay_line(aid)
            check("   ACTION LIST says FAILSAFE", cli_failsafe(line), line)

            r = cmd(f"ACTION {aid} ON")
            f = await wait_frame(aid, lambda st, fl: fl & OVR_MASK)
            check("4. operator force: ON, bit 5 clear (0x05)",
                  f is not None and f[2] == 1 and f[3] == (1 | FAULTED), show(f) + "\n" + r)
            line = relay_line(aid)
            check("   ACTION LIST shows the override, not FAILSAFE",
                  "OVERRIDE ON" in line and not cli_failsafe(line), line)
            cmd(f"ACTION {aid} AUTO")
            f = await wait_frame(aid, lambda st, fl: not (fl & OVR_MASK))
            check("   AUTO: bit 5 back (0x24)",
                  f is not None and f[2] == 1 and f[3] == (FAILSAFE | FAULTED), show(f))

            cmd(f"TEST INJECT {IN} 120", 0.3)
            f = await wait_frame(aid, lambda st, fl: fl & RULE_ACTIVE)
            check("5. the rule trips: ON, bit 3 only (0x08)",
                  f is not None and f[2] == 1 and f[3] == RULE_ACTIVE, show(f))
            line = relay_line(aid)
            check("   ACTION LIST: ON without FAILSAFE", "-> ON" in line and not cli_failsafe(line), line)

            cmd(f"TEST INJECT {IN} NAN", 0.2)
            f = await wait_frame(aid, lambda st, fl: fl & FAILSAFE, 6)
            check("   sensor dies while tripped: failsafe takes over (0x24), relay never OFF",
                  f is not None and f[2] == 1 and f[3] == (FAILSAFE | FAULTED), show(f))

            cmd("CONFIG", 1.5)
            f = await wait_frame(aid, lambda st, fl: st == 0)
            check("6. CONFIG holds it OFF: no bit 5",
                  f is not None and f[2] == 0 and not (f[3] & FAILSAFE), show(f))

            mine = [x for x in frames if x[1] == aid]
            bad = [x for x in mine if x[3] & FAILSAFE and (x[2] == 0 or x[3] & (OVR_MASK | RULE_ACTIVE))]
            check(f"every frame ({len(mine)}): bit 5 only with ON, AUTO and no rule demand", not bad,
                  "\n".join(show(x) for x in bad[:5]))
            print("     frames: " + " -> ".join(f"{'ON' if x[2] else 'OFF'}/0x{x[3]:02x}" for x in mine))
            await c.stop_notify(ACTION)
    finally:
        # ---- restore ---------------------------------------------------------
        cmd("TEST CLEAR")
        cmd("CONFIG", 1.5)
        rep = import_doc(base)
        check("baseline re-imported", "pplied" in rep, rep)
        check(f"pin {IN} released", "Available" in pin_holder(IN), pin_holder(IN))
        check(f"pin {RELAY} released", "Available" in pin_holder(RELAY), pin_holder(RELAY))
        check("the dump matches the baseline", comparable(dump()) == comparable(base))
        if base.get("mode") == "RUN":
            cmd("RUN", 1.5)

    bad = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(bad)}/{len(results)} passed")
    sys.exit(1 if bad else 0)


asyncio.run(main())
