#!/usr/bin/env python3
"""Decode a HALTECH output capture with Haltech's own conversions (#612 phase 4).

    python3 tools/bench/obdlink_send.py --monitor 5 > capture.txt
    python3 tools/bench/haltech_decode.py capture.txt

The table below is copied from the Haltech ECU Broadcast CAN Protocol v2.0
(the "Conversion from Raw" column), NOT from the firmware's layout table, so a
decode that matches LIST INPUTS checks the firmware against the spec. Haltech's
units: temperatures K (shown here in degC, x - 273.15), pressures kPa absolute
(shown as Haltech's dash would, gauge = abs - 101.3, and as absolute).

Prints the frame count and rate per ID, then each ID's last frame decoded.
A field of raw 0 is "no data" (Haltech's error convention) and shows as "0 (no data?)".
"""
import collections, re, sys

# ID: [(first byte, signed, name, conversion)] per the spec, 2-byte big-endian fields
def k(x):  return round(x / 10 - 273.15, 2)              # 0.1 K -> degC
def kpa(x): return f"{x / 10 - 101.3:.1f} kPa gauge ({x / 10:.1f} abs)"   # y = x/10 - 101.3
def kpa_abs(x): return f"{x / 10:.1f} kPa abs"            # y = x/10, "kPa (Abs)"
def kpa1(x): return f"{x - 101.3:.1f} kPa gauge ({x} abs)"   # 0x36B brake: y = x - 101.3
def d10(x): return x / 10
def raw(x): return x

SPEC = {
    0x360: [(0, False, "RPM", raw), (2, False, "Manifold Pressure", kpa_abs),
            (4, False, "Throttle Position %", d10), (6, False, "Coolant Pressure", kpa)],
    0x361: [(0, False, "Fuel Pressure", kpa), (2, False, "Oil Pressure", kpa),
            (4, False, "Engine Demand %", d10), (6, False, "Wastegate Pressure", kpa)],
    0x36B: [(0, False, "Brake Pressure Front", kpa1)],
    0x370: [(0, False, "Vehicle Speed km/h", d10)],
    0x372: [(0, False, "Battery Voltage V", d10), (4, False, "Target Boost Level kPa", d10),
            (6, False, "Barometric Pressure", kpa_abs)],
    0x373: [(b, False, f"EGT {1 + b // 2} degC", k) for b in (0, 2, 4, 6)],
    0x374: [(b, False, f"EGT {5 + b // 2} degC", k) for b in (0, 2, 4, 6)],
    0x376: [(0, False, "Ambient Air Temperature degC", k), (2, True, "Relative Humidity %", d10)],
    0x3E0: [(0, False, "Coolant Temperature degC", k), (2, False, "Air Temperature degC", k),
            (4, False, "Fuel Temperature degC", k), (6, False, "Oil Temperature degC", k)],
    0x3E1: [(0, False, "Gearbox Oil Temperature degC", k), (2, False, "Diff Oil Temperature degC", k)],
    0x6F8: [(6, False, "Fuel Level Percentage 0 %", d10)],
}


def field(data, first, signed):
    v = (data[first] << 8) | data[first + 1]
    return v - 0x10000 if signed and v >= 0x8000 else v


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    frames = []
    for tok in re.split(r"[\r\n|]+", open(sys.argv[1]).read()):
        p = tok.split()
        if len(p) >= 2 and re.fullmatch(r"[0-9A-F]{3}", p[0]) and all(re.fullmatch(r"[0-9A-F]{2}", x) for x in p[1:]):
            frames.append((int(p[0], 16), bytes(int(x, 16) for x in p[1:])))
    count = collections.Counter(f[0] for f in frames)
    print("frames:", len(frames), {hex(i): n for i, n in sorted(count.items())})
    unknown = sorted(i for i in count if i not in SPEC)
    if unknown:
        print("IDs not in the HALTECH layout:", [hex(i) for i in unknown])
    last = {}
    for fid, data in frames:
        if fid in SPEC and len(data) == 8:
            last[fid] = data
    for fid in sorted(SPEC):
        if fid not in last:
            print(f"{fid:#x}: not seen")
            continue
        data = last[fid]
        out = []
        for first, signed, name, conv in SPEC[fid]:
            v = field(data, first, signed)
            out.append(f"{name} = {'0 (no data?)' if v == 0 else conv(v)}")
        print(f"{fid:#x} [{data.hex(' ')}]: " + "; ".join(out))


if __name__ == "__main__":
    main()
