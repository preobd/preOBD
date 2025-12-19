#!/usr/bin/env python3
"""
openEMS Static Configuration Tool

An interactive CLI to generate static sensor configurations for openEMS,
reducing the reliance on EEPROM and enabling compile-time optimizations.
"""
import argparse
import json
import os
import sys
from typing import List, Dict, Any, Optional

# Ensure the script can find the openems_config package
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '.')))

from openems_config.registry_parser import (
    parse_sensor_library,
    parse_application_presets,
    parse_units_registry,
)
from openems_config.platform import (
    detect_platform,
    get_platform_limits,
    validate_pin,
)
from openems_config.config_generator import (
    generate_config_block,
    update_config_h,
    generate_thin_library_files,
    generate_advanced_config_block,
    update_advanced_config_h,
)

TOOL_VERSION = "1.0.0"

def print_header():
    """Prints the tool's header."""
    print(f"=== openEMS Static Configuration Tool v{TOOL_VERSION} ===")
    print()

def load_registries(project_dir: str) -> Optional[Dict[str, List[Dict[str, Any]]]]:
    """Loads all C registry files."""
    print("Loading registries...")
    try:
        sensors = parse_sensor_library(os.path.join(project_dir, "src/lib/sensor_library.h"))
        apps = parse_application_presets(os.path.join(project_dir, "src/lib/application_presets.h"))
        units = parse_units_registry(os.path.join(project_dir, "src/lib/units_registry.h"))

        print(f"  \u2713 {len(sensors)} sensors from sensor_library.h")
        print(f"  \u2713 {len(apps)} applications from application_presets.h")
        print(f"  \u2713 {len(units)} units from units_registry.h")
        print()
        return {"sensors": sensors, "applications": apps, "units": units}
    except FileNotFoundError as e:
        print(f"Error: Could not find a registry file. {e}", file=sys.stderr)
        return None

def prompt_for_pin(platform: str, used_pins: List[str], default: str = "") -> str:
    """Prompts the user for a pin and validates it."""
    while True:
        prompt = f"Pin (e.g., A0, 6) [{default}]: " if default else "Pin (e.g., A0, 6): "
        pin_str = input(prompt).strip().upper() or default
        if not pin_str: continue

        temp_used_pins = [p.upper() for p in used_pins]
        if default and pin_str == default.upper() and default.upper() in temp_used_pins:
            temp_used_pins.remove(default.upper())

        error = validate_pin(pin_str, platform, temp_used_pins)
        if error:
            print(f"  \u2717 Invalid pin: {error}")
        else:
            return pin_str

def prompt_for_selection(options: List[Dict[str, Any]], title: str, default: int = -1) -> int:
    """Generic prompt for selecting from a list of registry items."""
    print(f"\n{title}:")
    for item in options:
        print(f"  {item['index']}. {item.get('label', item.get('name', 'N/A'))}")

    while True:
        try:
            prompt = f"Select [{default}]: " if default != -1 else "Select: "
            selection_str = input(prompt).strip() or str(default)
            if selection_str == "-1": continue
            selection = int(selection_str)

            if any(item['index'] == selection for item in options):
                return selection
            else:
                print("  \u2717 Invalid selection.")
        except ValueError:
            print("  \u2717 Please enter a number.")

