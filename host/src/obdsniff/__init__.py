"""Laptop tool for the OBD2 CAN Sniffer.

The board is driven through its serial console in host mode (see
docs/protocol.md). `Device` is the programming interface; the command line
(`obdsniff`) and the desktop app are built on it.
"""

from obdsniff.device import CommandError, Device, DeviceError, ProtocolError, open_device

__version__ = "0.3.0"

__all__ = ["CommandError", "Device", "DeviceError", "ProtocolError", "__version__", "open_device"]
