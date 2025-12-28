"""
Parses C header files to extract registry data for sensors, applications, and units.
"""
import re
from typing import List, Dict, Any, Optional

def djb2_hash(s: str) -> int:
    """
    Computes a 16-bit, case-insensitive DJB2 hash of a string, matching the C implementation.
    """
    if not s:
        return 0
    hash_value = 5381
    for c in s.upper():  # Case-insensitive
        hash_value = ((hash_value << 5) + hash_value) + ord(c)
    return hash_value & 0xFFFF  # 16-bit output

def _parse_pstr_macros(content: str) -> Dict[str, str]:
    """Finds all PSTR string definitions and returns a lookup dictionary."""
    pstr_pattern = re.compile(r'static const char (PSTR_\w+)\[\] PROGMEM = "([^"]+)";')
    return {match.group(1): match.group(2) for match in pstr_pattern.finditer(content)}

def _extract_struct_block(content: str, struct_name: str) -> Optional[str]:
    """Extracts the full array definition for a given struct name."""
    block_pattern = re.compile(
        rf'static const PROGMEM \w+ {struct_name}\[\] = \{{(.*?)\}};\s*\n',
        re.DOTALL
    )
    match = block_pattern.search(content)
    return match.group(1) if match else None

def _parse_enum_constants(enum_header_path: str) -> Dict[str, int]:
    """
    Parses the generated registry_enums.h to extract enum constant values.
    Returns a dictionary mapping enum names to their numeric values.
    """
    enum_map = {}
    try:
        with open(enum_header_path, 'r') as f:
            content = f.read()

        # Match enum constant definitions like "SENSOR_MAX6675 = 1"
        enum_pattern = re.compile(r'(\w+)\s*=\s*(\d+)')
        for match in enum_pattern.finditer(content):
            name = match.group(1)
            value = int(match.group(2))
            enum_map[name] = value
    except FileNotFoundError:
        # If enum file doesn't exist yet, return empty map
        pass

    return enum_map

def _parse_structs(struct_content: str, pstr_macros: Dict[str, str], enum_constants: Dict[str, int] = None) -> List[Dict[str, Any]]:
    """
    Parses a block of C structs into a list of Python dictionaries.
    Also captures the raw C block for each struct.

    Args:
        struct_content: The C struct array content to parse
        pstr_macros: Dictionary mapping PSTR macro names to their string values
        enum_constants: Optional dictionary mapping enum names to their numeric values
    """
    if enum_constants is None:
        enum_constants = {}

    # This pattern captures:
    # 1. The entire struct block, including its preceding index comment.
    # 2. The index number from the comment (optional).
    # 3. The content inside the braces.
    entry_pattern = re.compile(
        r'((?:(?://|\/\*)\s*Index\s+(\d+):.*?\*\/?)?\s*\{(.+?)\s*\})',
        re.DOTALL
    )
    # This pattern extracts individual fields from within a struct
    field_pattern = re.compile(r'\.(\w+)\s*=\s*([^,]+),?')

    registries = []
    current_index = 0
    for match in entry_pattern.finditer(struct_content):
        raw_block = match.group(1)
        index_str = match.group(2)
        struct_data = match.group(3)

        if index_str:
            index = int(index_str)
        else:
            index = current_index

        item = {'index': index, 'is_implemented': True, 'raw_c_block': raw_block, 'used_pstr_macros': []}

        for field_match in field_pattern.finditer(struct_data):
            key = field_match.group(1).strip()
            value_str = field_match.group(2).strip().split('//')[0].strip()

            if value_str in pstr_macros:
                value = pstr_macros[value_str]
                item['used_pstr_macros'].append(value_str)
            elif value_str in enum_constants:
                # Resolve enum constant to its numeric value
                value = enum_constants[value_str]
            elif value_str == 'nullptr':
                value = None
            elif value_str == 'true':
                value = True
            elif value_str == 'false':
                value = False
            elif value_str.startswith('0x'):
                value = int(value_str, 16)
            else:
                try:
                    if '.' in value_str:
                        value = float(value_str)
                    else:
                        value = int(value_str)
                except ValueError:
                    value = value_str

            item[key] = value

        if item.get('name') is None:
            item['is_implemented'] = False

        registries.append(item)
        current_index += 1
    return registries

def parse_sensor_library(header_path: str) -> List[Dict[str, Any]]:
    """
    Parses sensor_library.h to extract a list of sensor dictionaries.
    """
    with open(header_path, 'r') as f:
        content = f.read()

    pstr_macros = _parse_pstr_macros(content)
    if 'PSTR_NONE' not in pstr_macros:
        pstr_macros['PSTR_NONE'] = 'NONE'

    # Load enum constants from generated header
    import os
    enum_header_path = os.path.join(os.path.dirname(header_path), 'generated', 'registry_enums.h')
    enum_constants = _parse_enum_constants(enum_header_path)

    struct_content = _extract_struct_block(content, 'SENSOR_LIBRARY')
    if not struct_content:
        return []

    return _parse_structs(struct_content, pstr_macros, enum_constants)


def parse_application_presets(header_path: str) -> List[Dict[str, Any]]:
    """
    Parses application_presets.h to extract a list of application dictionaries.
    """
    with open(header_path, 'r') as f:
        content = f.read()

    pstr_macros = _parse_pstr_macros(content)
    if 'PSTR_APP_NONE' not in pstr_macros:
        pstr_macros['PSTR_APP_NONE'] = 'NONE'

    # Load enum constants from generated header
    import os
    enum_header_path = os.path.join(os.path.dirname(header_path), 'generated', 'registry_enums.h')
    enum_constants = _parse_enum_constants(enum_header_path)

    struct_content = _extract_struct_block(content, 'APPLICATION_PRESETS')
    if not struct_content:
        return []

    return _parse_structs(struct_content, pstr_macros, enum_constants)


def parse_units_registry(header_path: str) -> List[Dict[str, Any]]:
    """
    Parses units_registry.h to extract a list of unit dictionaries.
    """
    with open(header_path, 'r') as f:
        content = f.read()

    pstr_macros = _parse_pstr_macros(content)

    # Load enum constants from generated header
    import os
    enum_header_path = os.path.join(os.path.dirname(header_path), 'generated', 'registry_enums.h')
    enum_constants = _parse_enum_constants(enum_header_path)

    struct_content = _extract_struct_block(content, 'UNITS_REGISTRY')
    if not struct_content:
        return []

    return _parse_structs(struct_content, pstr_macros, enum_constants)
