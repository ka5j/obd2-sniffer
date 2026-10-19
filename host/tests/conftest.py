from __future__ import annotations

import pytest

from obdsniff.device import Device
from obdsniff.simulator import SimulatedBoard, SimulatedTransport
from obdsniff.transport import Transport


class FakeClock:
    """Manually advanced time source."""

    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


class ScriptedTransport(Transport):
    """Replies with canned bytes: each write pops the next response."""

    def __init__(self, responses: list[bytes] | None = None) -> None:
        self.responses = list(responses or [])
        self.written: list[bytes] = []
        self.pending = bytearray()
        self.closed = False

    def write(self, data: bytes) -> None:
        self.written.append(data)
        if self.responses:
            self.pending += self.responses.pop(0)

    def read(self, timeout: float) -> bytes:
        data = bytes(self.pending)
        self.pending.clear()
        return data

    def close(self) -> None:
        self.closed = True


@pytest.fixture
def clock() -> FakeClock:
    return FakeClock()


@pytest.fixture
def board(clock: FakeClock) -> SimulatedBoard:
    sim = SimulatedBoard(clock=clock)
    sim.take_output()  # discard the start-up report
    return sim


@pytest.fixture
def device(board: SimulatedBoard) -> Device:
    dev = Device(SimulatedTransport(board), timeout=0.5)
    dev.connect()
    return dev
