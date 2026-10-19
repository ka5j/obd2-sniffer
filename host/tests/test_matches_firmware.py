"""Keep the simulator in step with the real firmware.

The simulator is a hand-written model of the board's console. If someone
changes a command, a key or a label in firmware/src/commands.c and forgets
the simulator, everything built and tested against the simulator would
quietly go out of date. These tests read the firmware source and fail if
the two disagree.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from obdsniff.simulator import COMMANDS, HELP_WIDTH, LABEL_WIDTH, LINE_MAX, SimulatedBoard
from obdsniff.transport import BAUD_RATE

REPO = Path(__file__).resolve().parents[2]
COMMANDS_C = REPO / "firmware" / "src" / "commands.c"

pytestmark = pytest.mark.skipif(
    not COMMANDS_C.exists(), reason="firmware sources not available (installed outside the repo)"
)

# {"name", "usage", "help text", min, max, handler}, possibly across lines.
TABLE_ENTRY = re.compile(
    r'\{\s*"(?P<name>\w+)",\s*"(?P<usage>[^"]*)",\s*"(?P<help>[^"]*)",\s*'
    r"(?P<min>\d+)U,\s*(?P<max>\d+)U,\s*cmd_\w+\s*\}"
)
FIELD = re.compile(r'console_fieldf\(\s*ctx,\s*"(?P<key>\w+)",\s*"(?P<label>[^"]*)"')


def firmware_source() -> str:
    return COMMANDS_C.read_text()


# Commands whose fields are printed by a helper rather than cmd_<name>().
FIELD_FUNCTIONS = {"can": "can_status"}


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
    pairs: list[tuple[str, str]] = []
    for match in FIELD.finditer(handler_body(name)):
        pair = (match["key"], match["label"])
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


@pytest.mark.parametrize("command", ["version", "status", "can"])
def test_fields_match(command: str) -> None:
    assert simulator_fields(command, SimulatedBoard()) == firmware_fields(command)


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


def test_every_firmware_label_fits_the_column() -> None:
    # A label as wide as the column gets a single space and its value ends
    # up one column right of all the others.
    too_wide = [
        m["label"] for m in FIELD.finditer(firmware_source()) if len(m["label"]) >= LABEL_WIDTH
    ]
    assert too_wide == []


def test_every_reply_fits_the_space_the_firmware_reserves() -> None:
    """The firmware starts a command only when CONSOLE_REPLY_SPACE bytes of its
    transmit buffer are free, so that the whole reply fits without waiting.
    Values on the real board (version string, compiler) can be longer than the
    simulator's, hence the margin."""
    space = c_define(REPO / "firmware" / "include" / "console.h", "CONSOLE_REPLY_SPACE")
    margin = 128
    board = SimulatedBoard()
    board.feed(b"crash bus\r")  # so that `fault` gives its longest reply
    board.run_pending()
    board.take_output()
    for mode in ("interactive", "host"):
        board.feed(f"mode {mode}\r".encode())
        board.take_output()
        for command in ("help", "version", "status", "fault", "can"):
            board.feed(f"{command}\r".encode())
            assert len(board.take_output()) + margin <= space, f"{command} ({mode})"


def test_serial_speed_matches() -> None:
    uart_link_h = REPO / "firmware" / "include" / "uart_link.h"
    assert c_define(uart_link_h, "UART_LINK_BAUD") == BAUD_RATE
