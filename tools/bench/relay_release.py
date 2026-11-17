#!/usr/bin/env python3
"""#648/#649 bench: relay actions claim before they configure, switch off when
released, and TEST OUTPUT honours INVERT and the operator.

    python3 tools/bench/relay_release.py /dev/cu.usbmodemXXXX [--pins 6,19,18]

Uses three spare GPIOs (default 6, 19, 18: none may be listed by SYSTEM PINS,
and nothing should be wired to them). Checks what the console says and what
the registry holds: the SYSTEM PINS "Registry: N" count and its Action Pins
section (the full list prints action pins from the table, #644, so the count
is what proves a release). No extra hardware needed; watching the pin level
needs a meter or LED on it (the cases print when to look).

Runs in CONFIG and never sends SAVE: it snapshots SYSTEM DUMP JSON first and
re-imports that snapshot at the end, then checks the dump came back identical.
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
pins = [6, 19, 18]
if "--pins" in args:
    pins = [int(x) for x in args[args.index("--pins") + 1].split(",")]
P1, P2, P3 = pins

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
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


def cmd(c, wait=1.5):
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    return read_for(wait)


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


def pins_status():
    """(registry count, {pin: listing line}) from SYSTEM PINS."""
    text = cmd("SYSTEM PINS", 1.5)
    m = re.search(r"Registry:\s*(\d+)", text)
    listed = {}
    for line in text.splitlines():
        mm = re.match(r"\s*Pin\s+(\S+):\s*(.*)", line)
        if mm:
            listed[mm.group(1)] = mm.group(2).strip()
    return (int(m.group(1)) if m else -1), listed


def pin_holder(p):
    return pins_status()[1].get(str(p), "Available")


def registry():
    return pins_status()[0]


def action_ids():
    return [a["id"] for a in dump().get("actions", [])]


cmd("CONFIG")
base = dump()
snap = time.strftime(os.path.expanduser("~/.preobd/relay-release-%Y%m%d-%H%M%S.json"))
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline saved to {snap}")
for p in pins:
    h = pin_holder(p)
    if "Available" not in h:
        sys.exit(f"pin {p} is not free on this unit:\n{h}")

reg0 = registry()

# 1. ACTION ADD claims, then configures
r1 = cmd(f"ACTION ADD RELAY {P1}")
r2 = cmd(f"ACTION ADD RELAY {P2} INVERT")
ids = action_ids()
check("ACTION ADD x2 accepted", len(ids) == 2 and "added" in r1 and "added" in r2, r1 + r2)
a1, a2 = ids[0], ids[1]
check(f"pin {P1} held by the action", "Action" in pin_holder(P1), pin_holder(P1))
check(f"pin {P2} held by the action", "Action" in pin_holder(P2), pin_holder(P2))
check("the registry gained two entries", registry() == reg0 + 2, str(registry()))
lst = cmd("ACTION LIST")
check("ACTION LIST shows no NOT DRIVEN", "NOT DRIVEN" not in lst, lst)

# 2. Manual ON, then TEST OUTPUT leaves it alone; the inverted one pulses
on = cmd(f"ACTION {a1} ON")
check("ACTION ON accepted", "manual ON" in on, on)
print(f"  look: pin {P1} HIGH (relay on); pin {P2} HIGH (inverted relay off)")
t = cmd("TEST OUTPUT", 6)
check("TEST OUTPUT skips the manual-ON relay", f"Action #{a1} skipped: already ON" in t, t)
check("TEST OUTPUT pulses the inverted relay", f"Action #{a2} -> ON" in t and f"Action #{a2} -> OFF" in t, t)
lst = cmd("ACTION LIST")
check("the manual relay is still ON after the test",
      re.search(rf"#{a1}\b.*-> ON", lst) is not None, lst)

# 3. A JSON import that moves the ON action to another pin switches the old one off
doc = dump()
for a in doc["actions"]:
    if a["id"] == a1:
        a["target"] = P3
rep = import_doc(doc)
check("import moving the ON relay is applied", "Applied" in rep or "applied" in rep, rep)
check("the moved-away relay is reported off", f"NOTE: Action #{a1} off, pin {P1}" in rep, rep)
check(f"pin {P1} released", "Available" in pin_holder(P1), pin_holder(P1))
check(f"pin {P3} now held", "Action" in pin_holder(P3), pin_holder(P3))
check("the registry still holds two action pins", registry() == reg0 + 2, str(registry()))
lst = cmd("ACTION LIST")
check("the moved action starts OFF", re.search(rf"#{a1}\b.*-> OFF", lst) is not None, lst)
print(f"  look: pin {P1} floating/LOW (released); pin {P3} LOW (relay off)")

# 4. ACTION DEL releases its pin
d = cmd(f"ACTION DEL {a2}")
check("ACTION DEL releases the pin", f"pin {P2} off, released" in d, d)
check(f"pin {P2} released", "Available" in pin_holder(P2), pin_holder(P2))
check("the registry dropped one entry", registry() == reg0 + 1, str(registry()))

# 5. #648 review B1: disabling a serial port that is off must not free a pin
#    an action holds (it used to unregister its default pins by number)
SP, SPIN = 7, 29            # Serial7 is off on this unit; pin 29 is its TX
if "Available" in pin_holder(SPIN) and re.search(r"Serial7: disabled", cmd("BUS SERIAL")):
    cmd(f"ACTION ADD RELAY {SPIN}")
    sid = [a["id"] for a in dump()["actions"] if a["target"] == SPIN][0]
    cmd(f"ACTION {sid} ON")
    reg = registry()
    cmd(f"BUS SERIAL {SP} DISABLE")
    check("BUS SERIAL DISABLE of an idle port keeps the action's pin", registry() == reg and "Action" in pin_holder(SPIN),
          pin_holder(SPIN))
    off = cmd(f"ACTION {sid} OFF")
    check("the relay can still be switched off", "manual OFF" in off, off)
    d = cmd(f"ACTION DEL {sid}")
    check("and deleted, releasing its pin", f"pin {SPIN} off, released" in d, d)
else:
    print(f"skip: Serial{SP} is in use or pin {SPIN} is held on this unit")

# 6. Restore the baseline (drops the last action) and compare
rep = import_doc(base)
check("baseline re-imported", "Applied" in rep or "applied" in rep, rep)
check(f"pin {P3} released by the restore", "Available" in pin_holder(P3), pin_holder(P3))
check("the registry is back to the baseline", registry() == reg0, str(registry()))
check("the dump matches the baseline", comparable(dump()) == comparable(base))

bad = [n for n, ok in results if not ok]
print(f"\n{len(results) - len(bad)}/{len(results)} passed")
sys.exit(1 if bad else 0)
