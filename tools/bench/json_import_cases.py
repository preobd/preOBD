#!/usr/bin/env python3
"""#567/#603 bench: JSON IMPORT is all or nothing, and only from this board type.

    python3 tools/bench/json_import_cases.py /dev/cu.usbmodemXXXX [--replies DIR]

Takes SYSTEM DUMP JSON as the baseline (saved to bench_backups/ first), then
sends a series of files built from it with the byte-counted JSON IMPORT <N>.
Each refused case must reply "...; nothing was changed" AND leave the dump
identical to the baseline. The accepted cases are followed by a re-import of
the baseline, so the unit ends where it started. Never sends SAVE.

--replies DIR writes each case's reply verbatim to DIR/<case>.txt (for the app
fixtures, which quote the firmware word for word).
"""
import copy, json, os, re, sys, time
import serial

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
port = args[0]
replies_dir = None
if "--replies" in args:
    replies_dir = args[args.index("--replies") + 1]
    os.makedirs(replies_dir, exist_ok=True)

CSV = re.compile(r"^[A-Za-z][A-Za-z0-9_]*,[-0-9.naNA]+,[^,]*$")
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()


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
    for k in ("firmware", "exportedAt", "timestamp"):
        d.pop(k, None)
    return d


def import_doc(doc):
    payload = json.dumps(doc, separators=(",", ":")).encode()
    s.reset_input_buffer()
    s.write(f"JSON IMPORT {len(payload)}\n".encode())   # \r\n: the \n would be payload byte 1
    head = read_for(3, b"bytes")
    if "Ready for" not in head:
        return head
    s.write(payload)
    return read_for(6, b"preOBD>")


cmd("CONFIG")
base = dump()
platform = base["firmware"]["platform"]
os.makedirs("bench_backups", exist_ok=True)
snap = time.strftime("bench_backups/import-cases-%Y%m%d-%H%M%S.json")
with open(snap, "w") as f:
    json.dump(base, f, indent=1)
print(f"baseline from {platform}, saved to {snap}")
inputs = base.get("inputs", [])
assert inputs, "the baseline needs at least one input"
held = set()
for line in cmd("SYSTEM PINS", 2).splitlines():
    m = re.match(r"\s*Pin\s+(\d+):", line)
    if m:
        held.add(int(m.group(1)))
# SYSTEM PINS leaves the RGB LED out of its list (it counts them in
# "Registry: N"), so the LED's wiring.ini pins are named here.
held |= {int(m) for m in re.findall(r"RGB_PIN_[RGB]=(\d+)", open(f"boards/{platform.lower()}/wiring.ini").read())} \
    if os.path.exists(f"boards/{platform.lower()}/wiring.ini") else set()
used = {i["pin"] for i in inputs}
physical = [i for i in inputs if i["pin"] <= 0x7F]
assert physical, "the baseline needs a physical input"

cases = []  # (name, doc, expect_ok)


def case(name, expect_ok, mutate):
    d = copy.deepcopy(base)
    mutate(d)
    cases.append((name, d, expect_ok))


def no_platform(d):
    d.pop("firmware", None)


def other_board(d):
    d["firmware"]["platform"] = "TEENSY41" if platform != "TEENSY41" else "TEENSY40"


def bad_record(d):
    d["inputs"][-1]["sensor"] = "NO_SUCH_SENSOR"


def on_sd_cs(d):
    rec = copy.deepcopy(physical[0])
    rec.update(pin=10, pinLabel="10", idx=len(d["inputs"]))
    d["inputs"].append(rec)


def twice(d):
    d["inputs"].append(copy.deepcopy(d["inputs"][0]))


def free_pin():
    for p in range(6, 40):
        if p not in held and p not in used:
            return p
    raise SystemExit("no free digital pin")


def on_staged_i2c(d):
    rec = copy.deepcopy(physical[0])
    rec.update(pin=18, pinLabel="A4", idx=len(d["inputs"]))
    d["inputs"].append(rec)
    d.setdefault("system", {}).setdefault("buses", {})["i2c"] = 0


# Teensy 4.x: (port, RX pin) of the ports past Serial1
SERIAL_RX = [(7, 28), (6, 25), (3, 15), (4, 16), (2, 7)]


