"""Talk to the board over its serial console in host mode.

Protocol summary (full description in docs/protocol.md):
  * Requests are command lines ending in CR.
  * Replies are data lines followed by a final "OK" or "ERR <reason>".
  * Data lines from version/status/fault are "key=value".
  * Lines starting with "!" are events and may arrive at any time.
  * Captured CAN frames arrive as binary packets between the lines; they are
    separated from the text as soon as they are read (see packets.py).
"""

from __future__ import annotations

import contextlib
import time
from collections import deque
from collections.abc import Callable

import serial

from obdsniff.packets import CanFrame, StreamSplitter
from obdsniff.transport import SerialTransport, Transport

PROMPT = b"obd> "
DEFAULT_TIMEOUT = 2.0  # seconds to wait for a reply
BOOT_TIMEOUT = 5.0  # seconds to wait for the board to restart
RESTORE_TIMEOUT = 0.5  # seconds to wait for the prompt when handing the console back
READ_SLICE = 0.05  # longest single blocking read, keeps loops responsive


class DeviceError(Exception):
    """Base class for errors talking to the board."""


class ProtocolError(DeviceError):
    """No reply, or a reply that doesn't follow the protocol."""


class CommandError(DeviceError):
    """The board answered "ERR <reason>"."""

    def __init__(self, command: str, reason: str) -> None:
        super().__init__(f"{command}: {reason}")
        self.command = command
        self.reason = reason


def parse_fields(lines: list[str]) -> dict[str, str]:
    """Turn "key=value" lines into a dict. Values may contain '=' and spaces."""
    fields: dict[str, str] = {}
    for line in lines:
        key, sep, value = line.partition("=")
        if not sep:
            raise ProtocolError(f"expected key=value, got {line!r}")
        fields[key] = value
    return fields


