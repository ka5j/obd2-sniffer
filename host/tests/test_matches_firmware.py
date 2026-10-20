"""Keep the simulator and the laptop tool in step with the real firmware.

The simulator is a hand-written model of the board's console, and the
command line labels the board's fields itself. If someone changes a
command, a key or a label in firmware/src/commands.c and forgets the laptop
side, everything built and tested against the simulator would quietly go
out of date. These tests read the firmware source and fail if they
disagree.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from obdsniff import sim_obd
from obdsniff.output import LABELS
from obdsniff.simulator import COMMANDS, HELP_WIDTH, LABEL_WIDTH, LINE_MAX, SimulatedBoard
from obdsniff.transport import BAUD_RATE

REPO = Path(__file__).resolve().parents[2]
COMMANDS_C = REPO / "firmware" / "src" / "commands.c"
OBD2_H = REPO / "lib" / "include" / "obd2.h"
OBD2_C = REPO / "lib" / "src" / "obd2.c"
ECU_SIM_H = REPO / "lib" / "include" / "ecu_sim.h"
ECU_SIM_C = REPO / "lib" / "src" / "ecu_sim.c"

pytestmark = pytest.mark.skipif(
    not COMMANDS_C.exists(), reason="firmware sources not available (installed outside the repo)"
)

# {"name", "usage", "help text", min, max, handler}, possibly across lines.
TABLE_ENTRY = re.compile(
    r'\{\s*"(?P<name>\w+)",\s*"(?P<usage>[^"]*)",\s*"(?P<help>[^"]*)",\s*'
    r"(?P<min>\d+)U,\s*(?P<max>\d+)U,\s*cmd_\w+\s*\}"
)
FIELD = re.compile(r'console_fieldf\(\s*ctx,\s*"(?P<key>\w+)",\s*"(?P<label>[^"]*)"')
# {OBD2_PID_ENGINE_SPEED, OBD2_FORMULA_RPM, 0U, "obd_rpm", "Engine (rpm)"}
PID_ENTRY = re.compile(
    r"\{(?P<pid>OBD2_PID_\w+),\s*OBD2_FORMULA_(?P<formula>\w+),\s*(?P<decimals>\d+)U,\s*"
    r'"(?P<key>\w+)",\s*"(?P<label>[^"]*)"\}'
)


def firmware_source() -> str:
    return COMMANDS_C.read_text()


# Commands whose fields are printed by a helper rather than cmd_<name>().
FIELD_FUNCTIONS = {"can": "can_status", "obd": "obd_status"}


def c_hex_define(path: Path, name: str) -> int:
    """Value of `#define NAME 0x1FU` in a C source file."""
    match = re.search(rf"#define {name}\s+0x([0-9A-Fa-f]+)U", path.read_text())
    assert match, f"{name} not found in {path.name}"
    return int(match[1], 16)


def firmware_pids() -> list[tuple[int, str, int, str, str]]:
    """lib/src/obd2.c's PID table: (pid, formula, decimals, key, label)."""
    return [
        (
            c_hex_define(OBD2_H, m["pid"]),
            m["formula"].lower(),
            int(m["decimals"]),
            m["key"],
            m["label"],
        )
        for m in PID_ENTRY.finditer(OBD2_C.read_text())
    ]


def handler_body(name: str) -> str:
    """Source of the firmware function that prints a command's fields."""
    source = firmware_source()
    function = FIELD_FUNCTIONS.get(name, f"cmd_{name}")
    start = source.index(f"static cli_status_t {function}(")
    end = source.index("\n}\n", start)
    return source[start:end]


def firmware_fields(name: str) -> list[tuple[str, str]]:
    """(key, label) pairs printed by a firmware command, in source order,
    without repeats (e.g. `fault` prints "fault" in two branches)."""
    body = handler_body(name)
    # obd_values() prints one field per entry of lib/src/obd2.c's PID table.
    before, _, after = body.partition("obd_values(")
    found = [(m["key"], m["label"]) for m in FIELD.finditer(before)]
    if after:
        found += [(key, label) for _, _, _, key, label in firmware_pids()]
        found += [(m["key"], m["label"]) for m in FIELD.finditer(after)]
    pairs: list[tuple[str, str]] = []
    for pair in found:
        if pair not in pairs:
            pairs.append(pair)
    return pairs


def simulator_fields(command: str, board: SimulatedBoard) -> list[tuple[str, str]]:
    """(key, label) pairs the simulator prints for a command."""
    seen: list[tuple[str, str]] = []
    original = board._field

    def record(key: str, label: str, value: str) -> None:
        if (key, label) not in seen:
            seen.append((key, label))
        original(key, label, value)

    board._field = record  # type: ignore[method-assign]
    board.feed(f"{command}\r".encode())
    board._field = original  # type: ignore[method-assign]
    return seen


def test_command_table_matches() -> None:
    firmware = [
        (m["name"], m["usage"], m["help"], int(m["min"]), int(m["max"]))
        for m in TABLE_ENTRY.finditer(firmware_source())
    ]
    assert firmware, "no command table entries found in commands.c"
    assert firmware == COMMANDS


