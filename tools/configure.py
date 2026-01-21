#!/usr/bin/env python3
"""
openEMS Static Configuration Tool

An interactive CLI to generate static sensor configurations for openEMS,
reducing the reliance on EEPROM and enabling static build optimizations.

Parses sensor definitions from the modular sensor library structure:
  - src/lib/sensor_library.h (orchestrator)
  - src/lib/sensor_library/sensors/*.h (sensor definitions by type)
"""
import argparse
import json
import os
import sys
import time
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
    generate_static_calibrations_file,
    write_static_calibrations_file,
)

TOOL_VERSION = "1.0.0"

def print_header():
    """Prints the tool's header."""
    print(f"=== openEMS Static Configuration Tool v{TOOL_VERSION} ===")
    print()

def load_registries(project_dir: str) -> Optional[Dict[str, List[Dict[str, Any]]]]:
    """Loads all C registry files.

    Parses sensor_library.h which includes modular sensor definitions from
    sensor_library/sensors/*.h using X-macro pattern.
    """
    print("Loading registries...")
    try:
        sensors = parse_sensor_library(os.path.join(project_dir, "src/lib/sensor_library.h"))
        apps = parse_application_presets(os.path.join(project_dir, "src/lib/application_presets.h"))
        units = parse_units_registry(os.path.join(project_dir, "src/lib/units_registry.h"))

        print(f"  \u2713 {len(sensors)} sensors from sensor_library.h (+ sensor_library/sensors/)")
        print(f"  \u2713 {len(apps)} applications from application_presets.h")
        print(f"  \u2713 {len(units)} units from units_registry.h")
        print()
        return {"sensors": sensors, "applications": apps, "units": units}
    except FileNotFoundError as e:
        print(f"Error: Could not find a registry file. {e}", file=sys.stderr)
        return None

def prompt_for_pin(platform: str, used_pins: List[str], default: str = "", sensor_info: Optional[Dict] = None) -> str:
    """Prompts the user for a pin and validates it."""
    sensor_pin_requirement = sensor_info.get('pinTypeRequirement') if sensor_info else None

    while True:
        # Customize prompt for I2C sensors
        if sensor_pin_requirement == 'PIN_I2C':
            prompt = f"Pin (use 'I2C' for I2C sensors) [{default}]: " if default else "Pin (use 'I2C' for I2C sensors): "
        else:
            prompt = f"Pin (e.g., A0, 6) [{default}]: " if default else "Pin (e.g., A0, 6): "

        pin_str = input(prompt).strip().upper() or default
        if not pin_str: continue

        temp_used_pins = [p.upper() for p in used_pins]
        if default and pin_str == default.upper() and default.upper() in temp_used_pins:
            temp_used_pins.remove(default.upper())

        error = validate_pin(pin_str, platform, temp_used_pins, sensor_pin_requirement)
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

    # Map choice to type string
    type_map = {
        1: "THERMISTOR_STEINHART",
        2: "PRESSURE_LINEAR",
        3: "PRESSURE_POLYNOMIAL",
        4: "RPM"
    }

    calibration = {
        "type": type_map[cal_choice],
        "source": "CUSTOM",
        "params": {}
    }

    if cal_choice == 1:  # Thermistor Steinhart-Hart
        calibration["params"] = {
            "biasResistor": float(input("Bias resistor (Ohms) [1000.0]: ").strip() or "1000.0"),
            "steinhartA": float(input("Steinhart A coefficient [1.129e-3]: ").strip() or "1.129e-3"),
            "steinhartB": float(input("Steinhart B coefficient [2.341e-4]: ").strip() or "2.341e-4"),
            "steinhartC": float(input("Steinhart C coefficient [8.775e-8]: ").strip() or "8.775e-8")
        }
    elif cal_choice == 2:  # Pressure Linear
        calibration["params"] = {
            "voltageMin": float(input("Minimum voltage (V) [0.5]: ").strip() or "0.5"),
            "voltageMax": float(input("Maximum voltage (V) [4.5]: ").strip() or "4.5"),
            "pressureMin": float(input("Minimum pressure (bar) [0.0]: ").strip() or "0.0"),
            "pressureMax": float(input("Maximum pressure (bar) [5.0]: ").strip() or "5.0")
        }
    elif cal_choice == 3:  # Pressure Polynomial
        calibration["params"] = {
            "biasResistor": float(input("Bias resistor (Ohms) [1000.0]: ").strip() or "1000.0"),
            "polyA": float(input("Polynomial A coefficient [-0.3682]: ").strip() or "-0.3682"),
            "polyB": float(input("Polynomial B coefficient [36.465]: ").strip() or "36.465"),
            "polyC": float(input("Polynomial C coefficient [10.648]: ").strip() or "10.648")
        }
    elif cal_choice == 4:  # RPM
        calibration["params"] = {
            "poles": int(input("Number of poles [12]: ").strip() or "12"),
            "pulleyRatio": float(input("Pulley ratio [3.0]: ").strip() or "3.0"),
            "calibrationMult": float(input("Calibration multiplier [1.0]: ").strip() or "1.0"),
            "timeoutMs": int(input("Timeout (ms) [2000]: ").strip() or "2000"),
            "minRpm": int(input("Minimum RPM [100]: ").strip() or "100"),
            "maxRpm": int(input("Maximum RPM [10000]: ").strip() or "10000")
        }

    return calibration

