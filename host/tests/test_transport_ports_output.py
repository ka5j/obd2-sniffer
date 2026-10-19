from __future__ import annotations

from dataclasses import dataclass

import pytest

import obdsniff.transport as transport_module
from obdsniff.output import format_fields, format_reply
from obdsniff.ports import ST_VENDOR_ID, find_board, list_serial_ports
from obdsniff.transport import SerialTransport

# ---- serial transport -------------------------------------------------------


class FakeSerial:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        self.args = (port, baud, timeout)
        self.timeout = timeout
        self.incoming = bytearray()
        self.written = bytearray()
        self.closed = False

    @property
    def in_waiting(self) -> int:
        return len(self.incoming)

    def read(self, size: int) -> bytes:
        data = bytes(self.incoming[:size])
        del self.incoming[:size]
        return data

    def write(self, data: bytes) -> None:
        self.written += data

    def close(self) -> None:
        self.closed = True


@pytest.fixture
def fake_serial(monkeypatch) -> list[FakeSerial]:
    created: list[FakeSerial] = []

    def factory(*args, **kwargs) -> FakeSerial:
        port = FakeSerial(*args, **kwargs)
        created.append(port)
        return port

    monkeypatch.setattr(transport_module.serial, "Serial", factory)
    return created


def test_serial_transport(fake_serial) -> None:
    transport = SerialTransport("/dev/ttyACM0")
    port = fake_serial[0]
    assert port.args == ("/dev/ttyACM0", 115200, 0)

    transport.write(b"status\r")
    assert port.written == b"status\r"

    assert transport.read(0.1) == b""  # nothing arrived
    assert port.timeout == 0.1

    port.incoming += b"x"
    assert transport.read(-1) == b"x"  # one byte, nothing else waiting
    assert port.timeout == 0.0

    port.incoming += b"OK\r\n"
    assert transport.read(0.1) == b"OK\r\n"  # first byte plus everything waiting

    transport.close()
    assert port.closed


# ---- port detection ---------------------------------------------------------


@dataclass
class FakePort:
    device: str
    description: str | None
    vid: int | None
    pid: int | None


def test_board_ports_listed_first(monkeypatch) -> None:
    ports = [
        FakePort("/dev/ttyS0", None, None, None),
        FakePort("/dev/ttyACM1", "STM32 STLink", ST_VENDOR_ID, 0x374B),
        FakePort("/dev/ttyUSB0", "Other ST device", ST_VENDOR_ID, 0x5740),
    ]
    monkeypatch.setattr("obdsniff.ports.list_ports.comports", lambda: ports)
    listed = list_serial_ports()
    assert [p.device for p in listed] == ["/dev/ttyACM1", "/dev/ttyS0", "/dev/ttyUSB0"]
    assert [p.is_board for p in listed] == [True, False, False]
    assert listed[1].description == ""
    assert find_board() == "/dev/ttyACM1"


def test_find_board_none(monkeypatch) -> None:
    monkeypatch.setattr(
        "obdsniff.ports.list_ports.comports", lambda: [FakePort("/dev/ttyS0", "", None, None)]
    )
    assert find_board() is None


# ---- output formatting ------------------------------------------------------


def test_format_fields_aligns_labels() -> None:
    rows = format_fields({"led": "on", "custom_key": "1"})
    assert rows == ["  LED         on", "  custom_key  1"]
    assert format_fields({}) == []


def test_format_reply() -> None:
    assert format_reply(["led=on"]) == ["  LED  on"]
    assert format_reply(["  help text"]) == ["  help text"]
    assert format_reply([]) == []
