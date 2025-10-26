; PlatformIO Project Configuration File
; Build configurations for different hardware platforms

[platformio]
default_envs = teensy40

; Common settings shared across all environments
[env]
framework = arduino
monitor_speed = 115200
lib_deps = 
    adafruit/Adafruit BME280 Library@^2.2.2
    adafruit/Adafruit Unified Sensor@^1.1.9
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    sandeepmistry/CAN@^0.3.1

; Teensy 4.0 (Recommended)
[env:teensy40]
platform = teensy
board = teensy40
build_flags = 
    -D TEENSY_40
    -O2
    -Wall

; Teensy 3.6
[env:teensy36]
platform = teensy
board = teensy36
build_flags = 
    -D TEENSY_36
    -O2
    -Wall

; Arduino Mega 2560
[env:megaatmega2560]
platform = atmelavr
board = megaatmega2560
build_flags = 
    -D ARDUINO_MEGA
    -O2
    -Wall

; Arduino Uno (Limited analog inputs)
[env:uno]
platform = atmelavr
board = uno
build_flags = 
    -D ARDUINO_UNO
    -O2
    -Wall

; Arduino Due
[env:due]
platform = atmelsam
board = dueUSB
build_flags = 
    -D ARDUINO_DUE
    -O2
    -Wall

; Debug environment with verbose output
[env:debug_teensy40]
platform = teensy
board = teensy40
build_flags = 
    -D TEENSY_40
    -D DEBUG
    -Og
    -g
    -Wall
    -Wextra
build_type = debug
monitor_filters = 
    default
    time