def prompt_for_custom_calibration(inp: Dict[str, Any], registries: Dict) -> Optional[Dict[str, Any]]:
    """Prompts user if they want to add custom calibration for this input."""
    print(f"\nCustom calibration for Input {inp['input_number']}?")
    choice = input("Add custom calibration? [y/N]: ").strip().lower()

    if choice != 'y':
        return None

    # Get sensor info to determine calibration type
    sensor_info = next((s for s in registries['sensors'] if s['index'] == inp['sensor_index']), None)
    if not sensor_info:
        print("  Error: Could not find sensor info")
        return None

    cal_type_name = sensor_info.get('calibrationType', '')

    print(f"\nCalibration type for {sensor_info['name']}: {cal_type_name}")
    print("Available calibration types:")
    print("  1. Thermistor (Steinhart-Hart)")
    print("  2. Pressure (Linear)")
    print("  3. Pressure (Polynomial/VDO)")
    print("  4. RPM")

    while True:
        try:
            cal_choice = input("Select calibration type [1-4]: ").strip()
            cal_choice = int(cal_choice)
            if cal_choice in [1, 2, 3, 4]:
                break
            print("  Invalid selection. Choose 1-4.")
        except ValueError:
            print("  Please enter a number.")

    calibration = {"type": cal_choice}

    if cal_choice == 1:  # Thermistor Steinhart-Hart
        calibration.update({
            "calibration_name": "ThermistorSteinhartCalibration",
            "bias_resistor": float(input("Bias resistor (Ohms) [1000.0]: ").strip() or "1000.0"),
            "steinhart_a": float(input("Steinhart A coefficient [1.129e-3]: ").strip() or "1.129e-3"),
            "steinhart_b": float(input("Steinhart B coefficient [2.341e-4]: ").strip() or "2.341e-4"),
            "steinhart_c": float(input("Steinhart C coefficient [8.775e-8]: ").strip() or "8.775e-8")
        })
    elif cal_choice == 2:  # Pressure Linear
        calibration.update({
            "calibration_name": "LinearCalibration",
            "voltage_min": float(input("Minimum voltage (V) [0.5]: ").strip() or "0.5"),
            "voltage_max": float(input("Maximum voltage (V) [4.5]: ").strip() or "4.5"),
            "pressure_min": float(input("Minimum pressure (bar) [0.0]: ").strip() or "0.0"),
            "pressure_max": float(input("Maximum pressure (bar) [5.0]: ").strip() or "5.0")
        })
    elif cal_choice == 3:  # Pressure Polynomial
        calibration.update({
            "calibration_name": "PolynomialCalibration",
            "bias_resistor": float(input("Bias resistor (Ohms) [1000.0]: ").strip() or "1000.0"),
            "poly_a": float(input("Polynomial A coefficient [-0.3682]: ").strip() or "-0.3682"),
            "poly_b": float(input("Polynomial B coefficient [36.465]: ").strip() or "36.465"),
            "poly_c": float(input("Polynomial C coefficient [10.648]: ").strip() or "10.648")
        })
    elif cal_choice == 4:  # RPM
        calibration.update({
            "calibration_name": "RPMCalibration",
            "poles": int(input("Number of poles [12]: ").strip() or "12"),
            "pulley_ratio": float(input("Pulley ratio [3.0]: ").strip() or "3.0"),
            "calibration_mult": float(input("Calibration multiplier [1.0]: ").strip() or "1.0"),
            "timeout_ms": int(input("Timeout (ms) [2000]: ").strip() or "2000"),
            "min_rpm": int(input("Minimum RPM [100]: ").strip() or "100"),
            "max_rpm": int(input("Maximum RPM [10000]: ").strip() or "10000")
        })

    return calibration

def edit_input(inp: Dict[str, Any], platform: str, used_pins: List[str], registries: Dict) -> Dict[str, Any]:
    """Interactive prompt to edit a single input."""
    print(f"\n--- Editing Input {inp['input_number']} ---")

    pin = prompt_for_pin(platform, used_pins, default=str(inp['pin']))

    available_apps = [app for app in registries['applications'] if app['is_implemented']]
    app_index = prompt_for_selection(available_apps, "Applications", default=inp['application_index'])
    selected_app = next(app for app in available_apps if app['index'] == app_index)

    compatible_sensors = [s for s in registries['sensors'] if s['is_implemented'] and s['measurementType'] == selected_app['expectedMeasurementType']]
    sensor_index = prompt_for_selection(compatible_sensors, f"Compatible sensors for {selected_app['name']}", default=inp['sensor_index'])

    inp.update({
        "pin": pin,
        "application": selected_app['name'], "application_index": app_index,
        "sensor": next(s['name'] for s in compatible_sensors if s['index'] == sensor_index), "sensor_index": sensor_index
    })

    # Prompt for custom calibration
    custom_cal = prompt_for_custom_calibration(inp, registries)
    if custom_cal:
        inp['custom_calibration'] = custom_cal
    elif 'custom_calibration' in inp:
        del inp['custom_calibration']

    return inp