def convert_to_unified_format(inputs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """Converts internal format to unified v1 JSON schema format."""
    unified_inputs = []
    for inp in inputs:
        unified_inp = {
            "idx": inp['input_number'],
            "pin": inp['pin'],
            "application": inp['application'],
            "applicationIndex": inp['application_index'],
            "sensor": inp['sensor'],
            "sensorIndex": inp['sensor_index']
        }
        if 'calibration' in inp:
            unified_inp['calibration'] = inp['calibration']
        unified_inputs.append(unified_inp)
    return unified_inputs

def convert_from_unified_format(inputs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """Converts unified v1 JSON schema format to internal format."""
    internal_inputs = []
    for inp in inputs:
        internal_inp = {
            "input_number": inp['idx'],
            "pin": inp['pin'],
            "application": inp['application'],
            "application_index": inp['applicationIndex'],
            "sensor": inp['sensor'],
            "sensor_index": inp['sensorIndex']
        }
        if 'calibration' in inp:
            internal_inp['calibration'] = inp['calibration']
        internal_inputs.append(internal_inp)
    return internal_inputs

def edit_input(inp: Dict[str, Any], platform: str, used_pins: List[str], registries: Dict) -> Dict[str, Any]:
    """Interactive prompt to edit a single input."""
    print(f"\n--- Editing Input {inp['input_number']} ---")

    # Select application and sensor first (needed for pin validation)
    available_apps = [app for app in registries['applications'] if app['is_implemented']]
    app_index = prompt_for_selection(available_apps, "Applications", default=inp['application_index'])
    selected_app = next(app for app in available_apps if app['index'] == app_index)

    compatible_sensors = [s for s in registries['sensors'] if s['is_implemented'] and s['measurementType'] == selected_app['expectedMeasurementType']]
    sensor_index = prompt_for_selection(compatible_sensors, f"Compatible sensors for {selected_app['name']}", default=inp['sensor_index'])
    selected_sensor = next(s for s in compatible_sensors if s['index'] == sensor_index)

    # Now prompt for pin with sensor validation
    pin = prompt_for_pin(platform, used_pins, default=str(inp['pin']), sensor_info=selected_sensor)

    inp.update({
        "pin": pin,
        "application": selected_app['name'], "application_index": app_index,
        "sensor": selected_sensor['name'], "sensor_index": sensor_index
    })

    # Prompt for custom calibration
    custom_cal = prompt_for_custom_calibration(inp, registries)
    if custom_cal:
        inp['calibration'] = custom_cal
    elif 'calibration' in inp:
        del inp['calibration']

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
    original_timestamp = None  # Track original timestamp to preserve it if no changes made

    if args.load:
        should_add_new = False
        try:
            with open(args.load, 'r') as f:
                config_data = json.load(f)

            # Preserve the original timestamp
            original_timestamp = config_data.get("metadata", {}).get("timestamp")

            # Validate unified v1 schema
            if config_data.get("schemaVersion") != 1:
                print("Error: Only schemaVersion 1 is supported.", file=sys.stderr)
                sys.exit(1)

            if config_data.get("mode") != "static":
                print("Error: Only mode='static' configs can be loaded.", file=sys.stderr)
                sys.exit(1)

            # Validate pin type compatibility
            from openems_config.validators import validate_pin_type_compatibility
            pin_errors = validate_pin_type_compatibility(
                config_data.get("inputs", []),
                registries['sensors'],
                platform
            )
            if pin_errors:
                print("\nConfiguration errors found:", file=sys.stderr)
                for error in pin_errors:
                    print(f"  \u2717 {error}", file=sys.stderr)
                print("\nPlease fix these errors before continuing.", file=sys.stderr)
                sys.exit(1)

            # Convert from unified format to internal format
            inputs = convert_from_unified_format(config_data.get("inputs", []))
            print(f"Loaded {len(inputs)} inputs from {args.load}")

            # Track if user makes any changes
            config_modified = False

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
                        config_modified = True
                    except (ValueError, IndexError):
                        print("Invalid selection.")
                elif choice == 'a':
                    should_add_new = True
                    config_modified = True
                    break
                elif choice == 'd':
                    try:
                        idx = int(input("Enter the number of the input to delete: "))
                        inputs.pop(idx)
                        config_modified = True
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

            # Select application and sensor first (needed for pin validation)
            available_apps = [app for app in registries['applications'] if app['is_implemented']]
            app_index = prompt_for_selection(available_apps, "Applications")
            selected_app = next(app for app in available_apps if app['index'] == app_index)

            compatible_sensors = [s for s in registries['sensors'] if s['is_implemented'] and s['measurementType'] == selected_app['expectedMeasurementType']]
            sensor_index = prompt_for_selection(compatible_sensors, f"Compatible sensors for {selected_app['name']}")
            selected_sensor = next(s for s in compatible_sensors if s['index'] == sensor_index)

            # Now prompt for pin with sensor validation
            pin = prompt_for_pin(platform, used_pins, sensor_info=selected_sensor)

            new_input = {
                "input_number": len(inputs), "pin": pin,
                "application": selected_app['name'], "application_index": app_index,
                "sensor": selected_sensor['name'], "sensor_index": sensor_index
            }

            # Prompt for custom calibration
            custom_cal = prompt_for_custom_calibration(new_input, registries)
            if custom_cal:
                new_input['calibration'] = custom_cal

            inputs.append(new_input)

            if input("\nConfigure another input? [y/N]: ").strip().lower() != 'y': break

    for i, inp in enumerate(inputs):
        inp['input_number'] = i

    save_path = None

    # Determine if we should save
    if args.load:
        # Only save if changes were made
        if config_modified or should_add_new:
            save_path = args.load
    else:
        # For new configurations, ask user if they want to save
        if input("\nSave to JSON? [y/N]: ").strip().lower() == 'y':
            filename = input("File: ").strip()
            if filename:
                if not filename.endswith(".json"): filename += ".json"
                saved_configs_dir = os.path.join(os.path.dirname(__file__), 'saved-configs')
                if not os.path.exists(saved_configs_dir): os.makedirs(saved_configs_dir)
                save_path = os.path.join(saved_configs_dir, os.path.basename(filename))

    if save_path:
        with open(save_path, 'w') as f:
            # Determine timestamp: use original if loading without changes, otherwise use current time
            timestamp = original_timestamp if (original_timestamp and not config_modified and not should_add_new) else int(time.time())

            unified_config = {
                "schemaVersion": 1,
                "mode": "static",
                "metadata": {
                    "toolVersion": TOOL_VERSION,
                    "platform": platform,
                    "timestamp": timestamp
                },
                "inputs": convert_to_unified_format(inputs)
            }
            json.dump(unified_config, f, indent=2)
        print(f"\u2713 Saved to {save_path}")

    print("\nGenerating config.h...")
    config_h_path = os.path.join(args.project_dir, 'src', 'config.h')
    new_block = generate_config_block(inputs, platform, TOOL_VERSION)
    if update_config_h(config_h_path, new_block):
        print(f"\u2713 Backup: {config_h_path}.bak")
        print(f"\u2713 Generated with {len(inputs)} inputs")
    else:
        print(f"\u2717 Failed to update {config_h_path}", file=sys.stderr)

    # Generate static_calibrations.h if there are custom calibrations
    static_cal_content = generate_static_calibrations_file(inputs, TOOL_VERSION)
    static_cal_path = os.path.join(args.project_dir, 'src', 'lib', 'generated', 'static_calibrations.h')

    if static_cal_content:
        print("\nGenerating static_calibrations.h...")
        if write_static_calibrations_file(static_cal_path, static_cal_content):
            cal_count = len([i for i in inputs if 'calibration' in i and i['calibration'].get('source') == 'CUSTOM'])
            print(f"\u2713 Generated custom calibrations for {cal_count} inputs")
            print(f"\u2713 File: {static_cal_path}")
        else:
            print(f"\u2717 Failed to write {static_cal_path}", file=sys.stderr)
    else:
        # Remove the file if no custom calibrations exist
        if os.path.exists(static_cal_path):
            os.remove(static_cal_path)
            print("\u2713 Removed static_calibrations.h (no custom calibrations)")

    if args.generate_thin_libs:
        print("\nGenerating thin libraries...")
        output_dir = os.path.join(args.project_dir, 'src', 'lib', 'generated')
        generate_thin_library_files(inputs, registries['sensors'], registries['applications'], output_dir, args.project_dir)
        print(f"\u2713 Generated thin libraries in {output_dir}")

    print(f"\nReady to compile: pio run -e {platform}")

if __name__ == "__main__":
    main()
