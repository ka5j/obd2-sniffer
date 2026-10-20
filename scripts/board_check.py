#!/usr/bin/env python3
"""Check a connected board: the scripted checks behind docs/design/testing.md.

Runs through the obdsniff library against the real board, or against the
simulator with --port sim, and prints one line per check with what was
measured. Exits with status 1 if any check fails.

Needs the laptop tool installed (`pip install -e host`). Close any serial
terminal first: only one program can hold the port.

Usage:
    python3 scripts/board_check.py [--port PORT] [--quick] [--crash]

--crash adds the crash-capture and watchdog checks. They restart the board
several times. A Debug build must record each crash; a Release build must
reject the command.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass

from obdsniff.cantest import analyse, collect_test_frames, run_test
from obdsniff.device import CommandError, Device, DeviceError
from obdsniff.packets import FrameGaps
from obdsniff.ports import find_board
from obdsniff.transport import BAUD_RATE, SerialTransport, Transport

SYSCLK_HZ = 180_000_000
WATCHDOG_MS = 500
# The board's clock comes from the ST-LINK's crystal (tens of ppm). The
# internal oscillator it falls back to is only trimmed to 1% (10,000 ppm).
CLOCK_PPM_LIMIT = 1000
# Sent in one write: 700 bytes, inside the board's 1 KB receive buffer. The
# replies (about 40 KB) are far more than its 4 KB transmit buffer holds, so
# this exercises the console's flow control.
BURST_COMMANDS = 100
BURST_LINE = b"status\r"
LINK_BYTES_PER_S = BAUD_RATE / 10  # 8 data bits plus start and stop bits
# An 8-byte standard frame with the most stuff bits it can have, plus the
# 3-bit gap after it. A full bus delivers at least bitrate / this many frames.
MAX_FRAME_BITS = 135
# A frame with no data and the gap after it at 1 Mbit/s: the receive
# interrupt must finish before another frame can arrive, at any bit rate.
ISR_LIMIT_US = 47
FAULT_TYPES = {
    "bus": "BusFault",
    "usage": "UsageFault",
    "divzero": "UsageFault",
    "hard": "HardFault",
}
REPLY_IDLE_TIMEOUT = 2.0  # seconds without output before giving up on burst replies


@dataclass(frozen=True)
class Result:
    area: str
    passed: bool
    detail: str


class BoardCheck:
    def __init__(self, transport: Transport, quick: bool) -> None:
        self.transport = transport
        self.on_board = isinstance(transport, SerialTransport)
        self.device = Device(transport)
        self.quick = quick
        self.results: list[Result] = []
        self.isr_max_us = 0
        self.build = ""

    def record(self, area: str, passed: bool, detail: str) -> None:
        result = Result(area, passed, detail)
        self.results.append(result)
        print(f"{'PASS' if passed else 'FAIL'}  {area:<16}{detail}", flush=True)

    def run(self, crash: bool) -> bool:
        self.device.connect()
        checks: list[Callable[[], None]] = [
            self.identity,
            self.clock_accuracy,
            self.command_burst,
            self.can_off_refuses_to_send,
            lambda: self.capture(500, 10_000 if self.quick else 100_000),
            lambda: self.capture(250, 2_000 if self.quick else 10_000),
            lambda: self.overload(5_000 if self.quick else 20_000),
            self.interrupt_time,
            self.reset,
        ]
        if crash:
            checks.append(self.crash_capture)
        try:
            for check in checks:
                try:
                    check()
                except DeviceError as exc:
                    self.record("Error", False, str(exc))
                    self.device.connect()  # a check stopped mid-way; start the next one clean
            self.device.can_off()
        finally:
            self.device.close()
        failed = sum(not r.passed for r in self.results)
        print(f"{len(self.results)} checks: " + (f"{failed} FAILED." if failed else "all passed."))
        return failed == 0

    # ---- identity and clocks ------------------------------------------------

    def identity(self) -> None:
        version = self.device.version()
        status = self.device.status()
        self.build = version["build"]
        self.record(
            "Identity",
            True,
            f"firmware {version['firmware']}, {self.build} build, "
            f"{version['mcu']} rev {version['revision']}",
        )
        clock_ok = status["clock"].startswith("HSE") and int(status["sysclk_hz"]) == SYSCLK_HZ
        self.record(
            "Clocks",
            clock_ok,
            f"{int(status['sysclk_hz']) / 1e6:g} MHz from {status['clock']}",
        )
        self.record(
            "Watchdog",
            int(status["watchdog_timeout_ms"]) == WATCHDOG_MS,
            f"{status['watchdog_timeout_ms']} ms nominal",
        )

    def board_time(self) -> tuple[float, int]:
        """The laptop's clock and the board's uptime (ms) at the same moment,
        taking the middle of the request's round trip."""
        sent = time.monotonic()
        uptime = int(self.device.status()["uptime_ms"])
        return (sent + time.monotonic()) / 2, uptime

    def clock_accuracy(self) -> None:
        seconds = 5 if self.quick else 20
        laptop_start, board_start = self.board_time()
        time.sleep(seconds)
        laptop_end, board_end = self.board_time()
        laptop = laptop_end - laptop_start
        ppm = ((board_end - board_start) / 1000 - laptop) / laptop * 1e6
        self.record(
            "Clock accuracy",
            abs(ppm) < CLOCK_PPM_LIMIT,
            f"{ppm:+.0f} ppm against the laptop over {laptop:.0f} s (limit {CLOCK_PPM_LIMIT})",
        )

    # ---- serial console -----------------------------------------------------

    def read_replies(self, count: int) -> tuple[int, int, int]:
        """Read raw output until `count` replies have ended; returns the OK and
        ERR lines seen and the bytes read."""
        buffer = bytearray()
        oks = errors = total = 0
        last_arrival = time.monotonic()
        while oks + errors < count and time.monotonic() - last_arrival < REPLY_IDLE_TIMEOUT:
            data = self.transport.read(0.05)
            if not data:
                continue
            last_arrival = time.monotonic()
            total += len(data)
            buffer += data
            *lines, rest = buffer.split(b"\n")
            buffer = bytearray(rest)
            oks += sum(line.rstrip(b"\r") == b"OK" for line in lines)
            errors += sum(line.startswith(b"ERR ") for line in lines)
        return oks, errors, total

    def command_burst(self) -> None:
        before = self.device.status()
        started = time.monotonic()
        self.transport.write(BURST_LINE * BURST_COMMANDS)
        oks, errors, total = self.read_replies(BURST_COMMANDS)
        elapsed = time.monotonic() - started
        after = self.device.status()
        counters = ("uart_rx_dropped", "uart_rx_overruns", "uart_rx_errors", "uart_tx_dropped")
        nothing_dropped = all(before[key] == after[key] for key in counters)
        no_restart = int(after["uptime_ms"]) > int(before["uptime_ms"])
        speed = f"{total / 1000:.1f} KB in {elapsed * 1000:.0f} ms"
        if self.on_board:
            speed += f" ({total / elapsed / LINK_BYTES_PER_S:.0%} of the line rate)"
        self.record(
            "Command burst",
            oks == BURST_COMMANDS and errors == 0 and nothing_dropped and no_restart,
            f"{BURST_COMMANDS} commands sent at once: {oks} replies, {errors} errors, "
            + ("nothing dropped" if nothing_dropped else "BYTES DROPPED")
            + ("" if no_restart else ", BOARD RESTARTED")
            + f"; {speed}",
        )

    # ---- CAN ----------------------------------------------------------------

    def can_off_refuses_to_send(self) -> None:
        self.device.can_off()
        refused = []
        for command in ("can send 123", "can test 1"):
            try:
                self.device.command(command)
            except CommandError:
                refused.append(command)
        self.record(
            "CAN safety",
            len(refused) == 2,
            f"with CAN off, {len(refused)} of 2 transmit commands refused",
        )

    def note_interrupt_time(self) -> None:
        self.isr_max_us = max(self.isr_max_us, int(self.device.can_status()["can_isr_max_us"]))

    def capture(self, kbps: int, count: int) -> None:
        self.device.can_loopback(kbps)
        report = run_test(self.device, count, kbps=kbps)
        self.note_interrupt_time()
        full_bus = kbps * 1000 / MAX_FRAME_BITS
        self.record(
            "CAN capture",
            report.passed and report.frames_per_second >= full_bus,
            f"{count:,} frames at {kbps} kbit/s: {report.lost} lost, {report.wrong} wrong, "
            f"{report.damaged} damaged; {report.frames_per_second:,.0f} frames/s "
            f"(a full bus gives at least {full_bus:,.0f})",
        )

    def overload(self, count: int) -> None:
        """At 1 Mbit/s a full bus outruns the serial link: frames are dropped on
        the board. Every lost frame must be accounted for, by the board's
        counters and by gaps in the capture numbers.

        A gap only shows once a later frame arrives, so frames dropped after
        the last one received leave none. The test frames are the only
        traffic and are numbered from 0, so those are counted separately."""
        self.device.can_loopback(1000)  # counters and capture numbers start at 0
        self.device.poll_frames()
        damaged_before = self.device.bad_packets
        self.device.can_test(count)
        frames = collect_test_frames(self.device, count)
        status = self.device.can_status()
        self.note_interrupt_time()
        report = analyse(frames, count, self.device.bad_packets - damaged_before)
        gaps = FrameGaps()
        for frame in frames:
            gaps.add(frame.seq)
        after_last = count - 1 - frames[-1].seq if frames else count
        dropped = int(status["can_rx_dropped"])  # queue full: numbered, then dropped
        overruns = int(status["can_rx_overruns"])  # lost in the controller: never numbered
        tail = f", plus {after_last} after the last frame received" if after_last else ""
        self.record(
            "CAN overload",
            report.wrong == 0
            and report.damaged == 0
            and gaps.lost + after_last == dropped
            and report.lost == dropped + overruns,
            f"{count:,} frames at 1000 kbit/s: {report.lost:,} lost; board counted "
            f"{dropped:,} dropped and {overruns} overruns, laptop saw {gaps.lost:,} "
            f"missing capture numbers{tail}",
        )

    def interrupt_time(self) -> None:
        self.record(
            "CAN interrupt",
            self.isr_max_us < ISR_LIMIT_US,
            f"longest receive interrupt {self.isr_max_us} us (limit {ISR_LIMIT_US})",
        )

    # ---- restarts -----------------------------------------------------------

    def reset(self) -> None:
        self.device.reset()
        cause = self.device.status()["reset_cause"]
        self.record("Reset", cause == "software", f"after `reset`: last reset {cause}")

    def crash_capture(self) -> None:
        if self.build == "release":
            try:
                self.device.crash("bus")
            except CommandError as exc:
                self.record("Crash command", True, f"absent in the Release build ({exc.reason})")
            else:
                self.record("Crash command", False, "the Release build crashed on request")
            return
        for kind, expected in FAULT_TYPES.items():
            self.device.crash(kind)
            fault = self.device.fault()
            cause = self.device.status()["reset_cause"]
            self.device.clear_fault()
            self.record(
                "Crash capture",
                fault["fault"] == expected and cause == "software",
                f"`crash {kind}`: {fault['fault']} ({fault['cause']}) at {fault['pc']}",
            )
        self.device.crash("watchdog")
        cause = self.device.status()["reset_cause"]
        self.record(
            "Watchdog",
            cause == "independent watchdog",
            f"main loop stopped: last reset {cause}",
        )


def open_transport(port: str | None) -> tuple[Transport, str]:
    if port == "sim":
        from obdsniff.simulator import SimulatedTransport

        return SimulatedTransport(), "simulator"
    port = port or find_board()
    if port is None:
        sys.exit("no board found: connect the Nucleo over USB or pass --port")
    return SerialTransport(port), port


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial port, or 'sim' (default: find the board)")
    parser.add_argument("--quick", action="store_true", help="fewer frames, shorter clock check")
    parser.add_argument("--crash", action="store_true", help="also crash the board on purpose")
    args = parser.parse_args()

    transport, name = open_transport(args.port)
    print(f"Board check on {name}" + (" (quick)" if args.quick else ""))
    return 0 if BoardCheck(transport, args.quick).run(args.crash) else 1


if __name__ == "__main__":
    sys.exit(main())
