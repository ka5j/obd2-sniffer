"""Interactive session with the board from the laptop.

Like typing into the board's own console, but the laptop tool keeps the
board in host mode underneath, formats the replies, shows events as they
arrive, and handles restarts (`reset`, `crash`) by reconnecting.
"""

from __future__ import annotations

from collections.abc import Callable

from obdsniff.device import CommandError, Device, ProtocolError
from obdsniff.output import format_fields, format_reply

BANNER = "Connected. Type board commands (try 'help'); 'exit' or Ctrl-D quits."


def execute_line(device: Device, line: str) -> list[str]:
    """Run one line typed by a person and return the rows to show.

    Board commands are passed through and their replies formatted. `reset`
    and `crash <kind>` also wait for the board to restart and reconnect.
    `mode` is refused: the laptop tool keeps the board in host mode. Raises
    CommandError or ProtocolError like Device.command(). Used by the shell
    and the desktop app's console."""
    words = line.split()
    name = words[0].lower()
    if name == "mode":
        return ["The laptop tool manages the console mode itself."]
    if name == "reset":
        device.reset()
        return ["Board restarted."]
    if name == "crash" and len(words) == 2:
        device.crash(words[1])
        return [
            "Board restarted after the crash. What it recorded:",
            *format_fields(device.fault()),
        ]
    return format_reply(device.command(line))


def run_shell(
    device: Device,
    read_line: Callable[[str], str] = input,
    write: Callable[[str], None] = print,
) -> int:
    write(BANNER)
    while True:
        for event in device.poll_events():
            write(f"[event] {event}")
        try:
            line = read_line("obd> ").strip()
        except (EOFError, KeyboardInterrupt):
            write("")
            return 0
        if not line:
            continue
        if line.lower() in ("exit", "quit"):
            return 0
        try:
            for row in execute_line(device, line):
                write(row)
        except CommandError as exc:
            write(f"error: {exc.reason}")
        except ProtocolError as exc:
            write(f"error: {exc}")
