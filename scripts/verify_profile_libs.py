"""
verify_profile_libs.py - Pre-build profile/lib_deps consistency check

Parses the active env's profile header and verifies that required libraries
are present in lib_deps. Prints a warning (does not abort) if a mismatch is
found so developers notice drift before it becomes a link error.

Rules checked:
  - ENABLE_CAN + PROFILE_HAS_NATIVE_CAN=0 → needs autowp-mcp2515
  - ENABLE_BME280                          → needs Adafruit BME280
  - ENABLE_LCD                             → needs LiquidCrystal_I2C
  - ENABLE_ELM327 on ESP32                → needs ESP32_BleSerial
"""

# pylint: disable=undefined-variable
# pyright: reportUndefinedVariable=false
Import("env")  # type: ignore

import os
import re


def _parse_profile(env_name: str):
    """Return (set_of_defined_macros, dict_of_macro_values) for the env's profile."""
    profile_path = os.path.join(
        env.subst("$PROJECT_DIR"), "src", "profiles", f"profile_{env_name}.h"
    )
    if not os.path.isfile(profile_path):
        return set(), {}
    defines = set()
    values = {}
    with open(profile_path) as f:
        for line in f:
            m = re.match(r"^\s*#define\s+(\w+)(?:\s+(\S+))?", line)
            if m:
                name, val = m.group(1), m.group(2)
                defines.add(name)
                if val is not None:
                    values[name] = val
    return defines, values


def _lib_deps_str(env) -> str:
    return " ".join(env.GetProjectOption("lib_deps", [])).lower()


def check(env):
    env_name = env.subst("$PIOENV")
    defines, values = _parse_profile(env_name)
    if not defines:
        print(f"verify_profile_libs: no profile found for env '{env_name}', skipping check")
        return

    libs = _lib_deps_str(env)
    warnings = []

    # ENABLE_CAN on a non-native-CAN board → MCP2515 driver needed.
    # PROFILE_HAS_NATIVE_CAN=0 means SPI controller; if the flag is absent we
    # assume native to avoid false positives on future envs.
    if "ENABLE_CAN" in defines:
        native = values.get("PROFILE_HAS_NATIVE_CAN", "1")
        if native == "0" and "mcp2515" not in libs:
            warnings.append(
                "ENABLE_CAN with PROFILE_HAS_NATIVE_CAN=0 but autowp-mcp2515 not in lib_deps"
            )

    if "ENABLE_BME280" in defines and "bme280" not in libs:
        warnings.append("ENABLE_BME280 is set but Adafruit BME280 not found in lib_deps")

    if "ENABLE_LCD" in defines and "liquidcrystal" not in libs:
        warnings.append("ENABLE_LCD is set but LiquidCrystal_I2C not found in lib_deps")

    if "ENABLE_ELM327" in defines and "esp32" in env_name and "bleserial" not in libs:
        warnings.append("ENABLE_ELM327 on ESP32 but ESP32_BleSerial not found in lib_deps")

    for w in warnings:
        print(f"WARNING [verify_profile_libs] {env_name}: {w}")

    if not warnings:
        print(f"verify_profile_libs: {env_name} OK ({len(defines)} defines in profile)")


check(env)
