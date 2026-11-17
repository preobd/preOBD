#!/usr/bin/env python3
"""#616 bench: ELM327 over BLE (FFE1, framed 0x30) through the mux's registered
handlers, alone and alongside a bulk text reply, plus the relay (0x20) handler.

    python3 tools/bench/elm327_ble_concurrent.py PORT [--secs 10] [--pin 6]

Needs `ELM327 on BLE - enabled` in OUTPUT LIST, the nRF in framed mode, and a
free BLE slot (bleak is the central; a phone or Chrome may hold the other).

  1. ELM327 alone: ATZ / ATE0 / ATL0 / 0100 answer on FFE1 and end in '>';
     then a 0100/0105/010B loop (PIDs the
     unit serves) for --secs: every reply is a well-formed
     positive response, none lost, round-trips per second reported.
  2. Concurrent (#263 ordering, now behind setMuxFrameBoundaryHandler): the
     same loop while SYSTEM DUMP JSON runs on Command TX. The dump parses and
     equals the USB dump, and ELM327 replies keep arriving DURING the dump
     (not queued behind it).
  3. Relay frame (0x20, the wrapped ActionBroadcaster handler): ACTION ADD
     RELAY <pin> in CONFIG, a BLE MANUAL_ON / OFF / AUTO write each shows up
     in the action notify and ACTION LIST. Deletes the action afterwards.

Never SAVEs. The action is RAM-only and deleted; the unit is left in RUN.
"""
import asyncio, json, re, sys, time
import serial
from bleak import BleakClient, BleakScanner

args = sys.argv[1:]
if not args or args[0].startswith("-"):
    sys.exit(__doc__)
port = args[0]
def opt(name, default):
    return type(default)(args[args.index(name) + 1]) if name in args else default
SECS = opt("--secs", 10.0)
PIN = opt("--pin", 6)

BASE = "4f424400-7072-654f-4244-00000000{:04x}"
SVC, CMD_TX, CMD_RX, ACTION = (BASE.format(x) for x in (0x0001, 0x0010, 0x0011, 0x0031))
FFE1 = "0000ffe1-0000-1000-8000-00805f9b34fb"

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
results = []


def check(ok, what, detail=""):
    results.append(ok)
    print(("PASS " if ok else "FAIL ") + what)
    if not ok and detail:
        print("     " + str(detail).replace("\n", "\n     "))


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    t, out = time.time(), b""
    while time.time() - t < wait:
        out += s.read(8192)
        if out.rstrip().endswith(b"preOBD>") and time.time() - t > 0.3:
            break
    lines = out.decode(errors="replace").splitlines()
    return "\n".join(l for l in lines if not CSV.match(l.strip()))


class Elm:
    """One request in flight at a time, reply = bytes up to the '>' prompt."""
    def __init__(self, c):
        self.c, self.buf, self.ev = c, bytearray(), asyncio.Event()

    def on(self, _, d):
        self.buf.extend(d)
        if b">" in self.buf:
            self.ev.set()

    async def ask(self, q, timeout=3.0):
        self.buf.clear(); self.ev.clear()
        await self.c.write_gatt_char(FFE1, (q + "\r").encode(), response=False)
        try:
            await asyncio.wait_for(self.ev.wait(), timeout)
        except asyncio.TimeoutError:
            return None
        return self.buf[:self.buf.index(b">")].decode(errors="replace").strip()


# PIDs this unit serves (LIST INPUTS: ECT 0x05, MAP 0x0B)
POSITIVE = {"0100": "41 00", "0105": "41 05", "010B": "41 0B"}


def norm(r):
    return re.sub(r"\s+", " ", (r or "").replace("\r", " ")).strip().upper()


async def elm_loop(elm, secs, stop=None, log=None):
    """Returns (sent, ok, bad list). Stops at secs or when stop is set."""
    sent = ok = 0
    bad = []
    t = time.time()
    pids = list(POSITIVE)
    while time.time() - t < secs and not (stop and stop.is_set()):
        q = pids[sent % len(pids)]
        r = await elm.ask(q)
        sent += 1
        if r is not None and POSITIVE[q] in norm(r):
            ok += 1
            if log is not None:
                log.append(time.time())
        else:
            bad.append((q, r))
    return sent, ok, bad


