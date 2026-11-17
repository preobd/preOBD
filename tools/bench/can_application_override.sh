#!/bin/sh
# #640 bench: SET CAN:n APPLICATION changes only a CAN import's application.
#
#   tools/bench/can_application_override.sh /dev/cu.usbmodemXXXX
#
# Needs CAN input on a bus with an OBD ECU (tools/bench/obd_ecu_sim) and a CAN
# import of PID 0x0C on CAN:0 (SET CAN 0x0C). Imports 0x70, 0x0B and 0x05 on
# the next free CAN:n pins. Sends no SAVE: SYSTEM REBOOT at the end drops every
# change (snapshot SYSTEM DUMP JSON first anyway).
PORT=${1:?usage: $0 <port>}
cd "$(dirname "$0")/../.." || exit 1
S="python3 tools/bench/serial_cmd.py $PORT"

$S 1.5 CONFIG "INFO CAN:0" "RULE LIST"
# Allowed: NONE and back; the decode stays (INFO shows CAN_IMPORT, PID 0x0C)
$S 1.5 "SET CAN:0 APPLICATION none" "SET CAN:0 APPLICATION ENGINE_RPM" "INFO CAN:0"
# Refused, nothing changes
$S 1.5 "SET CAN:0 APPLICATION OIL_PRESSURE" "SET CAN:0 SENSOR VDO_120C_STEINHART" \
       "SET CAN:0 ENGINE_RPM CAN_IMPORT" "SET CAN:20 APPLICATION CHT" "INFO CAN:0"
# Pressure reference and reference applications
$S 1.5 "SET CAN 0x70" "SET CAN 0x0B" "SET CAN 0x05" "LIST INPUTS"
echo "### use the CAN:n numbers LIST INPUTS printed above if they differ from 1-3"
$S 1.5 "SET CAN:1 APPLICATION BOOST_PRESSURE" "SET CAN:1 APPLICATION MAP_PRESSURE" \
       "SET CAN:2 APPLICATION BAROMETRIC_PRESSURE" "SET CAN:2 APPLICATION OIL_PRESSURE"
# No Rule is seeded on a change (compare with the RULE LIST above)
$S 1.5 "SET CAN:3 APPLICATION OIL_TEMP" "RULE LIST"
# An existing Rule re-seeds on a change; NONE keeps it with a warning
$S 1.5 "SET CAN:3 ALARM -40 120" "SET CAN:3 APPLICATION COOLANT_TEMP" "RULE LIST" \
       "SET CAN:3 APPLICATION none"
# Still reading the car
$S 3 RUN "LIST INPUTS"
$S 1 CONFIG
$S 3 "SYSTEM REBOOT"
