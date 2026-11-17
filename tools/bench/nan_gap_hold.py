#!/usr/bin/env python3
"""#650 bench: a relay rides out a short sensor dropout and goes to its
failsafe after the 2 s fault debounce.

    python3 tools/bench/nan_gap_hold.py /dev/cu.usbmodemXXXX [--pins 19,6]

Uses two spare GPIOs (default 19 for the input, 6 for the relay; neither may
be listed by SYSTEM PINS, and nothing should be wired to them). The input's
reading is driven with TEST INJECT (a value, or NAN for a dead sensor), so no
sensor hardware is needed. Rule-driven relays are held off in CONFIG, so the
checks run in RUN.

  1. Inject 120 against RULE ABOVE 100: the relay (FAILSAFE OFF) turns ON.
  2. Inject NAN for about 1 s, then 120 again: the relay never drops.
  3. Inject NAN and keep it: the relay stays ON for about 2 s, then the
     failsafe turns it OFF. (ACTION LIST names a faulted source only next to
     an override, #329, so this checks the relay state.)

Never sends SAVE: it snapshots SYSTEM DUMP JSON first, re-imports it at the
end, and checks the dump came back identical.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
pins = [19, 6]
if "--pins" in args:
    pins = [int(x) for x in args[args.index("--pins") + 1].split(",")]
IN, RELAY = pins

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
        print("     " + detail.replace("\n", "\n     "))


def pin_holder(p):
    for line in cmd("SYSTEM PINS", 1.5).splitlines():
        mm = re.match(r"\s*Pin\s+(\S+):\s*(.*)", line)
        if mm and mm.group(1) == str(p):
            return mm.group(2).strip()
    return "Available"


def relay_line(aid):
    text = cmd("ACTION LIST", 0.35, b"preOBD>")
    for line in text.splitlines():
        if re.search(rf"#{aid}\b", line):
            return line
    return text


def relay_on(line):
    return re.search(r"-> ON\b", line) is not None


cmd("CONFIG", 1.5)
base = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/nan-gap-hold-%Y%m%d-%H%M%S.json"))
os.makedirs(os.path.dirname(snap), exist_ok=True)
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline saved to {snap}")
for p in pins:
    if "Available" not in pin_holder(p):
        sys.exit(f"pin {p} is not free on this unit:\n{pin_holder(p)}")

r = cmd(f"SET {IN} COOLANT_TEMP VDO_120C_STEINHART")
r += cmd(f"SET {IN} ALARM OFF")              # no buzzer: drop the default alarm rule
check("input set", "ERROR" not in r, r)
r = cmd(f"RULE ADD {IN} ABOVE 100 NONE")
rid = [x["id"] for x in dump()["rules"] if x.get("input") == IN]
check("control rule added", "ERROR" not in r and len(rid) == 1, r)
r = cmd(f"ACTION ADD RELAY {RELAY}")
aid = [a["id"] for a in dump()["actions"] if a["target"] == RELAY]
check("relay added (FAILSAFE OFF)", "added" in r and len(aid) == 1, r)
aid = aid[0]
r = cmd(f"ACTION {aid} RULE {rid[0]}")
check("relay follows the rule", "ERROR" not in r, r)

cmd("RUN", 1.5)
cmd(f"TEST INJECT {IN} 120")
time.sleep(1.5)                              # persist (500 ms) + margin
line = relay_line(aid)
check("1. hot reading turns the relay ON", relay_on(line), line)

# 2. a ~1 s dropout
cmd(f"TEST INJECT {IN} NAN", 0.2)
t0, seen = time.time(), []
while time.time() - t0 < 1.0:
    seen.append(relay_line(aid))
cmd(f"TEST INJECT {IN} 120", 0.2)
for _ in range(4):
    seen.append(relay_line(aid))
drops = [l for l in seen if not relay_on(l)]
check(f"2. a 1 s dropout never drops the relay ({len(seen)} samples)", not drops,
      "\n".join(drops[:3]))
check("   and it isn't reported as a fault", not any("source faulted" in l for l in seen))

# 3. a dead sensor
time.sleep(1.0)
cmd(f"TEST INJECT {IN} NAN", 0.2)
t0, off_at, last = time.time(), None, ""
while time.time() - t0 < 4.0:
    last = relay_line(aid)
    if not relay_on(last):
        off_at = time.time() - t0
        break
check("3. a dead sensor reaches the failsafe (OFF)", off_at is not None, last)
if off_at is not None:
    print(f"     failsafe after {off_at:.2f} s")
    check("   after about 2 s, not at once", 1.6 <= off_at <= 2.8, f"{off_at:.2f} s")

# ---- restore -----------------------------------------------------------------
cmd("TEST CLEAR")
cmd("CONFIG", 1.5)
rep = import_doc(base)
check("baseline re-imported", "pplied" in rep, rep)
check(f"pin {IN} released", "Available" in pin_holder(IN), pin_holder(IN))
check(f"pin {RELAY} released", "Available" in pin_holder(RELAY), pin_holder(RELAY))
check("the dump matches the baseline", comparable(dump()) == comparable(base))
mode = "RUN" if base.get("mode") == "RUN" else None
if mode:
    cmd("RUN", 1.5)

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed")
sys.exit(1 if bad else 0)