async def main():
    out = cmd("OUTPUT LIST", 2)
    check("ELM327 on BLE - enabled" in out, "OUTPUT LIST: ELM327 on BLE enabled", out)
    usb = cmd("SYSTEM DUMP JSON", 8)
    usb_json = usb[usb.index("{"):usb.rindex("}") + 1]

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in [u.lower() for u in ad.service_uuids] or (d.name or "").startswith("preOBD"),
        timeout=15)
    if dev is None:
        sys.exit("no preOBD BLE device advertising (is a phone or Chrome holding both nRF slots?)")
    async with BleakClient(dev) as c:
        print(f"connected to {dev.name} ({dev.address}), MTU {c.mtu_size}")
        elm = Elm(c)
        text = bytearray()
        frames = []
        await c.start_notify(FFE1, elm.on)
        await c.start_notify(CMD_RX, lambda _, d: text.extend(d))
        await c.start_notify(ACTION, lambda _, d: frames.append((time.time(), bytes(d))))
        await asyncio.sleep(1.0)

        # --- 1. ELM327 alone ---
        init = {}
        for q in ("ATZ", "ATE0", "ATL0", "ATS1", "ATH0", "0100"):
            init[q] = await elm.ask(q)
            print(f"  {q:5s} -> {init[q]!r}")
        check(init["ATZ"] is not None and "ELM327" in init["ATZ"].upper(), "ATZ answers with the ELM327 banner", init["ATZ"])
        check(init["ATE0"] is not None and "OK" in init["ATE0"].upper(), "ATE0 -> OK", init["ATE0"])
        check(init["0100"] is not None and "41 00" in norm(init["0100"]), "0100 -> 41 00 ...", init["0100"])

        sent, ok, bad = await elm_loop(elm, SECS)
        rate = ok / SECS
        print(f"  alone: {ok}/{sent} positive in {SECS:.0f} s = {rate:.1f} req/s")
        check(sent > 0 and ok == sent, f"alone: every reply positive ({ok}/{sent})", bad[:5])

        # --- 2. concurrent with SYSTEM DUMP JSON on Command TX ---
        text.clear()
        stop = asyncio.Event()
        times = []

        async def dump():
            t0 = time.time()
            await c.write_gatt_char(CMD_TX, b"SYSTEM DUMP JSON\r\n", response=True)
            while time.time() - t0 < 45:
                await asyncio.sleep(0.1)
                if text.rstrip().endswith(b"preOBD>") and b"}" in text:
                    break
            t1 = time.time()
            await asyncio.sleep(1.0)   # keep the ELM loop going a bit past the end
            stop.set()
            return t0, t1

        (t0, t1), (sent2, ok2, bad2) = await asyncio.gather(dump(), elm_loop(elm, 60, stop, times))
        dur = t1 - t0
        during = sum(1 for t in times if t0 <= t <= t1)
        print(f"  dump took {dur:.1f} s ({len(text)} B); ELM327 {ok2}/{sent2} positive, {during} during the dump")
        body = text.decode(errors="replace")
        try:
            bj = body[body.index("{"):body.rindex("}") + 1]
            json.loads(bj)
            strip = lambda j: {k: v for k, v in json.loads(j).items() if k != "firmware"}
            check(True, f"BLE dump parses ({len(bj)} B)")
            check(strip(bj) == strip(usb_json), "BLE dump == USB dump")
        except ValueError:
            check(False, "BLE dump parses", body[-300:])
        check(ok2 == sent2, f"concurrent: every ELM327 reply positive ({ok2}/{sent2})", bad2[:5])
        check(during >= max(2, int(dur * 2)), f"ELM327 interleaves with the dump ({during} replies in {dur:.1f} s)")

        # --- 3. relay frame through the wrapped 0x20 handler ---
        cmd("CONFIG")
        add = cmd(f"ACTION ADD RELAY {PIN}")
        m = re.search(r"Action #(\d+)", add)
        aid = int(m.group(1)) if m else -1
        check(aid >= 0 and "not driven" not in add, f"ACTION ADD RELAY {PIN} -> #{aid}", add)
        if aid >= 0:
            for code, name, want in ((1, "MANUAL_ON", 1), (2, "MANUAL_OFF", 0), (3, "AUTO", None)):
                t = time.time()
                req = bytearray(16)
                req[0], req[1], req[10] = 2, aid, code
                await c.write_gatt_char(ACTION, bytes(req), response=True)
                f = None
                for _ in range(30):
                    await asyncio.sleep(0.2)
                    got = [b for (ts, b) in frames if ts >= t and len(b) >= 16 and b[0] == 2 and b[1] == aid]
                    if got and (want is None or got[-1][3] == want):
                        f = got[-1]; break
                lst = cmd("ACTION LIST")
                line = next((l for l in lst.splitlines() if re.match(rf"\s*#?{aid}\b", l.strip())), lst)
                print(f"  {name}: frame {f.hex() if f else None} | {line.strip()}")
                check(f is not None, f"BLE {name} write -> action frame (state {f[3] if f else '?'})")
            cmd(f"ACTION DEL {aid}")
        print(cmd("ACTION LIST"))
        cmd("RUN")

    print(f"\n{sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


asyncio.run(main())
