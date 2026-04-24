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


def _profile_path_from_flags(env) -> str:
    """Extract the -include profile path from the env's build_flags.

    Reads the actual -include flag rather than assuming profile_<env>.h so that
    envs pointing at a shared profile (e.g. [env:debug] → profile_teensy41.h)
    are checked against the correct defines.
    """
    project_dir = env.subst("$PROJECT_DIR")
    try:
        flags = env.GetProjectOption("build_flags", [])
        flags_str = " ".join(flags) if isinstance(flags, list) else str(flags)
        m = re.search(r"-include\s+\"?([\w./]+profile_\w+\.h)\"?", flags_str)
        if m:
            path = m.group(1)
            return path if os.path.isabs(path) else os.path.join(project_dir, path)
    except Exception:
        pass
    # Convention fallback: profile_<env>.h
    env_name = env.subst("$PIOENV")
    return os.path.join(project_dir, "src", "profiles", f"profile_{env_name}.h")


def _parse_profile(profile_path: str, _depth: int = 0):
    """Return (defines: set, values: dict) by parsing a profile header.

    Recursively follows #include "profile_*.h" directives so that profiles
    that delegate to another profile (e.g. a future profile_debug.h that
    #includes profile_teensy41.h) are fully resolved.
    """
    if _depth > 4 or not os.path.isfile(profile_path):
        return set(), {}
    defines = set()
    values = {}
    profile_dir = os.path.dirname(profile_path)
    with open(profile_path) as f:
        for line in f:
            inc = re.match(r'^\s*#include\s+"(profile_\w+\.h)"', line)
            if inc:
                sub_path = os.path.join(profile_dir, inc.group(1))
                sub_defines, sub_values = _parse_profile(sub_path, _depth + 1)
                defines.update(sub_defines)
                values.update(sub_values)
                continue
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
    profile_path = _profile_path_from_flags(env)
    defines, values = _parse_profile(profile_path)
    if not defines:
        print(f"verify_profile_libs: no profile found for env '{env_name}' "
              f"(looked in {profile_path}), skipping check")
        return

    libs = _lib_deps_str(env)
    warnings = []

    # ENABLE_CAN on a non-native-CAN board → MCP2515 driver needed.
    # Default to "0" (assume SPI/external) when the flag is absent — this
    # produces a warning that prompts the developer to annotate the profile
    # rather than silently skipping the check.
    if "ENABLE_CAN" in defines:
        native = values.get("PROFILE_HAS_NATIVE_CAN", None)
        if native is None:
            print(f"INFO  [verify_profile_libs] {env_name}: PROFILE_HAS_NATIVE_CAN not set, "
                  "assuming non-native (SPI) CAN — add the flag to silence this")
        if (native is None or native == "0") and "mcp2515" not in libs:
            warnings.append(
                "ENABLE_CAN with PROFILE_HAS_NATIVE_CAN=0 (or unset) "
                "but autowp-mcp2515 not in lib_deps"
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