@pytest.mark.parametrize("command", ["version", "status", "can", "obd"])
def test_fields_match(command: str) -> None:
    assert simulator_fields(command, SimulatedBoard()) == firmware_fields(command)


def test_obd2_pid_table_matches() -> None:
    simulator = [(p.pid, p.formula, p.decimals, p.key, p.label) for p in sim_obd.PIDS]
    assert simulator == firmware_pids()
    assert c_define(OBD2_H, "OBD2_PID_COUNT") == len(sim_obd.PIDS)


def test_simulated_engine_matches() -> None:
    """The drive, the VIN and the trouble codes of lib/src/ecu_sim.c."""
    source = ECU_SIM_C.read_text()
    table = source[source.index("drive[] = {") : source.index("};", source.index("drive[] = {"))]
    cycle = str(c_define(ECU_SIM_H, "ECU_SIM_CYCLE_MS"))
    keyframes = [
        tuple(int(v.rstrip("U")) for v in m.groups())
        for m in re.finditer(
            r"\{(\w+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\}",
            table.replace("ECU_SIM_CYCLE_MS", cycle + "U"),
        )
    ]
    assert tuple(keyframes) == sim_obd.DRIVE
    assert int(cycle) == sim_obd.CYCLE_MS
    vin = re.search(r'#define ECU_SIM_VIN\s+"(\w+)"', ECU_SIM_H.read_text())
    assert vin and vin[1] == sim_obd.VIN
    codes = source[source.index("codes[ECU_SIM_DTC_COUNT]") :]
    assert (
        tuple(int(c, 16) for c in re.findall(r"0x([0-9A-F]{4})U", codes[: codes.index("};")]))
        == sim_obd.DTCS
    )


def test_fault_fields_match() -> None:
    board = SimulatedBoard()
    sim = simulator_fields("fault", board)  # no crash recorded
    board.feed(b"crash bus\r")
    board.run_pending()
    sim += [pair for pair in simulator_fields("fault", board) if pair not in sim]
    assert sim == firmware_fields("fault")


def c_define(path: Path, name: str) -> int:
    """Value of `#define NAME 123U` in a C source file."""
    match = re.search(rf"#define {name}\s+(\d+)U", path.read_text())
    assert match, f"{name} not found in {path.name}"
    return int(match[1])


def test_layout_and_limits_match() -> None:
    lib = REPO / "lib"
    assert c_define(lib / "include" / "console_core.h", "CONSOLE_LABEL_WIDTH") == LABEL_WIDTH
    assert c_define(lib / "src" / "console_core.c", "HELP_USAGE_WIDTH") == HELP_WIDTH
    assert c_define(lib / "include" / "line_editor.h", "LINE_EDITOR_MAX") == LINE_MAX


def all_firmware_fields() -> list[tuple[str, str]]:
    """Every (key, label) the firmware prints, including the PID table's."""
    literal = [(m["key"], m["label"]) for m in FIELD.finditer(firmware_source())]
    return literal + [(key, label) for _, _, _, key, label in firmware_pids()]


def test_laptop_labels_match_the_firmware() -> None:
    """The command line and shell show each field with the board's own label."""
    mismatched = [
        (key, label, LABELS.get(key))
        for key, label in all_firmware_fields()
        if LABELS.get(key) != label
    ]
    assert mismatched == []


def test_every_firmware_label_fits_the_column() -> None:
    # A label as wide as the column gets a single space and its value ends
    # up one column right of all the others.
    too_wide = [label for _, label in all_firmware_fields() if len(label) >= LABEL_WIDTH]
    assert too_wide == []


def test_every_reply_fits_the_space_the_firmware_reserves() -> None:
    """The firmware starts a command only when CONSOLE_REPLY_SPACE bytes of its
    transmit buffer are free, so that the whole reply fits without waiting.
    Values on the real board (version string, compiler) can be longer than the
    simulator's, hence the margin."""
    space = c_define(REPO / "firmware" / "include" / "console.h", "CONSOLE_REPLY_SPACE")
    margin = 128
    now = [1000.0]
    board = SimulatedBoard(clock=lambda: now[0])
    board.feed(b"crash bus\r")  # so that `fault` gives its longest reply
    board.run_pending()
    board.feed(b"can loopback\rcan stream off\robd start\r")  # so that `obd` has values
    now[0] += 1.0
    board.take_output()
    for mode in ("interactive", "host"):
        board.feed(f"mode {mode}\r".encode())
        board.take_output()
        for command in ("help", "version", "status", "fault", "can", "obd"):
            board.feed(f"{command}\r".encode())
            assert len(board.take_output()) + margin <= space, f"{command} ({mode})"


def test_serial_speed_matches() -> None:
    uart_link_h = REPO / "firmware" / "include" / "uart_link.h"
    assert c_define(uart_link_h, "UART_LINK_BAUD") == BAUD_RATE
