#!/usr/bin/env python3
"""
openEMS Registry Validation Tool

A CI-friendly script to validate the integrity of the C registry files and,
optionally, the generated static config in config.h.

Parses the modular sensor library structure:
  - src/lib/sensor_library.h (orchestrator)
  - src/lib/sensor_library/sensors/*.h (sensor definitions using X-macro pattern)
"""

import argparse
import os
import sys
from typing import List, Dict, Any

# Ensure the script can find the openems_config package
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '.')))

from openems_config.registry_parser import (
    parse_sensor_library,
    parse_application_presets,
    parse_units_registry,
)
from openems_config.validators import (
    validate_hash_collisions,
    validate_index_references,
    validate_measurement_types,
    validate_hash_algorithm,
    validate_config_h,
)

def main():
    """Main execution function."""
    parser = argparse.ArgumentParser(description="openEMS Registry Validation Tool")
    parser.add_argument(
        "--project-dir",
        default=".",
        help="Path to the openEMS project root directory.",
    )
    parser.add_argument(
        "--check-config",
        action="store_true",
        help="Also validate the generated static config in src/config.h",
    )
    parser.add_argument(
        "--ci",
        action="store_true",
        help="Enable CI mode (machine-readable exit codes).",
    )
    args = parser.parse_args()

    print("=== openEMS Registry Validation ===")

    # Load registries
    try:
        sensors = parse_sensor_library(os.path.join(args.project_dir, "src/lib/sensor_library.h"))
        apps = parse_application_presets(os.path.join(args.project_dir, "src/lib/application_presets.h"))
        units = parse_units_registry(os.path.join(args.project_dir, "src/lib/units_registry.h"))
    except FileNotFoundError as e:
        print(f"\n\u2717 Critical Error: Could not find a registry file. {e}", file=sys.stderr)
        sys.exit(1)

    all_errors = []
    all_warnings = []

    # Run validations
    print("\nChecking sensor_library.h...")
    print(f"  \u2713 {len(sensors)} sensors loaded (from sensor_library/sensors/*.h)")

    print("\nChecking application_presets.h...")
    print(f"  \u2713 {len(apps)} applications loaded")

    print("\nChecking units_registry.h...")
    print(f"  \u2713 {len(units)} units loaded")

    print("\nChecking hash algorithm correctness...")
    hash_errors = validate_hash_algorithm(sensors, apps, units)
    if hash_errors:
        all_errors.extend(hash_errors)
        for err in hash_errors: print(f"  \u2717 {err}")
    else:
        print("  \u2713 All hashes match djb2 algorithm")

    print("\nChecking for hash collisions...")
    collision_errors = validate_hash_collisions(sensors, apps, units)
    if collision_errors:
        all_errors.extend(collision_errors)
        for err in collision_errors: print(f"  \u2717 {err}")
    else:
        print("  \u2713 No hash collisions found")

    print("\nChecking cross-references...")
    ref_errors, ref_warnings = validate_index_references(apps, sensors)
    if ref_errors:
        all_errors.extend(ref_errors)
        for err in ref_errors: print(f"  \u2717 {err}")
    if ref_warnings:
        all_warnings.extend(ref_warnings)
        for warn in ref_warnings: print(f"  \u26A0 {warn}")
    if not ref_errors and not ref_warnings:
        print("  \u2713 All index references are valid")

    print("\nChecking measurement types...")
    type_errors = validate_measurement_types(apps, sensors)
    if type_errors:
        all_errors.extend(type_errors)
        for err in type_errors: print(f"  \u2717 {err}")
    else:
        print("  \u2713 All measurement types are consistent")

    if args.check_config:
        print("\nChecking config.h...")
        config_h_path = os.path.join(args.project_dir, 'src', 'config.h')
        config_errors = validate_config_h(config_h_path, sensors, apps)
        if config_errors:
            all_errors.extend(config_errors)
            for err in config_errors: print(f"  \u2717 {err}")
        else:
            print("  \u2713 config.h is valid")

    # Final summary
    print("\n=== Summary ===")
    status_str = f"({len(all_errors)} errors, {len(all_warnings)} warnings)"
    if not all_errors and not all_warnings:
        print("Status: PASSED")
    elif not all_errors and all_warnings:
        print(f"Status: PASSED WITH WARNINGS {status_str}")
    else:
        print(f"Status: FAILED {status_str}")

    if args.ci:
        if all_errors:
            sys.exit(1)

    sys.exit(0)

if __name__ == "__main__":
    main()
