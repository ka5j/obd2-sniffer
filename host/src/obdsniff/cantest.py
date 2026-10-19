"""End-to-end check of CAN capture: `obdsniff can test`.

The board sends a numbered series of test frames in loopback (firmware
can_gen.c: ID 123, data = frame number and its bitwise inverse, both 32-bit
little-endian). Every frame comes back through the whole capture path:
controller, receive interrupt, queue, packet, serial link, decoder. Each one
is checked here.
"""

from __future__ import annotations

import struct
import time
from collections.abc import Callable
from dataclasses import dataclass
from itertools import pairwise

from obdsniff.device import Device
from obdsniff.packets import CanFrame, TimestampUnwrapper

TEST_ID = 0x123
IDLE_TIMEOUT = 2.0  # seconds without a new frame before giving up on the rest


@dataclass(frozen=True)
class LoopbackReport:
    requested: int
    received: int
    wrong: int  # content doesn't match its own number, or out of order
    damaged: int  # packets that failed their CRC
    frames_per_second: float  # from the board's timestamps
    spacing_us: tuple[int, int, int] | None  # min, median, max between frames

    @property
    def lost(self) -> int:
        return self.requested - self.received

    @property
    def passed(self) -> bool:
        return self.lost == 0 and self.wrong == 0 and self.damaged == 0


def analyse(frames: list[CanFrame], requested: int, damaged: int) -> LoopbackReport:
    """Check received test frames."""
    wrong = 0
    previous = -1
    for frame in frames:
        number, inverse = struct.unpack("<II", frame.data) if len(frame.data) == 8 else (0, 0)
        if len(frame.data) != 8 or inverse != (~number & 0xFFFF_FFFF) or number <= previous:
            wrong += 1
        previous = max(previous, number)

    clock = TimestampUnwrapper()
    times = [clock.seconds(f.timestamp_us) for f in frames]
    gaps = sorted(round((b - a) * 1_000_000) for a, b in pairwise(times))
    span = times[-1] - times[0] if len(times) > 1 else 0.0
    return LoopbackReport(
        requested=requested,
        received=len(frames),
        wrong=wrong,
        damaged=damaged,
        frames_per_second=(len(frames) - 1) / span if span > 0 else 0.0,
        spacing_us=(gaps[0], gaps[len(gaps) // 2], gaps[-1]) if gaps else None,
    )


def run_test(
    device: Device,
    count: int,
    per_second: int = 0,
    kbps: int = 500,
    clock: Callable[[], float] = time.monotonic,
) -> LoopbackReport:
    """Run a test series and check every frame. Starts loopback at `kbps` if
    CAN isn't in loopback already."""
    if device.can_status().get("can_mode") != "loopback":
        device.can_loopback(kbps)
    device.poll_frames()  # earlier traffic isn't part of the test
    damaged_before = device.bad_packets
    device.can_test(count, per_second)
    frames = collect_test_frames(device, count, clock)
    return analyse(frames, count, device.bad_packets - damaged_before)


def collect_test_frames(
    device: Device, count: int, clock: Callable[[], float] = time.monotonic
) -> list[CanFrame]:
    """Test frames as they arrive, until `count` have come or none has
    arrived for IDLE_TIMEOUT seconds (the rest were lost)."""
    frames: list[CanFrame] = []
    last_arrival = clock()
    while len(frames) < count:
        new = [f for f in device.poll_frames(0.05) if f.id == TEST_ID and not f.extended]
        if new:
            frames += new
            last_arrival = clock()
        elif clock() - last_arrival > IDLE_TIMEOUT:
            break
    return frames


def describe(report: LoopbackReport, kbps: str, per_second: int) -> list[str]:
    """Readable summary lines."""
    pace = f"{per_second} per second" if per_second else "as fast as the bus allows"
    lines = [
        f"CAN loopback test: {report.requested} frames at {kbps}, {pace}",
        f"  Received   {report.received} of {report.requested}, "
        + ("none lost" if report.lost == 0 else f"{report.lost} lost"),
        "  Content    "
        + ("all correct, in order" if report.wrong == 0 else f"{report.wrong} wrong"),
        f"  Damaged    {report.damaged} packets",
    ]
    if report.spacing_us is not None:
        low, median, high = report.spacing_us
        lines += [
            f"  Bus rate   {report.frames_per_second:,.0f} frames/s (board timestamps)",
            f"  Spacing    {low} / {median} / {high} us (min / median / max)",
        ]
    lines.append("Passed." if report.passed else "FAILED.")
    return lines
