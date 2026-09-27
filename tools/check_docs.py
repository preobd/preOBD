#!/usr/bin/env python3
"""
check_docs.py - keep the Markdown docs honest about the serial CLI.

Scans README.md and docs/**/*.md for serial commands, sensor names and
application names, and checks them against the firmware source:

  - top-level commands          <- COMMANDS[] in src/cli/command_table.cpp
  - second tokens (SYSTEM DUMP, BUS I2C, RULE ADD, ...)
                                <- PSTR("...") tokens in each command's handler
  - SET <pin> <field>           <- PSTR_SET_* in src/cli/cmd_set.cpp
  - SET <pin> CAL <profile>     <- PSTR_CAL_* in src/cli/cmd_set.cpp, and the
                                   profile must match the sensor's calibration type
  - sensor / application names  <- src/sensors/sensor_library/sensors/*.h and
                                   src/sensors/application_presets.cpp, including
                                   the sensor-measures-what-the-application-expects check
  - retired commands            <- RELAY (deprecated), SELFTEST (now SYSTEM DIAG INTEGRITY)

Everything is derived from the source each run, so the checker itself can't
drift from the firmware. It's a text scan, not a parser: it only looks at
code blocks and inline `code` that start with a known command word.

Suppress a finding by putting  <!-- docs-check: ignore -->  on the line
before a code block (skips the block) or at the end of a prose line.

Usage:
    python3 tools/check_docs.py              # check, exit 1 on problems
    python3 tools/check_docs.py FILE.md ...  # check specific files
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IGNORE = "docs-check: ignore"


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def pstr_defs(text):
    """static const char NAME[] PROGMEM = "VALUE";  ->  {NAME: VALUE}"""
    return dict(re.findall(r'static const char (\w+)\[\]\s+PROGMEM\s*=\s*"([^"]*)"', text))


def function_body(text, signature):
    """Text of the function whose definition starts with `signature`."""
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i]
    return text[brace:]


def upper_tokens(text):
    return {t for t in re.findall(r'PSTR\("([A-Z0-9_]+)"\)', text)}


# ----------------------------------------------------------------------------
# Firmware catalog
# ----------------------------------------------------------------------------

def load_catalog():
    cat = {}
    table = read("src/cli/command_table.cpp")
    cat["commands"] = set(re.findall(r'\{"([A-Z?]+)",\s*cmd_\w+', table))

    # Second tokens: every uppercase PSTR token in the handler (a superset of
    # the real verbs, which is fine — it catches verbs that don't exist).
    subs = {}
    for name, src in [
        ("LIST", ("src/cli/command_table.cpp", "int cmd_list(")),
        ("OUTPUT", ("src/cli/command_table.cpp", "int cmd_output(")),
        ("DISPLAY", ("src/cli/command_table.cpp", "int cmd_display(")),
        ("TRANSPORT", ("src/cli/command_table.cpp", "int cmd_transport(")),
        ("TEST", ("src/cli/command_table.cpp", "int cmd_test(")),
        ("LOG", ("src/cli/command_table.cpp", "int cmd_log(")),
        ("JSON", ("src/cli/command_table.cpp", "int cmd_json(")),
        ("SCAN", ("src/cli/command_table.cpp", "int cmd_scan(")),
    ]:
        subs[name] = upper_tokens(function_body(read(src[0]), src[1]))
    for name, path in [("RULE", "src/cli/cmd_rule.cpp"),
                       ("ACTION", "src/cli/cmd_action.cpp"),
                       ("ALARM", "src/cli/cmd_alarm.cpp")]:
        subs[name] = upper_tokens(read(path)) | {"#"}
    subs["SYSTEM"] = {v for k, v in pstr_defs(read("src/cli/cmd_system.cpp")).items()
                      if k.startswith("PSTR_SYS_")}
    bus = read("src/cli/cmd_bus.cpp")
    subs["BUS"] = {v for k, v in pstr_defs(bus).items() if k.startswith("PSTR_BUS_")} \
        | {"I2C", "SPI", "CAN", "SERIAL"}
    # OUTPUT <module>: module names from the output table (case-insensitive).
    outputs = read("src/outputs/output_manager.cpp")
    subs["OUTPUT"] |= {n.upper() for n in re.findall(r'\{"(\w+)",\s*(?:true|false)', outputs)}
    subs["OUTPUT"] |= {"STATUS"}
    subs["TEST"] |= {"#"}
    # TRANSPORT <plane> ...: plane names come from parsePlane(), not the handler.
    helpers = read("src/cli/command_helpers.cpp")
    subs["TRANSPORT"] |= set(re.findall(r'"([A-Z]+)"', function_body(helpers, "MessagePlane parsePlane(")))
    cat["subs"] = subs

    setsrc = read("src/cli/cmd_set.cpp")
    defs = pstr_defs(setsrc)
    cat["set_fields"] = {v for k, v in defs.items() if k.startswith("PSTR_SET_")}
    calmap = dict(re.findall(r'\{\s*(PSTR_CAL_\w+),\s*set_cal_\w+,\s*\w+,\s*(CAL_\w+)\s*\}', setsrc))
    cat["cal_profiles"] = {defs[k]: v for k, v in calmap.items()}

    sensors, calt = {}, {}
    for path in glob.glob(os.path.join(ROOT, "src/sensors/sensor_library/sensors/*.h")):
        text = open(path, encoding="utf-8").read()
        names = pstr_defs(text)
        for m in re.finditer(r'X_SENSOR\((PSTR_\w+),.*?\\\s*\n\s*(MEASURE_\w+),\s*(CAL_\w+)', text, re.S):
            if m.group(1) in names:
                sensors[names[m.group(1)]] = m.group(2)
                calt[names[m.group(1)]] = m.group(3)
    cat["sensors"], cat["sensor_cal"] = sensors, calt

    apps_src = read("src/sensors/application_presets.cpp")
    names = pstr_defs(apps_src)
    cat["apps"] = {names[m.group(1)]: m.group(2) for m in re.finditer(
        r'\.name = (PSTR_\w+),.*?\.expectedMeasurementType = (MEASURE_\w+)', apps_src, re.S)}

    cats = pstr_defs(read("src/sensors/sensor_library/sensor_categories.cpp"))
    cat["categories"] = {v for k, v in cats.items()
                         if k.startswith("PSTR_CAT_") and not k.endswith("_LABEL")} \
        | {"NTC", "TC", "RPM", "SPEED"}  # aliases in sensor_categories.h
    return cat


# ----------------------------------------------------------------------------
# Checks
# ----------------------------------------------------------------------------

# Words that have appeared in the docs as if they were top-level commands.
FORMER_COMMANDS = {"DUMP", "RESET", "RELOAD", "ENABLE", "DISABLE", "SHOW"}
PLACEHOLDER = re.compile(r'^[<\[].*|.*\.\.\.$|^\.\.\.$')
RETIRED = {"RELAY": "RELAY is deprecated; use RULE / ACTION",
           "SELFTEST": "SELFTEST was retired; use SYSTEM DIAG INTEGRITY"}
VENDOR = re.compile(r'\b(?:VDO|SMITHS|SW|ACDELCO|AC_DELCO|BOSCH|JEEP|GENERIC|MPX|AEM|BME280)_[A-Z0-9_]*[A-Z0-9]\b')


def alternatives(tok):
    return [a for a in re.split(r'[|/]', tok.upper()) if a]


def check_command(cat, words, pinsens):
    cmd = words[0].upper()
    if cmd in RETIRED:
        return RETIRED[cmd]
    if cmd not in cat["commands"]:
        return f"unknown command {words[0]}"
    if len(words) < 2 or PLACEHOLDER.match(words[1]):
        return None

    subs = cat["subs"].get(cmd)
    if subs is not None:
        for alt in alternatives(words[1]):
            if "#" in subs and alt.isdigit():
                continue
            if alt not in subs:
                return f"{cmd}: unknown subcommand {words[1]}"

    if cmd != "SET" or len(words) < 3 or PLACEHOLDER.match(words[2]):
        return None
    pin, field = words[1], words[2].upper()
    if pin.upper() == "CAN" and re.fullmatch(r'0x[0-9A-Fa-f]+|\d+', words[2]):
        return None                                   # SET CAN <pid> import
    arg = words[3] if len(words) > 3 and not PLACEHOLDER.match(words[3]) else None

    if field in cat["set_fields"]:
        if field == "CAL" and arg:
            prof = arg.upper()
            if prof not in cat["cal_profiles"]:
                return f"unknown CAL profile {arg}"
            want = cat["cal_profiles"][prof]
            have = cat["sensor_cal"].get(pinsens.get(pin, ""))
            if want != "CAL_NONE" and have and have != want:
                return f"CAL {prof} doesn't apply to {pinsens[pin]} ({have})"
        if field == "SENSOR" and arg:
            rest = words[3:]
            if rest[0].upper() in cat["categories"] and len(rest) > 1:
                rest = rest[1:]
            if rest[0] not in cat["sensors"]:
                return f"unknown sensor {rest[0]}"
            pinsens[pin] = rest[0]
        if field == "APPLICATION" and arg:
            if arg not in cat["apps"]:
                return f"unknown application {arg}"
            if len(words) > 4 and not words[4].startswith("#"):
                return "SET <pin> APPLICATION takes one name (the sensor is ignored); use SET <pin> <app> <sensor>"
        return None

    if field in cat["apps"]:
        if not arg:
            return f"SET <pin> {field} needs a sensor name"
        if arg not in cat["sensors"]:
            return f"unknown sensor {arg}"
        pinsens[pin] = arg
        meas = cat["sensors"][arg]
        if meas != cat["apps"][field] and meas != "MEASURE_CAN":
            return f"{arg} measures {meas[8:].lower()}, but {field} expects {cat['apps'][field][8:].lower()}"
        return None
    return f"unknown SET field or application {words[2]}"


def strip_comment(line):
    for sep in ("  #", " # ", "\t#", "  //"):
        i = line.find(sep)
        if i >= 0:
            line = line[:i]
    return line.strip()


def check_file(cat, path):
    problems = []
    rel = os.path.relpath(path, ROOT)
    in_block = skip_block = False
    prev = ""
    pinsens = {}
    for no, line in enumerate(open(path, encoding="utf-8"), 1):
        stripped = line.strip()
        if stripped.startswith("```"):
            in_block = not in_block
            skip_block = in_block and IGNORE in prev
            pinsens = {}
            prev = line
            continue
        prev = line
        if IGNORE in line or skip_block:
            continue

        candidates = []
        if in_block:
            text = strip_comment(re.sub(r'^\s*(?:>|\$|preOBD>)\s*', '', line.rstrip()))
            if re.match(r'^[A-Z?][A-Z_0-9]*(\s|$)', text):
                candidates.append(text)
        else:
            for m in re.finditer(r'`([^`]+)`', line):
                text = m.group(1).strip()
                first = text.split()[0] if text.split() else ""
                if first in cat["commands"] and len(text.split()) > 1 and re.match(r'^[A-Z<0-9]', text.split()[1]):
                    candidates.append(text)

        for text in candidates:
            words = text.split()
            if in_block and words[0].upper() not in cat["commands"] and words[0].upper() not in RETIRED:
                # Other capitalised lines in code blocks are usually program
                # output; only flag words that have been mistaken for commands.
                if words[0] not in FORMER_COMMANDS:
                    continue
                if len(words) > 1 and not re.match(r'^[A-Z<0-9]', words[1]):
                    continue
            if not in_block and words[0].upper() == "RELAY":
                continue                                   # migration tables in prose
            err = check_command(cat, words, pinsens)
            if err:
                problems.append(f"{rel}:{no}: {err}  | {text[:80]}")

        for m in VENDOR.finditer(line):
            name = m.group(0)
            if name not in cat["sensors"] and not name.endswith(("_GUIDE", "_PIN", "_BUS")):
                problems.append(f"{rel}:{no}: unknown sensor-like name {name}")
    return problems


def main():
    cat = load_catalog()
    if len(sys.argv) > 1:
        files = [os.path.abspath(f) for f in sys.argv[1:]]
    else:
        files = [os.path.join(ROOT, "README.md")] + sorted(
            glob.glob(os.path.join(ROOT, "docs", "**", "*.md"), recursive=True))
    problems = []
    for f in files:
        problems += check_file(cat, f)
    for p in problems:
        print(p)
    if problems:
        print(f"\n{len(problems)} documentation problem(s). Fix the doc, or if the "
              f"text is intentionally not a real command, add <!-- {IGNORE} -->.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
