"""
avr_progmem_guard.py - Post-link check that AVR PROGMEM fits in the low 64 KB (#523)

AVR code reads flash data with near pgm_read_* / LPM, which address only
0x0000-0xFFFF. The avr6 linker script puts the vectors and trampolines first,
then every `.progmem*` section, then any jumptables/lowtext and `__ctors_start`.
If that block grows past 0x10000, the link still succeeds, but tables and F() strings
above the line read back from the wrong address. mega2560 then printed garbage
and reset every ~3 s, and nothing caught it until it was run on a board.

This fails the build instead, reporting how far over or under the line it is.
`__ctors_start` is used as the end of the data block: it can only overstate,
by the size of any jumptables/lowtext.

No-op on every non-AVR env.
"""

# pylint: disable=undefined-variable
# pyright: reportUndefinedVariable=false
Import("env")  # type: ignore

import os
import re
import subprocess
import sys

LIMIT = 0x10000
# PSTR's `static const char __c[]`: `__c`, mangled `..E3__c`, or LTO's `__c.lto_priv.N`
PSTR_SYMBOL = re.compile(r"(^|E\d+)__c(\.|$)")


def _fail(env, elf):
    # Otherwise the next `pio run` finds the ELF up to date, skips this
    # check and reports SUCCESS, and an upload would flash the stale image.
    for path in (elf, os.path.splitext(elf)[0] + ".hex"):
        if os.path.exists(path):
            os.remove(path)
    env.Exit(1)


def _check(target, source, env):
    elf = str(target[0])
    nm = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "toolchain-atmelavr", "bin", "avr-nm")
    if not os.path.exists(nm):
        nm = "avr-nm"
    try:
        out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.stderr.write("avr_progmem_guard: could not run avr-nm (%s); check skipped\n" % e)
        return
    end = None
    in_ram = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        if parts[2] == "__ctors_start":
            end = int(parts[0], 16)
        # A PSTR()/F() literal is a static `__c`. In RAM (0x800000+) it means
        # avr_pstr_merge.h's section was dropped (a function template does
        # that), and pgm_read_* on it reads flash at the wrong address.
        elif PSTR_SYMBOL.search(parts[2]) and int(parts[0], 16) >= 0x800000:
            in_ram.append(parts[2])
    if in_ram:
        sys.stderr.write(
            "\nERROR: %d PSTR()/F() string(s) were placed in RAM, so they print garbage:\n  %s\n"
            "  F()/PSTR in a function template loses its section (boards/avr_pstr_merge.h).\n"
            "  Use a named PROGMEM array instead.\n\n" % (len(in_ram), "\n  ".join(in_ram[:10]))
        )
        _fail(env, elf)
        return
    if end is None:
        sys.stderr.write("avr_progmem_guard: no __ctors_start in %s; check skipped\n" % elf)
        return
    if end > LIMIT:
        sys.stderr.write(
            "\nERROR: PROGMEM ends at 0x%05X, %d B past the 64 KB that near pgm_read_* can reach.\n"
            "  Tables and F() strings above 0x10000 read back as garbage and the board\n"
            "  crash-loops (#523). Free PROGMEM (duplicate strings, help text, features\n"
            "  in board.h) until this fits.\n\n" % (end, end - LIMIT)
        )
        _fail(env, elf)
        return
    print("PROGMEM: ends at 0x%04X, %d B below the 64 KB near-read limit" % (end, LIMIT - end))


if env.subst("$PIOPLATFORM") == "atmelavr":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _check)
