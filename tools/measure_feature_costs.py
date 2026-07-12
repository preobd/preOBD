#!/usr/bin/env python3
"""Measure per-feature flash/RAM cost on a memory-constrained build (#145).

Toggles one knob at a time in a board profile relative to the current baseline,
rebuilds, and diffs flash (.text + .data) and static RAM (.data + .bss) from the
ELF. Regenerate the table in docs/feature_costs.md as the code evolves.

Usage:
    python3 tools/measure_feature_costs.py            # mega2560 (default)
    python3 tools/measure_feature_costs.py --env mega2560

Notes / gotchas:
  * PlatformIO/SCons does NOT track the `-include`d profile header as a build
    dependency, so an incremental build serves a STALE ELF after a profile edit.
    This script runs `-t clean` before every build — slower, but the only way to
    get trustworthy numbers.
  * The profile is always restored (try/finally), even on error or Ctrl-C.
  * "Enable currently-off" measurements can fail to build when the feature needs
    library deps the env doesn't pull (e.g. SD on mega). Failures are reported,
    not fatal — that "won't build as-is" result is itself informative.
"""
import argparse, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per-env profile + the knobs worth measuring. Only mega2560 is curated today
# (the one platform where the 8 KB RAM budget makes costs decision-relevant).
ENVS = {
    "mega2560": {
        "profile": "src/profiles/profile_mega2560.h",
        "size_tool": "~/.platformio/packages/toolchain-atmelavr/bin/avr-size",
        "ram_total": 8192,
        # currently-off features to try enabling (with the siblings they need)
        "enable": {
            "ENABLE_REALDASH":         ["#define ENABLE_REALDASH 1"],
            "ENABLE_TEST_MODE":        ["#define ENABLE_TEST_MODE 1"],
            "ENABLE_RELAY_OUTPUT":     ["#define ENABLE_RELAY_OUTPUT 1"],
            "ENABLE_SD_LOGGING":       ["#define ENABLE_SD_LOGGING 1",
                                        "#define SUPPORTS_SD 1", "#define SD_PIN 53"],
            "ENABLE_ELM327":           ["#define ENABLE_ELM327 1"],
            "ENABLE_FRAMED_TRANSPORT": ["#define ENABLE_FRAMED_TRANSPORT 1"],
        },
        # structural knobs: (label, macro, new value)
        "struct": [
            ("MAX_INPUTS 8->4",          "MAX_INPUTS", "4"),
            ("MAX_INPUTS 8->16",         "MAX_INPUTS", "16"),
            ("CLI_BUFFER_SIZE 820->512", "CLI_BUFFER_SIZE", "512"),
            ("MAX_PID_ENTRIES 8->16",    "MAX_PID_ENTRIES", "16"),
            ("MAX_PIN_REGISTRY 24->16",  "MAX_PIN_REGISTRY", "16"),
            ("CAN_CACHE_SIZE 8->16",     "CAN_CACHE_SIZE", "16"),
        ],
    },
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", default="mega2560", choices=sorted(ENVS),
                    help="PlatformIO env to measure (default: mega2560)")
    args = ap.parse_args()
    cfg = ENVS[args.env]

    profile = os.path.join(ROOT, cfg["profile"])
    elf = os.path.join(ROOT, ".pio", "build", args.env, "firmware.elf")
    size_tool = os.path.expanduser(cfg["size_tool"])
    bak = profile + ".measbak"
    orig = open(profile).read()

    def build():
        subprocess.run(["pio", "run", "-e", args.env, "-t", "clean"],
                       cwd=ROOT, capture_output=True, text=True)
        r = subprocess.run(["pio", "run", "-e", args.env],
                           cwd=ROOT, capture_output=True, text=True)
        return os.path.exists(elf), r.stdout + r.stderr

    def measure():
        cols = subprocess.check_output([size_tool, elf], text=True).splitlines()[1].split()
        text, data, bss = int(cols[0]), int(cols[1]), int(cols[2])
        return text + data, data + bss  # flash, static RAM

    def fail_reason(log):
        for ln in log.splitlines():
            if "error:" in ln or "undefined reference" in ln:
                return ln.strip()[:100]
        return "build failed"

    # mutations
    def mut_comment(name, s):
        return re.sub(rf'^(#define\s+{name}\b.*)$', r'// \1', s, count=1, flags=re.M)

    def mut_add(lines, s):
        return s + "\n" + "\n".join(lines) + "\n"

    def mut_set(name, val, s):
        return re.sub(rf'(#define\s+{name}\s+)\S+', rf'\g<1>{val}', s, count=1)

    enable_macros = [m for m in re.findall(r'^#define\s+(ENABLE_\w+)', orig, re.M)]

    shutil.copy(profile, bak)
    results = []
    try:
        ok, log = build()
        if not ok:
            print("BASELINE BUILD FAILED:\n", fail_reason(log)); sys.exit(1)
        base_flash, base_ram = measure()
        print(f"baseline {args.env}: flash={base_flash}  ram={base_ram} "
              f"(headroom {cfg['ram_total'] - base_ram} B)\n")

        def run(label, new_src):
            open(profile, "w").write(new_src)
            ok, log = build()
            shutil.copy(bak, profile)
            if not ok:
                results.append((label, None, None, fail_reason(log)))
                print(f"{label:32} FAILED: {fail_reason(log)}")
                return
            f, r = measure()
            results.append((label, f - base_flash, r - base_ram, ""))
            print(f"{label:32} flash {f-base_flash:+8}   ram {r-base_ram:+7}")

        print("--- DISABLE currently-on features (negative = saved) ---")
        for name in enable_macros:
            run("OFF " + name, mut_comment(name, orig))
        print("\n--- ENABLE currently-off features ---")
        for name, lines in cfg["enable"].items():
            run("ON " + name, mut_add(lines, orig))
        print("\n--- structural scaling ---")
        for label, macro, val in cfg["struct"]:
            run(label, mut_set(macro, val, orig))
    finally:
        shutil.copy(bak, profile)
        os.remove(bak)

    print("\n=== MARKDOWN ===")
    print("| Knob | ΔFlash | ΔStatic RAM | Note |")
    print("|---|---|---|---|")
    for label, df, dr, note in results:
        if df is None:
            print(f"| `{label}` | — | — | {note} |")
        else:
            print(f"| `{label}` | {df:+} B | {dr:+} B | |")


if __name__ == "__main__":
    main()
