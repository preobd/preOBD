# Bench tools

| Tool | What it is |
|---|---|
| `obd_ecu_sim/` | Mega 2560 + MCP2515 firmware: an OBD-II ECU on 0x7E8, with serial knobs (silent, listen-only, answer delay, ...). |
| `ecu_sim_hold.py` | Holds the sim's serial port open (opening it resets the Mega) and passes knobs through `~/.preobd/ecu_sim_fifo`. |
| `obdlink_daemon.py` | Holds the OBDLink LX's Bluetooth port open: closing it costs a manual re-pair. Commands through `~/.preobd/obdlink_fifo`. |
| `obdlink_send.py` | Sends commands to the daemon and prints the replies; `--monitor <s>` captures traffic (ATMA). |
| `can_decode.py` | Decodes a capture against a DBC (`docs/can/preOBD.dbc`). |
| `can_application_override.sh` | #640: `SET CAN:n APPLICATION` on CAN imports (allowed, refused, Rule re-seed); reboots at the end instead of SAVE. |
| `serial_cmd.py` | Runs preOBD console commands over USB with a fixed time per command. |
| `json_import_cases.py` | #567/#603: JSON IMPORT all-or-nothing cases from the unit's own dump (another board, no platform, a bad record, held/bus/serial pins, a rule without its input, a full table of other pins); each refusal must leave the dump identical; re-imports the baseline after each accepted case, never SAVEs. `--replies DIR` keeps each reply verbatim for the app fixtures. |

**Long-running holders are started by a person, in their own terminal, and left
running.** A session or script checks for them (`pgrep -f obdlink_daemon`,
`pgrep -f ecu_sim_hold`) and never starts or stops them. Their FIFOs and logs live
in `~/.preobd/`.

None of these tools sends `SAVE`. Before a test that changes the device's config,
snapshot it (`SYSTEM DUMP JSON`) and restore it afterwards.

Helpers written during a bench session belong here, not in a session's scratch
space: scratch space is gone when the session ends.
