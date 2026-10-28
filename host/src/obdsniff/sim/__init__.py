"""The board simulator: `--port sim`.

A software model of the firmware, so the laptop tool, its tests and demos run
without hardware. `board` is the serial console, `can` the CAN controller in
loopback, and `obd` the simulated engine ECU and the board's OBD2 tester.
host/tests/test_matches_firmware.py keeps them in step with the C code.
"""
