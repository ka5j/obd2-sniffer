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
        words = line.split()
        name = words[0].lower()
        try:
            if name in ("exit", "quit"):
                return 0
            if name == "mode":
                write("The shell manages the console mode itself.")
            elif name == "reset":
                device.reset()
                write("Board restarted.")
            elif name == "crash" and len(words) == 2:
                device.crash(words[1])
                write("Board restarted after the crash. What it recorded:")
                for row in format_fields(device.fault()):
                    write(row)
            else:
                for row in format_reply(device.command(line)):
                    write(row)
        except CommandError as exc:
            write(f"error: {exc.reason}")
        except ProtocolError as exc:
            write(f"error: {exc}")