class Device:
    """A connected board. Use `open_device()` to create one."""

    def __init__(
        self,
        transport: Transport,
        timeout: float = DEFAULT_TIMEOUT,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._transport = transport
        self._timeout = timeout
        self._clock = clock
        self._buffer = bytearray()  # text only: packets are taken out on arrival
        self._splitter = StreamSplitter()
        self._events: deque[str] = deque()
        self._connected = False

    # ---- context manager -------------------------------------------------

    def __enter__(self) -> Device:
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def close(self) -> None:
        """Hand the console back to people, then release the port.

        Switches the board to interactive mode, so a serial terminal opened
        afterwards gets a prompt and echo instead of silent host mode. The
        reply comes in the new mode: the prompt, not OK. Nothing is sent if
        connect() never succeeded (it may not be the board), and a board
        that has gone away doesn't stop the port being closed."""
        try:
            if self._connected:
                self._connected = False
                with contextlib.suppress(OSError):  # port already gone, e.g. unplugged
                    self._transport.write(b"mode interactive\r")
                    self._wait_for(PROMPT, self._clock() + RESTORE_TIMEOUT)
        finally:
            self._transport.close()

    # ---- low-level reading -----------------------------------------------

    def _fill(self, deadline: float) -> bool:
        """Read more bytes into the buffer.

        Always reads at least once, without waiting if the deadline has
        passed, so a zero timeout still picks up data that has already
        arrived. Returns False when there is nothing more to wait for."""
        remaining = deadline - self._clock()
        data = self._transport.read(max(0.0, min(remaining, READ_SLICE)))
        self._buffer += self._splitter.feed(data)
        return bool(data) or remaining > 0

    def _read_line(self, deadline: float) -> str | None:
        """Next non-empty line without its line ending, or None on timeout.

        Host mode never sends an empty line; skipping them keeps a stray line
        break (for example from a damaged packet) out of a reply."""
        while True:
            end = self._buffer.find(b"\n")
            if end >= 0:
                raw = bytes(self._buffer[:end])
                del self._buffer[: end + 1]
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                if line:
                    return line
                continue
            if not self._fill(deadline):
                return None

    def _wait_for(self, marker: bytes, deadline: float) -> bool:
        """Discard input up to and including `marker`. False on timeout."""
        while True:
            index = self._buffer.find(marker)
            if index >= 0:
                del self._buffer[: index + len(marker)]
                return True
            if not self._fill(deadline):
                return False

    def _discard_stale_lines(self) -> None:
        """Drop complete lines left over from an earlier command (e.g. a reply
        that arrived after its command timed out), so they can't be taken
        as the reply to the next one. Events among them are kept."""
        while True:
            line = self._read_line(self._clock())
            if line is None:
                return
            if line.startswith("!"):
                self._events.append(line[1:])

    # ---- protocol --------------------------------------------------------

    def connect(self) -> None:
        """Bring the console into host mode, whatever state it was in.

        Ctrl-C discards anything half-typed; `mode host` switches mode. Echo
        and prompt text before the final OK come from interactive mode and
        are ignored; events ("!" lines, if the board was already in host mode)
        are kept."""
        self._buffer.clear()
        self._splitter.reset()
        self._transport.write(b"\x03\r")
        self._transport.write(b"mode host\r")
        deadline = self._clock() + self._timeout
        while True:
            line = self._read_line(deadline)
            if line is None:
                raise ProtocolError(
                    "no reply from the board: is this the right port, and is the firmware running?"
                )
            if line == "OK":
                self._connected = True
                return
            if line.startswith("!"):
                self._events.append(line[1:])

    def command(self, line: str) -> list[str]:
        """Run one console command and return its data lines.

        Raises CommandError if the board replies ERR, ProtocolError on timeout."""
        if "\r" in line or "\n" in line:
            raise ValueError("command must be a single line")
        self._discard_stale_lines()
        self._transport.write(line.encode("ascii") + b"\r")
        data: list[str] = []
        deadline = self._clock() + self._timeout
        while True:
            reply = self._read_line(deadline)
            if reply is None:
                raise ProtocolError(f"no reply to {line!r}")
            if reply.startswith("!"):
                self._events.append(reply[1:])
            elif reply == "OK":
                return data
            elif reply.startswith("ERR "):
                raise CommandError(line, reply[4:])
            else:
                data.append(reply)

    def query(self, line: str) -> dict[str, str]:
        """Run a command whose reply is key=value data."""
        return parse_fields(self.command(line))

    def version(self) -> dict[str, str]:
        return self.query("version")

    def status(self) -> dict[str, str]:
        return self.query("status")

    def fault(self) -> dict[str, str]:
        return self.query("fault")

    def clear_fault(self) -> None:
        self.command("fault clear")

    def set_led(self, mode: str) -> None:
        self.command(f"led {mode}")

    def help(self) -> list[str]:
        return self.command("help")

    def reset(self) -> None:
        """Restart the board and reconnect once it is back."""
        self.command("reset")
        # After a restart the board prints its start-up report in interactive
        # mode, ending with the prompt.
        if not self._wait_for(PROMPT, self._clock() + BOOT_TIMEOUT):
            raise ProtocolError("the board did not come back after reset")
        self.connect()

    def crash(self, kind: str) -> None:
        """Make the board crash on purpose (debug firmware only), then reconnect.

        The board faults before it can reply, restarts, and prints its
        start-up report. A release build doesn't have the command and replies
        ERR instead."""
        self._transport.write(f"crash {kind}\r".encode("ascii"))
        deadline = self._clock() + BOOT_TIMEOUT
        while PROMPT not in self._buffer:
            end = self._buffer.find(b"\n")
            if end >= 0:
                line = bytes(self._buffer[:end]).decode("utf-8", errors="replace").rstrip("\r")
                del self._buffer[: end + 1]
                if line.startswith("ERR "):
                    raise CommandError(f"crash {kind}", line[4:])
            elif not self._fill(deadline):
                raise ProtocolError("the board did not restart after the crash")
        self._wait_for(PROMPT, deadline)
        self.connect()

    def _collect(self, timeout: float) -> None:
        """Read whatever arrives within `timeout` seconds: events are kept,
        frames are kept by the splitter, other stray lines are dropped."""
        deadline = self._clock() + timeout
        while True:
            line = self._read_line(deadline)
            if line is None:
                return
            if line.startswith("!"):
                self._events.append(line[1:])

    def poll_events(self, timeout: float = 0.0) -> list[str]:
        """Events received so far, waiting up to `timeout` seconds for new ones."""
        self._collect(timeout)
        events = list(self._events)
        self._events.clear()
        return events

    def poll_frames(self, timeout: float = 0.0) -> list[CanFrame]:
        """CAN frames received so far, waiting up to `timeout` seconds for new
        ones. Events that arrive meanwhile are kept for poll_events()."""
        self._collect(timeout)
        frames = list(self._splitter.frames)
        self._splitter.frames.clear()
        return frames

    @property
    def bad_packets(self) -> int:
        """Packets that arrived damaged and were dropped (since connecting)."""
        return self._splitter.bad_packets


def open_device(port: str | None, timeout: float = DEFAULT_TIMEOUT) -> Device:
    """Connect to a board.

    port: a serial port path, "sim" for the built-in simulator, or None to
    find a connected Nucleo board automatically."""
    if port == "sim":
        from obdsniff.simulator import SimulatedTransport

        transport: Transport = SimulatedTransport()
    else:
        if port is None:
            from obdsniff.ports import find_board

            port = find_board()
            if port is None:
                raise DeviceError(
                    "no board found. Connect the Nucleo over USB, pass --port, "
                    "or use --port sim to try the simulator"
                )
        try:
            transport = SerialTransport(port)
        except serial.SerialException as exc:
            raise DeviceError(f"cannot open {port}: {exc}") from exc
    device = Device(transport, timeout=timeout)
    try:
        device.connect()
    except DeviceError:
        device.close()
        raise
    return device
