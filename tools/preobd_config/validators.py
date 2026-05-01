"""
Validation functions for checking the integrity of registry data.
"""
import re
from typing import List, Dict, Any, Tuple

from .registry_parser import djb2_hash

def validate_hash_collisions(
    sensors: List[Dict[str, Any]],
    apps: List[Dict[str, Any]],
    units: List[Dict[str, Any]]
) -> List[str]:
    """
    Checks for hash collisions within and between registries.
    Returns a list of error messages.
    """
    errors = []
    # Check for collisions within sensors
    hashes = {}
    for sensor in sensors:
        if not sensor.get("name"): continue
        h = sensor["nameHash"]
        if h in hashes:
            errors.append(f"Sensor hash collision: {sensor['name']} and {hashes[h]} have the same hash 0x{h:04X}")
        else:
            hashes[h] = sensor["name"]

    # Check for collisions within applications
    hashes = {}
    for app in apps:
        if not app.get("name"): continue
        h = app["nameHash"]
        if h in hashes:
            errors.append(f"Application hash collision: {app['name']} and {hashes[h]} have the same hash 0x{h:04X}")
        else:
            hashes[h] = app["name"]

    # Check for collisions within units (name and alias)
    hashes = {}
    for unit in units:
        if not unit.get("name"): continue
        h_name = unit["nameHash"]
        h_alias = unit["aliasHash"]
        if h_name in hashes:
            errors.append(f"Unit hash collision: {unit['name']} and {hashes[h_name]} have the same hash 0x{h_name:04X}")
        else:
            hashes[h_name] = unit["name"]
        if h_alias in hashes and h_alias != h_name:
            errors.append(f"Unit alias hash collision: {unit['alias']} and {hashes[h_alias]} have the same hash 0x{h_alias:04X}")
        else:
            hashes[h_alias] = unit["alias"]

    return errors

def validate_index_references(
    apps: List[Dict[str, Any]],
    sensors: List[Dict[str, Any]]
) -> Tuple[List[str], List[str]]:
    """
    Checks that defaultSensor in applications points to a valid, implemented sensor.
    Returns a tuple of (errors, warnings).
    """
    errors = []
    warnings = []
    sensor_indices = {s['index'] for s in sensors}

    for app in apps:
        if not app.get("is_implemented"): continue

        sensor_index = app.get("defaultSensor")
        if sensor_index not in sensor_indices:
            errors.append(f"APP[{app['index']}]:{app['name']} references non-existent SENSOR[{sensor_index}]")
        else:
            sensor = next(s for s in sensors if s['index'] == sensor_index)
            if not sensor['is_implemented']:
                warnings.append(f"APP[{app['index']}]:{app['name']} references unimplemented SENSOR[{sensor_index}]")

    return errors, warnings

def validate_measurement_types(
    apps: List[Dict[str, Any]],
    sensors: List[Dict[str, Any]]
) -> List[str]:
    """
    Checks that the default sensor for an application has a matching measurement type.
    Returns a list of error messages.
    """
    errors = []
    for app in apps:
        if not app.get("is_implemented"): continue

        sensor_index = app.get("defaultSensor")
        sensor = next((s for s in sensors if s['index'] == sensor_index), None)
        if sensor and sensor['is_implemented']:
            if app['expectedMeasurementType'] != sensor['measurementType']:
                errors.append(
                    f"Measurement type mismatch in APP[{app['index']}]:{app['name']}. "
                    f"Expected {app['expectedMeasurementType']}, but SENSOR[{sensor_index}] provides {sensor['measurementType']}"
                )
    return errors

def validate_hash_algorithm(
    sensors: List[Dict[str, Any]],
    apps: List[Dict[str, Any]],
    units: List[Dict[str, Any]]
) -> List[str]:
    """
    Re-computes hashes and verifies they match the stored values.
    Returns a list of error messages.
    """
    errors = []
    for sensor in sensors:
        if sensor.get("name"):
            expected_hash = djb2_hash(sensor["name"])
            if expected_hash != sensor["nameHash"]:
                errors.append(f"Hash mismatch for SENSOR[{sensor['index']}]:{sensor['name']}. Expected 0x{expected_hash:04X}, found 0x{sensor['nameHash']:04X}")

    for app in apps:
        if app.get("name"):
            expected_hash = djb2_hash(app["name"])
            if expected_hash != app["nameHash"]:
                errors.append(f"Hash mismatch for APP[{app['index']}]:{app['name']}. Expected 0x{expected_hash:04X}, found 0x{app['nameHash']:04X}")

    for unit in units:
        if unit.get("name"):
            expected_hash = djb2_hash(unit["name"])
            if expected_hash != unit["nameHash"]:
                errors.append(f"Hash mismatch for UNIT[{unit['index']}]:{unit['name']}. Expected 0x{expected_hash:04X}, found 0x{unit['nameHash']:04X}")
        if unit.get("alias"):
            expected_hash = djb2_hash(unit["alias"])
            if expected_hash != unit["aliasHash"]:
                errors.append(f"Alias hash mismatch for UNIT[{unit['index']}]:{unit['alias']}. Expected 0x{expected_hash:04X}, found 0x{unit['aliasHash']:04X}")

    return errors