def main():
    parser = argparse.ArgumentParser(description="openEMS Static Configuration Tool")
    parser.add_argument("--load", metavar="FILE", help="Load configuration from a JSON file for editing.")
    parser.add_argument("--project-dir", default=".", help="Path to the openEMS project root directory.")
    parser.add_argument("--generate-thin-libs", action="store_true", help="Generate thinned sensor and application libraries.")
    parser.add_argument("--platform", help="Specify the target platform (e.g., uno, megaatmega2560). Overrides auto-detection.")
    args = parser.parse_args()

    print_header()

    registries = load_registries(args.project_dir)
    if not registries: sys.exit(1)

    platform = args.platform or detect_platform(args.project_dir) or "uno"
    limits = get_platform_limits(platform)
    print(f"Detected platform: {platform.upper()} ({limits['max_inputs']} inputs max)\n")

    inputs: List[Dict[str, Any]] = []
    should_add_new = True

    if args.load:
        should_add_new = False
        try:
            with open(args.load, 'r') as f:
                inputs = json.load(f).get("inputs", [])
            print(f"Loaded {len(inputs)} inputs from {args.load}")

            while True:
                print("\nCurrent configuration:")
                for i, inp in enumerate(inputs):
                    print(f"  {i}) Pin: {inp['pin']}, App: {inp['application']}, Sensor: {inp['sensor']}")

                choice = input("\nEdit (e), Add (a), Delete (d), or Finish (f)? [f]: ").strip().lower()

                if choice == 'e':
                    try:
                        idx = int(input("Enter the number of the input to edit: "))
                        used_pins = [str(i['pin']) for i in inputs]
                        inputs[idx] = edit_input(inputs[idx], platform, used_pins, registries)
                    except (ValueError, IndexError):
                        print("Invalid selection.")
                elif choice == 'a':
                    should_add_new = True
                    break
                elif choice == 'd':
                    try:
                        idx = int(input("Enter the number of the input to delete: "))
                        inputs.pop(idx)
                    except (ValueError, IndexError):
                        print("Invalid selection.")
                elif choice in ('f', ''):
                    break
        except (IOError, json.JSONDecodeError) as e:
            print(f"Error loading config file: {e}", file=sys.stderr)
            sys.exit(1)

    if should_add_new:
        while len(inputs) < limits['max_inputs']:
            used_pins = [str(i['pin']) for i in inputs]
            print(f"\n=== Adding Input {len(inputs)} ===")

            pin = prompt_for_pin(platform, used_pins)

            available_apps = [app for app in registries['applications'] if app['is_implemented']]
            app_index = prompt_for_selection(available_apps, "Applications")
            selected_app = next(app for app in available_apps if app['index'] == app_index)

            compatible_sensors = [s for s in registries['sensors'] if s['is_implemented'] and s['measurementType'] == selected_app['expectedMeasurementType']]
            sensor_index = prompt_for_selection(compatible_sensors, f"Compatible sensors for {selected_app['name']}")

            new_input = {
                "input_number": len(inputs), "pin": pin,
                "application": selected_app['name'], "application_index": app_index,
                "sensor": next(s['name'] for s in compatible_sensors if s['index'] == sensor_index), "sensor_index": sensor_index
            }

            # Prompt for custom calibration
            custom_cal = prompt_for_custom_calibration(new_input, registries)
            if custom_cal:
                new_input['custom_calibration'] = custom_cal

            inputs.append(new_input)

            if input("\nConfigure another input? [y/N]: ").strip().lower() != 'y': break

    for i, inp in enumerate(inputs):
        inp['input_number'] = i

    save_path = args.load
    if not save_path:
        if input("\nSave to JSON? [y/N]: ").strip().lower() == 'y':
            filename = input("File: ").strip()
            if filename:
                if not filename.endswith(".json"): filename += ".json"
                if not os.path.exists('configs'): os.makedirs('configs')
                save_path = os.path.join('configs', os.path.basename(filename))

    if save_path:
        with open(save_path, 'w') as f:
            json.dump({"metadata": {"tool_version": TOOL_VERSION, "platform": platform}, "inputs": inputs}, f, indent=2)
        print(f"\u2713 Saved to {save_path}")

    print("\nGenerating config.h...")
    config_h_path = os.path.join(args.project_dir, 'src', 'config.h')
    new_block = generate_config_block(inputs, platform, TOOL_VERSION)
    if update_config_h(config_h_path, new_block):
        print(f"\u2713 Backup: {config_h_path}.bak")
        print(f"\u2713 Generated with {len(inputs)} inputs")
    else:
        print(f"\u2717 Failed to update {config_h_path}", file=sys.stderr)

    # Generate advanced_config.h if there are custom calibrations
    advanced_config_path = os.path.join(args.project_dir, 'src', 'advanced_config.h')
    advanced_block = generate_advanced_config_block(inputs, TOOL_VERSION)
    if advanced_block:
        print("\nGenerating advanced_config.h...")
        if update_advanced_config_h(advanced_config_path, advanced_block):
            print(f"\u2713 Backup: {advanced_config_path}.bak")
            print(f"\u2713 Generated custom calibrations for {len([i for i in inputs if 'custom_calibration' in i])} inputs")
        else:
            print(f"\u2717 Failed to update {advanced_config_path}", file=sys.stderr)
    else:
        # Clear custom calibrations if none are defined
        if os.path.exists(advanced_config_path):
            update_advanced_config_h(advanced_config_path, None)
            print("\u2713 Cleared custom calibrations from advanced_config.h")

    if args.generate_thin_libs:
        print("\nGenerating thin libraries...")
        output_dir = os.path.join(args.project_dir, 'src', 'lib', 'generated')
        generate_thin_library_files(inputs, registries['sensors'], registries['applications'], output_dir, args.project_dir)
        print(f"\u2713 Generated thin libraries in {output_dir}")

    print(f"\nReady to compile: pio run -e {platform}")

if __name__ == "__main__":
    main()
