# Bench tools

| Tool | What it is |
|---|---|
| `obd_ecu_sim/` | Mega 2560 + MCP2515 firmware: an OBD-II ECU on 0x7E8, with serial knobs (silent, listen-only, answer delay, ...). |
| `ecu_sim_hold.py` | Holds the sim's serial port open (opening it resets the Mega) and passes knobs through `~/.preobd/ecu_sim_fifo`. |
| `obdlink_daemon.py` | Holds the OBDLink LX's Bluetooth port open: closing it costs a manual re-pair. Commands through `~/.preobd/obdlink_fifo`. |
| `obdlink_send.py` | Sends commands to the daemon and prints the replies; `--monitor <s>` captures traffic (ATMA). |
| `can_decode.py` | Decodes a capture against a DBC (`docs/can/preOBD.dbc`). |
| `can_application_override.sh` | #640: `SET CAN:n APPLICATION` on CAN imports (allowed, refused, Rule re-seed); reboots at the end instead of SAVE. |
| `mega_cmd.py` | As `serial_cmd.py`, for the Mega: waits out the reset that opening its port causes. |
| `serial_cmd.py` | Runs preOBD console commands over USB with a fixed time per command. |
| `relay_release.py` | #648/#649: relay actions claim before they configure (registry count), a manual-ON relay survives TEST OUTPUT, an inverted one pulses, a JSON import that moves an ON relay reports it off and releases the old pin, ACTION DEL releases; restores the baseline by import, never SAVEs. Default pins 6, 19, 18 (must be free and unwired). |
| `builtin_pins.py` | #646: `stage` (on a build with `-UBUZZER_PIN -UUSER_BUTTON_PIN`) snapshots the saved config and stores float switches on the buzzer and button pins; `check` on the build under test (pins held by the inputs, TEST OUTPUT skips the buzzer, the switches read their own pull-up); `check --hold` (an operator holds the button: boots in RUN, no long press); `restore` re-imports the snapshot, SAVEs, and checks the dump and that the buzzer chirps. SAVEs on purpose: a stored input wins only at boot. |
| `pin_claims_rgb_serial.py` | #646/#644: `pins` (SYSTEM PINS lists every registry entry once; RGB and Serial1 held as expected, also on MAX_PIN_REGISTRY stage builds), `disable` (BUS SERIAL 1 DISABLE refused on a console or framed port, nothing changes), `testout` (LED driven or "skipped: not driven"), `ble` (SYSTEM DUMP JSON over BLE matches USB: flow control still paces), `can2` (Serial1's pins 0/1 handed to CAN2 without a reboot, RPM from the sim over CAN2; reboots to restore). Never SAVEs. |
| `action_not_driven.py` | #653: on a stage build whose pin table is exactly full (`MAX_PIN_REGISTRY` = this unit's `Registry: N`), a relay added is NOT DRIVEN in ACTION LIST, refuses ON, and its BLE action frame (bleak as the central) has `overrideFlags` bit 4 set with state OFF; a BLE ON write confirms unchanged; `BUS SERIAL <n> DISABLE` frees entries and the next sync clears the bit. Deletes its actions and reboots; never SAVEs. |
| `nan_gap_hold.py` | #650: in RUN, a rule-driven relay (FAILSAFE OFF) rides out a 1 s NaN dropout (TEST INJECT NAN) without dropping, and a dead sensor reaches the failsafe after about 2 s, not at once. Default pins 19 (input), 6 (relay). Restores the baseline by import, never SAVEs. |
| `failsafe_frame.py` | #672: a FAILSAFE ON relay on a rule whose reading is driven with TEST INJECT; the BLE action frame (bleak as the central) sets `overrideFlags` bit 5 while only the failsafe holds it ON (0x20 unsettled, 0x24 dead sensor), clears it under an override, a tripped rule and in CONFIG, and agrees with ACTION LIST's `FAILSAFE` marker at each step. Default pins 19 (input), 6 (relay). Restores the baseline by import, never SAVEs. |
| `failsafe_until_trusted.py` | #669: across a reboot into RUN, a FAILSAFE ON relay is ON from the first sample and never drops on a hot start, ON (`FAILSAFE`) then OFF on a cold one, ON for good with the reading inside the hysteresis band, and a FAILSAFE OFF relay starts OFF; a jumper from the relay pin to a sense input checks the real pin level in every sample. **Needs a jumper between pins 20 and 21** (relay, sense; Serial5's pins, so Serial5 must be disabled) and pin 6 unwired (a float switch reading its own pull-up is the source). SAVEs on purpose: refuses unsaved changes, snapshots the saved config, and restores it (import, SAVE, reboot, dump compared). |
| `release_pulse_isr.py` | #652: a cleared W_PHASE RPM input lets go of its interrupt (a relay then toggled on its pin must not reach a second RPM input, read via `INFO`), and an import that releases a forced-OFF relay prints `override released`. Default pins 6, 19 (free and unwired). Restores the baseline by import, never SAVEs. |
| `elm327_ble_concurrent.py` | #616: ELM327 over BLE (bleak on `FFE1`, framed `0x30`): AT init and a 0100/0105/010B loop with every reply positive; the same loop during a `SYSTEM DUMP JSON` on Command TX (the dump equals the USB dump, ELM327 replies interleave with it); BLE action writes (ON/OFF/AUTO) on a RAM-only relay on pin 6, deleted afterwards. Needs `ELM327 on BLE` enabled and a free BLE slot. Never SAVEs. |
| `can2_responder.py` | #560: the OBD responder on CAN2 (pins 0/1, nRF off): stores Serial1 unframed and reboots, moves the OBD output to CAN2, silences the Mega sim and uses it as the tester (0x7DF and 0x7E0, PIDs 00/05/6F). SAVEs on purpose: refuses unsaved changes, snapshots, restores by import + SAVE + reboot, sets the secondary console back by command (an import never moves it, #561), compares the dump. `--abort`: `abortUnackedTx` on bus 1 instead (CAN input on CAN2; the sim's poll rate answering, listen-only (bounded, not ~3,600/s), and back; no reset). Opens the Mega's port itself: no `ecu_sim_hold.py` running. |
| `json_import_cases.py` | #567/#603: JSON IMPORT all-or-nothing cases from the unit's own dump (another board, no platform, a bad record, held/bus/serial pins, a rule without its input, a full table of other pins); each refusal must leave the dump identical; re-imports the baseline after each accepted case, never SAVEs. `--replies DIR` keeps each reply verbatim for the app fixtures. |

**Long-running holders are started by a person, in their own terminal, and left
running.** A session or script checks for them (`pgrep -f obdlink_daemon`,
`pgrep -f ecu_sim_hold`) and never starts or stops them. Their FIFOs and logs live
in `~/.preobd/`.

None of these tools sends `SAVE`. Before a test that changes the device's config,
snapshot it (`SYSTEM DUMP JSON`) and restore it afterwards.

Helpers written during a bench session belong here, not in a session's scratch
space: scratch space is gone when the session ends.
