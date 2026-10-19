"""Byte transports: the real serial port, or anything that behaves like one."""

from __future__ import annotations

import time
from abc import ABC, abstractmethod
from collections.abc import Callable

import serial

BAUD_RATE = 1_500_000  # must match UART_LINK_BAUD in the firmware
POLL_INTERVAL = 0.001  # seconds between checks while waiting for data


class Transport(ABC):
    """A bidirectional byte stream to the board."""

    @abstractmethod
    def write(self, data: bytes) -> None:
        """Send bytes."""

    @abstractmethod
    def read(self, timeout: float) -> bytes:
        """Return the bytes available now, waiting up to `timeout` seconds for at
        least one. Returns b"" if nothing arrived in time."""

    def close(self) -> None:  # noqa: B027 - optional hook, nothing to do by default
        """Release the connection."""


class SerialTransport(Transport):
    """The board's USB serial port (ST-LINK virtual COM port).

    The port is configured once and never changed while open. pyserial
    re-applies every setting whenever one changes (setting `timeout` is
    enough), and on macOS a speed outside the classic table, such as
    1.5 Mbaud, is applied in two steps through a placeholder speed: the
    ST-LINK briefly switches to it and bytes in flight are lost. So reads
    don't use the port's timeout; they wait by polling how many bytes have
    arrived."""

    def __init__(
        self,
        port: str,
        baud_rate: int = BAUD_RATE,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self._serial = serial.Serial(port, baud_rate, timeout=0)
        self._clock = clock
        self._sleep = sleep

    def write(self, data: bytes) -> None:
        self._serial.write(data)

    def read(self, timeout: float) -> bytes:
        deadline = self._clock() + timeout
        while True:
            waiting = self._serial.in_waiting
            if waiting:
                return self._serial.read(waiting)
            remaining = deadline - self._clock()
            if remaining <= 0:
                return b""
            self._sleep(min(remaining, POLL_INTERVAL))

    def close(self) -> None:
        self._serial.close()
