"""Byte transports: the real serial port, or anything that behaves like one."""

from __future__ import annotations

from abc import ABC, abstractmethod

import serial

BAUD_RATE = 115200


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
    """The board's USB serial port (ST-LINK virtual COM port)."""

    def __init__(self, port: str, baud_rate: int = BAUD_RATE) -> None:
        self._serial = serial.Serial(port, baud_rate, timeout=0)

    def write(self, data: bytes) -> None:
        self._serial.write(data)

    def read(self, timeout: float) -> bytes:
        self._serial.timeout = max(timeout, 0.0)
        first = self._serial.read(1)
        if not first:
            return b""
        waiting = self._serial.in_waiting
        return first + (self._serial.read(waiting) if waiting else b"")

    def close(self) -> None:
        self._serial.close()
