"""A software model of the board's serial console.

It follows the firmware's console behaviour (lib/console/console_core.c and
firmware/console/commands.c): the same commands, keys, help text, error
messages and start-up report. It lets the laptop tool, its tests and demos
run without hardware (`--port sim`).
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass

from obdsniff.packets import encode_can_frame
from obdsniff.sim.can import USAGE as CAN_USAGE
from obdsniff.sim.can import SimulatedCan
from obdsniff.sim.obd import USAGE as OBD_USAGE
from obdsniff.transport import Transport

HELP_WIDTH = 24
LINE_MAX = 96
FIRMWARE_VERSION = "simulator"
FRAMES_PER_READ = 200  # CAN frames produced per read of the output

COMMANDS: list[tuple[str, str, str, int, int]] = [
    # name, usage, help, min args, max args (same table as the firmware)
    ("help", "[command]", "List commands, or show how to use one", 0, 1),
    ("version", "", "Firmware version and chip identity", 0, 0),
    ("status", "", "Uptime, last reset, clocks and serial statistics", 0, 0),
    ("fault", "[clear]", "Details of the last crash, or forget it", 0, 1),
    ("can", CAN_USAGE, "CAN bus: status, loopback, test traffic, frame output", 0, 3),
    ("obd", OBD_USAGE, "OBD2: latest answers, or ask the engine ECU one question", 0, 2),
    ("reset", "", "Restart the board", 0, 0),
    ("crash", "bus|usage|divzero|hard|watchdog", "Crash on purpose to test crash capture", 1, 1),
]


@dataclass
class SimulatedFault:
    """What the firmware records for each `crash` kind (illustrative values)."""

    type: str
    cause: str
    pc: int
    lr: int
    cfsr: int
    hfsr: int
    address: int | None = None


CRASHES: dict[str, SimulatedFault] = {
    "bus": SimulatedFault(
        "BusFault", "bus error on data access", 0x08000E5A, 0x08000F9D, 0x00008200, 0, 0x08080000
    ),
    "usage": SimulatedFault(
        "UsageFault", "unaligned memory access", 0x08000E70, 0x08000F9D, 0x01000000, 0
    ),
    "divzero": SimulatedFault(
        "UsageFault", "divide by zero", 0x08000E86, 0x08000F9D, 0x02000000, 0
    ),
    "hard": SimulatedFault(
        "HardFault",
        "bus error on data access",
        0x08000E5A,
        0x08000E9F,
        0x00008200,
        0x40000000,
        0x08080000,
    ),
}


class SimulatedBoard:
    """The console side of the board: bytes in, bytes out."""

    def __init__(self, clock: Callable[[], float] = time.monotonic) -> None:
        self._clock = clock
        self._output = bytearray()
        self.fault: SimulatedFault | None = None  # what `fault` reports
        # Left by a crash for the next start-up, like the firmware's record in
        # RAM that survives a reset.
        self._surviving_record: SimulatedFault | None = None
        self._fault_uptime_ms = 0
        self._after_reply: Callable[[], None] | None = None
        self.can = SimulatedCan(clock)
        self.boot("power-on")

    # ---- board life cycle ---------------------------------------------------

    def boot(self, reset_cause: str) -> None:
        """Restart: start-up report, then the "started" event.

        A crash is reported by the start-up right after it and then forgotten,
        as in the firmware (fault_init() clears the record once read): any
        later restart reports no crash."""
        self.fault, self._surviving_record = self._surviving_record, None
        self.reset_cause = reset_cause
        self._line = ""
        self._last_was_cr = False
        self._overflowed = False
        self.can.reset()  # CAN is off after every start-up
        self._boot_time = self._clock()
        self.rx_bytes = 0
        self.tx_bytes = 0
        self._emit("\r\n========================================\r\n")
        self._emit(f" OBD2 CAN Sniffer  {FIRMWARE_VERSION}\r\n")
        self._emit("----------------------------------------\r\n")
        self._emit(" MCU      STM32F446RE  rev 0x1000 (simulated)\r\n")
        self._emit(f" Reset    {reset_cause}\r\n")
        if self.fault is not None:
            self._emit(
                f" Crash    {self.fault.type} at 0x{self.fault.pc:08X}: {self.fault.cause}\r\n"
            )
            self._emit("          details: obdsniff fault\r\n")
        self._emit("========================================\r\n")
        self._event("started")

    def uptime_ms(self) -> int:
        return int((self._clock() - self._boot_time) * 1000)

    def press_button(self) -> None:
        self._event("button pressed")

    # ---- byte interface -------------------------------------------------------

    def take_output(self) -> bytes:
        self._stream_frames()
        data = bytes(self._output)
        self._output.clear()
        self.tx_bytes += len(data)
        return data

    def feed(self, data: bytes) -> None:
        for value in data:
            self.rx_bytes += 1
            self._feed_byte(value)

    # ---- CAN frames (firmware/can/can_stream.c) ---------------------------------

    def _stream_frames(self) -> None:
        """Received CAN frames, one packet each."""
        for frame in self.can.take(FRAMES_PER_READ):
            self._output += encode_can_frame(frame)

    # ---- output helpers -------------------------------------------------------

    def _emit(self, text: str) -> None:
        self._output += text.encode("ascii", errors="replace")

    def _field(self, key: str, value: str) -> None:
        self._emit(f"{key}={value}\r\n")

    def _event(self, text: str) -> None:
        self._emit(f"!{text}\r\n")

    # ---- reading lines (lib/console/line_reader.c) ------------------------------------

    def _feed_byte(self, value: int) -> None:
        after_cr = self._last_was_cr
        self._last_was_cr = value == 0x0D

        if value in (0x0D, 0x0A):
            if value == 0x0A and after_cr:
                return
            line, self._line = self._line, ""
            if self._overflowed:
                # Rejected whole: running it cut short could do something else.
                self._overflowed = False
                self._report("line too long")
                return
            self._run(line)
        elif value == 0x03:
            self._line = ""
            self._overflowed = False
        elif 0x20 <= value <= 0x7E:
            if len(self._line) >= LINE_MAX:
                self._overflowed = True
            else:
                self._line += chr(value)

    # ---- command dispatch (lib/console/cli.c, lib/console/console_core.c) ---------------------

    def _run(self, line: str) -> None:
        words = line.split()
        if not words:
            return  # an empty line gets no reply
        if len(words) > 8:
            self._report("too many arguments")
            return
        name, args = words[0].lower(), words[1:]
        entry = next((c for c in COMMANDS if c[0] == name), None)
        if entry is None:
            self._report("unknown command")
            return
        _, usage, _, min_args, max_args = entry
        if not min_args <= len(args) <= max_args:
            self._report(f"wrong number of arguments (usage: {name} {usage})")
            return
        error = getattr(self, f"_cmd_{name}")([a.lower() for a in args])
        if self._after_reply is not None and name == "crash":
            return  # the real board faults before it can reply
        self._report(error)

    def _report(self, error: str | None) -> None:
        self._emit("OK\r\n" if error is None else f"ERR {error}\r\n")

    # ---- commands (firmware/console/commands.c) -----------------------------------

    def _cmd_help(self, args: list[str]) -> str | None:
        if args:
            entry = next((c for c in COMMANDS if c[0] == args[0]), None)
            if entry is None:
                return "invalid argument"
            self._emit(f"usage: {entry[0]} {entry[1]}\r\n  {entry[2]}\r\n")
            return None
        for name, usage, text, _, _ in COMMANDS:
            used = len(name) + 1 + len(usage)
            # Too wide for the column: the text goes below, still aligned.
            gap = (
                " " * (HELP_WIDTH - used) if used < HELP_WIDTH else "\r\n" + " " * (2 + HELP_WIDTH)
            )
            self._emit(f"  {name} {usage}{gap}{text}\r\n")
        return None

    def _cmd_version(self, args: list[str]) -> str | None:
        self._field("firmware", FIRMWARE_VERSION)
        self._field("build", "debug")
        self._field("mcu", "STM32F446RE")
        self._field("revision", "0x1000")
        self._field("uid", "00000000-00000000-00000000")
        self._field("compiler", "simulated")
        return None

    def _cmd_status(self, args: list[str]) -> str | None:
        self._field("uptime_ms", str(self.uptime_ms()))
        self._field("reset_cause", self.reset_cause)
        self._field("clock", "HSE 8 MHz bypass")
        self._field("sysclk_hz", "180000000")
        self._field("watchdog_timeout_ms", "500")
        self._field("last_fault", self.fault.type if self.fault else "none")
        self._field("uart_rx_bytes", str(self.rx_bytes))
        self._field("uart_rx_dropped", "0")
        self._field("uart_rx_overruns", "0")
        self._field("uart_rx_errors", "0")
        self._field("uart_tx_bytes", str(self.tx_bytes))
        self._field("uart_tx_dropped", "0")
        self._field("uart_tx_dma_errors", "0")
        return None

    def _cmd_fault(self, args: list[str]) -> str | None:
        if args:
            if args[0] != "clear":
                return "invalid argument"
            self.fault = None
            return None
        if self.fault is None:
            self._field("fault", "none")
            return None
        f = self.fault
        self._field("fault", f.type)
        self._field("cause", f.cause)
        self._field("pc", f"0x{f.pc:08X}")
        self._field("lr", f"0x{f.lr:08X}")
        if f.address is not None:
            self._field("fault_address", f"0x{f.address:08X}")
        self._field("crash_uptime_ms", str(self._fault_uptime_ms))
        self._field("cfsr", f"0x{f.cfsr:08X}")
        self._field("hfsr", f"0x{f.hfsr:08X}")
        return None

    def _cmd_can(self, args: list[str]) -> str | None:
        return self.can.command(args, self._field)

    def _cmd_obd(self, args: list[str]) -> str | None:
        self.can.run_obd()  # answers up to now
        if not args:
            self.can.tester.fields(self.can.mode == "loopback", self._field)
            return None
        if args[0] != "ask":
            return "invalid argument"
        if len(args) != 2:
            return f"wrong number of arguments (usage: obd {OBD_USAGE})"
        return self.can.obd_ask(args[1])

    def _cmd_reset(self, args: list[str]) -> str | None:
        self._after_reply = lambda: self.boot("software")
        return None

    def _cmd_crash(self, args: list[str]) -> str | None:
        kind = args[0]
        if kind == "watchdog":
            self._after_reply = lambda: self.boot("independent watchdog")
            return None
        if kind not in CRASHES:
            return "invalid argument"
        self._fault_uptime_ms = self.uptime_ms()
        self._surviving_record = CRASHES[kind]
        self._after_reply = lambda: self.boot("software")
        return None

    def run_pending(self) -> None:
        """Carry out a restart requested by the last command (after its reply)."""
        action, self._after_reply = self._after_reply, None
        if action is not None:
            action()


class SimulatedTransport(Transport):
    """A Transport connected to a SimulatedBoard instead of a serial port."""

    def __init__(self, board: SimulatedBoard | None = None) -> None:
        self.board = board if board is not None else SimulatedBoard()

    def write(self, data: bytes) -> None:
        self.board.feed(data)

    def read(self, timeout: float) -> bytes:
        data = self.board.take_output()
        if data:
            return data
        # A restart happens after the reply has been read, like the real
        # board, which flushes its reply before resetting.
        self.board.run_pending()
        data = self.board.take_output()
        if not data and timeout > 0:
            time.sleep(min(timeout, 0.01))
        return data
