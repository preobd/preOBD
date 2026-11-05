"""
teensy4_stack_guard.py - Post-link check that a Teensy 4 image leaves room for the stack

On Teensy 4.x, RAM1 (512 KB) holds code (ITCM, allocated in 32 KB blocks) and
variables (DTCM); the stack gets whatever is left: `_estack - _ebss`, the
"free for local variables" teensy_size prints. Nothing checked it. On teensy41
it fell from 47 KB to 4.7 KB in a month (2026-09/10) without anyone deciding
to spend it: code crossed a 32 KB block boundary by 72 bytes, and a duplicated
CAN driver (#555) held 10.5 KB. A stack that overflows corrupts memory or
hard-faults, which in a car looks like random resets.

This prints the figure on every build and fails below a floor (16 KB by
default; a board can set `custom_stack_floor = <bytes>` in its [env], decimal
or 0x hex). It fails closed: if nm can't be run or the symbols are missing,
the build fails rather than passing unchecked. To get
room back: mark cold code FLASHMEM (CLI handlers, JSON, boot-only setup),
which can free a whole 32 KB ITCM block, or move large tables to PROGMEM.

No-op on every env that isn't a Teensy 4 (imxrt1062).
"""

# pylint: disable=undefined-variable
# pyright: reportUndefinedVariable=false
Import("env")  # type: ignore

import os
import subprocess
import sys

DEFAULT_FLOOR = 16 * 1024


def _fail(env, elf):
    # Otherwise the next `pio run` finds the ELF up to date, skips this
    # check and reports SUCCESS, and an upload would flash the stale image.
    for path in (elf, os.path.splitext(elf)[0] + ".hex"):
        if os.path.exists(path):
            os.remove(path)
    env.Exit(1)


def _nm(env):
    # The nm beside the compiler that linked this image, found on the build
    # environment's PATH (not the shell's), so it works on any host
    cc = env.WhereIs(env.subst("$CC")) or env.subst("$CC")
    name = os.path.basename(cc).replace("gcc", "nm")
    nm = os.path.join(os.path.dirname(cc), name)
    return nm if os.path.isfile(nm) else env.WhereIs("arm-none-eabi-nm")


def _floor(env, elf):
    raw = env.GetProjectOption("custom_stack_floor", str(DEFAULT_FLOOR))
    try:
        return int(str(raw).strip(), 0)
    except ValueError:
        sys.stderr.write("\nERROR: custom_stack_floor = %r is not a number of bytes (e.g. 16384 or 0x4000)\n\n" % raw)
        _fail(env, elf)
        return None


def _check(target, source, env):
    elf = str(target[0])
    # Fail closed: a guard that skips when it can't look passes every build
    nm = _nm(env)
    try:
        out = subprocess.run([nm or "arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.stderr.write("\nERROR: teensy4_stack_guard could not run nm (%s), so the stack was not checked\n\n" % e)
        _fail(env, elf)
        return
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ("_ebss", "_estack"):
            syms[parts[2]] = int(parts[0], 16)
    if len(syms) != 2:
        sys.stderr.write("\nERROR: teensy4_stack_guard found no _ebss/_estack in %s\n\n" % elf)
        _fail(env, elf)
        return
    free = syms["_estack"] - syms["_ebss"]
    floor = _floor(env, elf)
    if floor is None:
        return
    if free < floor:
        sys.stderr.write(
            "\nERROR: only %d B of RAM1 left for the stack, below the %d B floor.\n"
            "  A stack overflow corrupts memory or hard-faults the board. Mark cold\n"
            "  code FLASHMEM (CLI handlers, JSON, boot-only setup): crossing back\n"
            "  under a 32 KB ITCM block boundary frees 32 KB at once. See\n"
            "  scripts/teensy4_stack_guard.py.\n\n" % (free, floor)
        )
        _fail(env, elf)
        return
    print("STACK: %d B of RAM1 free for the stack (floor %d B)" % (free, floor))


if env.BoardConfig().get("build.mcu", "") == "imxrt1062":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _check)
