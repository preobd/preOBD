#!/usr/bin/env python3
"""Decode an ELM327 monitor capture (ATMA, headers on) against a DBC.

    python3 tools/bench/obdlink_send.py --monitor 9 > capture.txt
    python3 tools/bench/can_decode.py docs/can/preOBD.dbc capture.txt

Prints the frame count per ID, then each ID's last frame decoded (signals
reading N/A left out). Needs cantools (pip install cantools).
"""
import collections, re, sys
import cantools

if len(sys.argv) != 3:
    sys.exit(__doc__)
db = cantools.database.load_file(sys.argv[1])
frames = []
for tok in re.split(r"[\r\n|]+", open(sys.argv[2]).read()):
    p = tok.split()
    if len(p) >= 2 and re.fullmatch(r"[0-9A-F]{3}", p[0]) and all(re.fullmatch(r"[0-9A-F]{2}", x) for x in p[1:]):
        frames.append((int(p[0], 16), bytes(int(x, 16) for x in p[1:])))
count = collections.Counter(f[0] for f in frames)
print("frames:", len(frames), {hex(k): v for k, v in sorted(count.items())})
last = {}
for fid, data in frames:
    try:
        last[fid] = db.decode_message(fid, data)
    except Exception:   # an unknown ID, or the capture's cut-off last frame
        pass
for fid in sorted(last):
    print(hex(fid), {k: v for k, v in last[fid].items() if str(v) != "N/A"} or "all N/A")
