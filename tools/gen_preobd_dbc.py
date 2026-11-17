#!/usr/bin/env python3
"""Regenerate docs/can/preOBD.dbc from the firmware's PREOBD table (#612).

The DBC is rendered by test/test_can_layout (native, no firmware flash): the
test fails when the committed file differs from the table, and writes the file
instead when PREOBD_WRITE_DBC=1. This script is that write run; then it checks
the result with the plain test run.
"""
import os
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cmd = ["pio", "test", "-e", "native", "-f", "test_can_layout"]

env = dict(os.environ, PREOBD_WRITE_DBC="1")
print("Writing docs/can/preOBD.dbc ...")
if subprocess.call(cmd, cwd=root, env=env) != 0:
    sys.exit("DBC render failed")
print("Verifying ...")
sys.exit(subprocess.call(cmd, cwd=root))