def on_enabled_serial(d):
    port_id, rx = next((p, r) for p, r in SERIAL_RX if r not in held and r not in used)
    rec = copy.deepcopy(physical[0])
    rec.update(pin=rx, pinLabel=str(rx), idx=len(d["inputs"]))
    d["inputs"].append(rec)
    ports = d.setdefault("system", {}).setdefault("serial", {}).setdefault("ports", [])
    for p in ports:
        if p.get("port") == port_id:
            p["enabled"] = True
            break
    else:
        ports.append({"port": port_id, "enabled": True})


def rule_without_input(d):
    d["rules"] = (d.get("rules") or []) + [
        {"id": 250 - len(d.get("rules") or []), "input": free_pin(), "condition": "ABOVE", "limitLo": 1}]


def relay_on_input(d):
    d["actions"] = (d.get("actions") or []) + [
        {"id": 250, "type": "RELAY", "target": physical[0]["pin"]}]


def full_table(d):
    # A table of the most inputs this board holds, none on the baseline's
    # pins: before #603 the old inputs kept their slots and some were refused.
    cap = int(re.search(r"Active Inputs: \d+/(\d+)", cmd("VERSION")).group(1))
    recs = []
    for p in range(6, 40):
        if p in held or p in used or len(recs) >= cap:
            continue
        r = copy.deepcopy(physical[0])
        r.update(pin=p, pinLabel=str(p), idx=len(recs))
        recs.append(r)
    can = [i for i in inputs if i["pin"] >= 0xC0]
    k = 0
    while len(recs) < cap and can and k < 32:
        pin = 0xC0 + k
        k += 1
        if pin in used:
            continue
        r = copy.deepcopy(can[0])
        r.update(pin=pin, pinLabel=f"CAN:{k - 1}", idx=len(recs))
        recs.append(r)
    # Only the fields a record needs, so the file stays under the 16 KB import
    # limit; a CAN import keeps its decode (calibration)
    keep = ("idx", "pin", "pinLabel", "app", "sensor", "units", "calibration")
    recs = [{k: r[k] for k in keep if k in r and (k != "calibration" or r["pin"] >= 0xC0)} for r in recs]
    d["inputs"] = recs
    d["rules"] = []
    d["actions"] = []


def fixture_t41(d):
    # The app's fixture shape (B1044_IMPORT.fromT41): a Teensy 4.1 file with
    # input 38 labelled A14, input 45 and RELAY action 9 on pin 50
    other_board(d)
    a, b = copy.deepcopy(physical[0]), copy.deepcopy(physical[0])
    a.update(pin=38, pinLabel="A14", idx=len(d["inputs"]))
    b.update(pin=45, pinLabel="45", idx=len(d["inputs"]) + 1)
    d["inputs"] += [a, b]
    d["actions"] = (d.get("actions") or []) + [{"id": 9, "type": "RELAY", "target": 50}]


def fixture_no_platform(d):
    no_platform(d)
    b = copy.deepcopy(physical[0])
    b.update(pin=45, pinLabel="45", idx=len(d["inputs"]))
    d["inputs"].append(b)


case("own_dump", True, lambda d: None)
case("no_platform", False, no_platform)
case("other_board", False, other_board)
case("bad_record", False, bad_record)
case("pin_twice", False, twice)
case("on_sd_cs", False, on_sd_cs)
case("on_staged_i2c", False, on_staged_i2c)
case("on_enabled_serial", False, on_enabled_serial)
case("rule_without_input", False, rule_without_input)
case("relay_on_input", False, relay_on_input)
case("full_table_of_other_pins", True, full_table)
case("fixture_t41", False, fixture_t41)
case("fixture_no_platform", False, fixture_no_platform)

fails = 0
for name, doc, expect_ok in cases:
    reply = import_doc(doc)
    if replies_dir:
        with open(os.path.join(replies_dir, name + ".txt"), "w") as f:
            f.write(reply)
    refused = "nothing was changed" in reply
    after = dump()
    unchanged = comparable(after) == comparable(base)
    if expect_ok:
        ok = "Applied" in reply and not refused
        if name == "full_table_of_other_pins":
            ok = ok and len(after.get("inputs", [])) == len(doc["inputs"])
        restore = import_doc(base)
        restored = comparable(dump()) == comparable(base)
        ok = ok and restored
        note = f"applied, {len(after.get('inputs', []))} inputs; restored={restored}"
    else:
        ok = refused and unchanged
        note = f"refused={refused} unchanged={unchanged}"
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'} {name}: {note}")
    if not ok:
        print("  " + reply.replace("\n", "\n  "))

cmd("RUN")
print(f"{len(cases) - fails}/{len(cases)} passed")
sys.exit(1 if fails else 0)
