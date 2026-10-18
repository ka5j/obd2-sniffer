"""Find the board's serial port."""

from __future__ import annotations

from dataclasses import dataclass

from serial.tools import list_ports

ST_VENDOR_ID = 0x0483
# ST-LINK/V2-1 (Nucleo-64 boards) and ST-LINK/V3 variants with a virtual COM port.
ST_LINK_PRODUCT_IDS = {0x374B, 0x374E, 0x374F, 0x3752, 0x3753, 0x3754}


@dataclass(frozen=True)
class PortInfo:
    device: str
    description: str
    is_board: bool


def list_serial_ports() -> list[PortInfo]:
    """All serial ports, with ST-LINK ports (a Nucleo board) flagged."""
    ports = []
    for port in list_ports.comports():
        is_board = port.vid == ST_VENDOR_ID and port.pid in ST_LINK_PRODUCT_IDS
        ports.append(PortInfo(port.device, port.description or "", is_board))
    return sorted(ports, key=lambda p: (not p.is_board, p.device))


def find_board() -> str | None:
    """The first ST-LINK serial port, or None if no board is connected."""
    for port in list_serial_ports():
        if port.is_board:
            return port.device
    return None
