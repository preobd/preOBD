"""
sync_gatt_defs.py - PlatformIO pre-build script for nRF firmware

Copies src/comms/ble_gatt_defs.h (source of truth, owned by the main MCU
firmware) into nrf_firmware/include/ble_gatt_defs.h with a generated-file
banner. Ensures the nRF and MCU firmwares are byte-identical on UUIDs and
binary struct layouts.

Hard-fails the build if the source file is missing.
"""

# pylint: disable=undefined-variable
# pyright: reportUndefinedVariable=false
# Import and env are injected by PlatformIO/SCons at runtime

Import("env")  # type: ignore
import os
import sys

PROJECT_DIR = env["PROJECT_DIR"]            # nrf_firmware/
REPO_ROOT = os.path.abspath(os.path.join(PROJECT_DIR, ".."))
SRC = os.path.join(REPO_ROOT, "src", "comms", "ble_gatt_defs.h")
DST = os.path.join(PROJECT_DIR, "include", "ble_gatt_defs.h")

BANNER = (
    "// =============================================================================\n"
    "// AUTO-GENERATED from src/comms/ble_gatt_defs.h -- DO NOT EDIT.\n"
    "// This file is regenerated on every build by scripts/sync_gatt_defs.py.\n"
    "// Edit the source file in the main MCU firmware tree instead.\n"
    "// =============================================================================\n\n"
)

if not os.path.isfile(SRC):
    sys.stderr.write(
        "sync_gatt_defs: source file not found: {}\n".format(SRC)
    )
    env.Exit(1)

os.makedirs(os.path.dirname(DST), exist_ok=True)

with open(SRC, "r", encoding="utf-8") as f:
    source_text = f.read()

new_text = BANNER + source_text

# Skip write if unchanged to keep ccache/incremental builds happy.
existing = ""
if os.path.isfile(DST):
    with open(DST, "r", encoding="utf-8") as f:
        existing = f.read()

if existing != new_text:
    with open(DST, "w", encoding="utf-8") as f:
        f.write(new_text)
    print("sync_gatt_defs: updated {}".format(os.path.relpath(DST, REPO_ROOT)))
else:
    print("sync_gatt_defs: up-to-date")
