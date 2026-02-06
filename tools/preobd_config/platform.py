"""
Handles platform detection, pin validation, and hardware limits.
"""
import configparser
import re
from typing import Dict, Any, Tuple, Optional, List

# Platform-specific limits
PLATFORM_LIMITS = {
    "uno": {"analog": 6, "digital": 14, "max_inputs": 8},
    "megaatmega2560": {"analog": 16, "digital": 54, "max_inputs": 16},
    "teensy36": {"analog": 25, "digital": 58, "max_inputs": 25},
    "teensy40": {"analog": 14, "digital": 40, "max_inputs": 14},
}

# Reserved pins from config.h
RESERVED_PINS = {
    2: "CAN_INT",
    3: "BUZZER",
    4: "SD_CS_PIN",
    5: "MODE_BUTTON",
    9: "CAN_CS",
}

def detect_platform(project_dir: str) -> Optional[str]:
    """
    Detects the platform from platformio.ini.
    """
    config = configparser.ConfigParser()
    try:
        config.read(f"{project_dir}/platformio.ini")
        default_env = config.get('platformio', 'default_envs', fallback='uno')

        # In platformio.ini, the board is specified in the env section
        # e.g., [env:megaatmega2560] -> board = megaatmega2560
        # We can extract it from the environment name
        if default_env.startswith('env:'):
            return default_env.split(':')[1]
        return default_env

    except (configparser.NoSectionError, configparser.NoOptionError):
        return None

def get_platform_limits(platform: str) -> Dict[str, Any]:
    """
    Returns the hardware limits for a given platform.
    """
    return PLATFORM_LIMITS.get(platform, PLATFORM_LIMITS["uno"])

def parse_pin(pin_str: str) -> Optional[Tuple[str, int]]:
    """
    Parses a pin string (e.g., "A2", "6", "I2C") into a tuple of (type, number).
    """
    pin_str = pin_str.strip().upper()
    if pin_str == "I2C":
        return ("i2c", 0)  # Return special type for I2C
    if pin_str.startswith('A'):
        try:
            pin_num = int(pin_str[1:])
            return ("analog", pin_num)
        except ValueError:
            return None
    else:
        try:
            pin_num = int(pin_str)
            return ("digital", pin_num)
        except ValueError:
            return None

def validate_pin(pin_str: str, platform: str, used_pins: List[str], sensor_pin_requirement: Optional[str] = None) -> Optional[str]:
    """
    Validates a pin against platform limits, reserved pins, used pins, and sensor requirements.
    Returns an error message string if invalid, otherwise None.

    Args:
        pin_str: Pin string (e.g., "A0", "6", "I2C")
        platform: Target platform (e.g., "uno")
        used_pins: List of already-used pins
        sensor_pin_requirement: Optional sensor pin requirement ("PIN_ANALOG", "PIN_DIGITAL", "PIN_I2C")
    """
    parsed_pin = parse_pin(pin_str)
    if not parsed_pin:
        return f"Invalid pin format: '{pin_str}'"

    pin_type, pin_num = parsed_pin
    limits = get_platform_limits(platform)

    # I2C pins bypass normal platform validation
    if pin_type != "i2c":
        # Check against platform limits
        if pin_type == "analog" and pin_num >= limits["analog"]:
            return f"Analog pin A{pin_num} exceeds platform limit of {limits['analog']-1}"
        if pin_type == "digital" and pin_num >= limits["digital"]:
            return f"Digital pin {pin_num} exceeds platform limit of {limits['digital']-1}"

        # Check against reserved pins (only for digital pins)
        if pin_type == "digital" and pin_num in RESERVED_PINS:
            return f"Pin {pin_num} is reserved for {RESERVED_PINS[pin_num]}"

        # Check for duplicates
        if pin_str.upper() in [p.upper() for p in used_pins]:
            return f"Pin {pin_str} is already in use."

    # Validate pin type compatibility with sensor requirements
    if sensor_pin_requirement:
        requirement = sensor_pin_requirement.replace("PIN_", "").lower()

        # I2C sensors must use "I2C" as pin string
        if requirement == "i2c":
            if pin_str.upper() != "I2C":
                return f"I2C sensor must use 'I2C' as pin (not {pin_str})"

        # Non-I2C sensors cannot use "I2C" as pin
        elif pin_str.upper() == "I2C":
            return f"Only I2C sensors can use 'I2C' as pin"

        # Validate digital/analog pin types
        elif requirement == "digital" and pin_type == "analog":
            return f"Sensor requires a digital pin, but {pin_str} is an analog pin"
        elif requirement == "analog" and pin_type == "digital":
            return f"Sensor requires an analog pin, but {pin_str} is a digital pin"

    return None
