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

from obdsniff.simulator import COMMANDS, SimulatedBoard

COMMANDS_C = Path(__file__).resolve().parents[2] / "firmware" / "src" / "commands.c"

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


def handler_body(name: str) -> str:
    """Source of the firmware's cmd_<name>() function."""
    source = firmware_source()
    start = source.index(f"static cli_status_t cmd_{name}(")
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


@pytest.mark.parametrize("command", ["version", "status"])
def test_fields_match(command: str) -> None:
    assert simulator_fields(command, SimulatedBoard()) == firmware_fields(command)


def test_fault_fields_match() -> None:
    board = SimulatedBoard()
    sim = simulator_fields("fault", board)  # no crash recorded
    board.feed(b"crash bus\r")
    board.run_pending()
    sim += [pair for pair in simulator_fields("fault", board) if pair not in sim]
    assert sim == firmware_fields("fault")
