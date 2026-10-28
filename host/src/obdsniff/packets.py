"""Binary packets in the board's output (format: docs/protocol.md).

In host mode the board sends captured CAN frames as binary packets mixed into
its text replies:

    0x00 | COBS( type | payload | CRC-16 ) | 0x00

COBS removes every zero byte from the packet body and the console's text never
contains one, so zeros mark where packets start and end. This module decodes
packets (and encodes them, for the simulator) and splits the board's output
into text and packets. It mirrors lib/link/cobs.c and lib/link/packet.c; both
sides are tested against the same golden vectors.
"""

from __future__ import annotations

import re
import struct
from collections import deque
from dataclasses import dataclass

DELIMITER = 0x00
TYPE_CAN_FRAME = 0x01
ID_EXTENDED = 0x8000_0000  # id bit 31: 29-bit identifier
ID_RTR = 0x4000_0000  # id bit 30: remote frame
CAN_ID_MASK = 0x1FFF_FFFF
CAN_MAX_DLC = 8

# type, seq, timestamp, id, dlc (data and CRC follow)
_HEADER = struct.Struct("<BHIIB")
# Longest packet body on the wire: type + header + 8 data bytes + CRC, COBS-encoded.
MAX_BODY = _HEADER.size + CAN_MAX_DLC + 2 + 1
# The console only ever sends printable ASCII plus these controls: bell,
# backspace, tab, line feed, carriage return. Every packet body contains a
# byte outside that set (its type, 0x01).
# Frames kept until collected: about 25 s of a fully loaded bus. Beyond that
# the oldest are discarded (and counted), so memory stays bounded when nobody
# is reading them.
MAX_PENDING_FRAMES = 100_000
_NOT_TEXT = re.compile(rb"[^\x07-\x0a\x0d\x20-\x7e]")


class PacketError(ValueError):
    """A packet that doesn't decode: corrupted or truncated."""


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), as lib/link/crc16.c."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    """Canonical COBS encoding (no zero bytes in the result)."""
    out = bytearray()
    block = bytearray()
    for i, byte in enumerate(data):
        if byte == 0:
            out += bytes([len(block) + 1]) + block
            block.clear()
            continue
        block.append(byte)
        if len(block) == 254 and i + 1 < len(data):
            out += b"\xff" + block
            block.clear()
    out += bytes([len(block) + 1]) + block
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    """Decode a COBS packet body (without delimiters)."""
    if not data:
        raise PacketError("empty packet")
    out = bytearray()
    pos = 0
    while pos < len(data):
        code = data[pos]
        if code == 0:
            raise PacketError("zero byte inside a packet")
        block = data[pos + 1 : pos + code]
        if len(block) != code - 1 or 0 in block:
            raise PacketError("truncated packet")
        out += block
        pos += code
        if code != 0xFF and pos < len(data):
            out.append(0)
    return bytes(out)


@dataclass(frozen=True)
class CanFrame:
    """One CAN frame as received by the board."""

    timestamp_us: int  # board's microsecond counter; wraps every 71.6 minutes
    id: int
    data: bytes = b""
    dlc: int | None = None  # defaults to len(data); a remote frame has a DLC but no data
    extended: bool = False
    rtr: bool = False
    seq: int = 0  # capture number (16-bit, wraps): a gap means frames were lost

    @property
    def length(self) -> int:
        return len(self.data) if self.dlc is None else self.dlc


def encode_can_frame(frame: CanFrame) -> bytes:
    """The packet for a frame, delimiters included (as the board sends it)."""
    wire_id = frame.id | (ID_EXTENDED if frame.extended else 0) | (ID_RTR if frame.rtr else 0)
    data = b"" if frame.rtr else frame.data
    body = _HEADER.pack(TYPE_CAN_FRAME, frame.seq, frame.timestamp_us, wire_id, frame.length)
    body += data
    body += struct.pack("<H", crc16(body))
    return b"\x00" + cobs_encode(body) + b"\x00"


def decode_packet(body: bytes) -> CanFrame:
    """Decode a packet body (the bytes between the two zeros)."""
    raw = cobs_decode(body)
    if len(raw) < _HEADER.size + 2:
        raise PacketError("packet too short")
    payload, (crc,) = raw[:-2], struct.unpack("<H", raw[-2:])
    if crc16(payload) != crc:
        raise PacketError("CRC mismatch")
    kind, seq, timestamp, wire_id, dlc = _HEADER.unpack(payload[: _HEADER.size])
    if kind != TYPE_CAN_FRAME:
        raise PacketError(f"unknown packet type {kind}")
    rtr = bool(wire_id & ID_RTR)
    data = payload[_HEADER.size :]
    if dlc > CAN_MAX_DLC or len(data) != (0 if rtr else dlc):
        raise PacketError("frame length doesn't match its DLC")
    return CanFrame(
        timestamp_us=timestamp,
        id=wire_id & CAN_ID_MASK,
        data=bytes(data),
        dlc=dlc,
        extended=bool(wire_id & ID_EXTENDED),
        rtr=rtr,
        seq=seq,
    )


class StreamSplitter:
    """Separates the board's output into console text and decoded frames.

    A zero byte starts a packet and the next zero ends it. When a byte has
    been lost (for example the laptop fell behind), the splitter gets back
    in step:
      * two zeros in a row: the second one starts the next packet;
      * a byte that console text never contains, outside a packet: a packet
        whose opening zero was lost; it is collected up to its closing zero;
      * a "packet" longer than any real one: it wasn't one (a stray byte, or
        an opening zero that was really a closing one), so its bytes are
        text after all.
    A packet that doesn't decode is counted in `bad_packets` and dropped; it
    never reaches the text."""

    def __init__(self, max_pending: int = MAX_PENDING_FRAMES) -> None:
        self.frames: deque[CanFrame] = deque(maxlen=max_pending)
        self.bad_packets = 0
        self.discarded_frames = 0  # not collected in time; oldest dropped first
        self._inside = False
        self._body = bytearray()

    def reset(self) -> None:
        self._inside = False
        self._body.clear()

    def feed(self, data: bytes) -> bytes:
        """Process received bytes; returns the text among them."""
        text = bytearray()
        pos = 0
        while pos < len(data):
            zero = data.find(DELIMITER, pos)
            end = len(data) if zero < 0 else zero
            if not self._inside:
                stray = _NOT_TEXT.search(data, pos, end)
                if stray is not None:
                    # Not console text: inside a packet whose opening zero
                    # was lost.
                    text += data[pos : stray.start()]
                    pos = stray.start()
                    self._inside = True
                    continue
                text += data[pos:end]
                pos = end + 1
                self._inside = zero >= 0
                continue
            self._body += data[pos:end]
            if len(self._body) > MAX_BODY:
                text += self._body  # not a packet: we were out of step
                self.reset()
                pos = end  # the zero, if any, starts the next packet
                continue
            pos = end + 1
            if zero >= 0 and self._body:
                self._finish_packet()
        return bytes(text)

    def _finish_packet(self) -> None:
        try:
            frame = decode_packet(bytes(self._body))
        except PacketError:
            self.bad_packets += 1
        else:
            if len(self.frames) == self.frames.maxlen:
                self.discarded_frames += 1
            self.frames.append(frame)
        self.reset()


# ---- presenting frames ----------------------------------------------------------

WRAP_US = 1 << 32  # the board's microsecond counter wraps after 2^32 us (71.6 min)


class TimestampUnwrapper:
    """Turns the board's wrapping 32-bit microsecond counter into seconds
    that keep increasing, for frames seen in order."""

    def __init__(self) -> None:
        self._last: int | None = None
        self._base = 0

    def seconds(self, timestamp_us: int) -> float:
        if self._last is not None and timestamp_us < self._last - WRAP_US // 2:
            self._base += WRAP_US
        self._last = timestamp_us
        return (self._base + timestamp_us) / 1_000_000


def format_frame(frame: CanFrame, seconds: float | None = None) -> str:
    """One line, as the board prints frames in interactive mode
    (lib/link/can_text.c): time, ID, length, data."""
    if seconds is None:
        whole, micros = divmod(frame.timestamp_us, 1_000_000)
    else:
        whole, micros = divmod(round(seconds * 1_000_000), 1_000_000)
    ident = f"{frame.id:08X}" if frame.extended else f"{frame.id:03X}"
    if frame.rtr:
        tail = "  remote request"
    else:
        tail = ("  " + " ".join(f"{b:02X}" for b in frame.data)) if frame.data else ""
    return f"  {whole}.{micros:06d}  {ident:<10}[{frame.length}]{tail}"


def candump_line(frame: CanFrame, seconds: float, interface: str = "can0") -> str:
    """A line of a candump log file (Linux can-utils), which tools such as
    SavvyCAN and Wireshark can open: `(time) can0 7E8#0441...`."""
    ident = f"{frame.id:08X}" if frame.extended else f"{frame.id:03X}"
    payload = f"R{frame.length}" if frame.rtr else frame.data.hex().upper()
    return f"({seconds:.6f}) {interface} {ident}#{payload}"


def frame_to_json(frame: CanFrame, seconds: float) -> dict[str, object]:
    return {
        "time": round(seconds, 6),
        "id": f"{frame.id:X}",
        "extended": frame.extended,
        "rtr": frame.rtr,
        "dlc": frame.length,
        "data": frame.data.hex().upper(),
        "seq": frame.seq,
    }


class FrameGaps:
    """Counts frames missing from the capture numbers (16-bit, wrapping).

    A jump back by more than half the range is taken as CAN having been
    restarted (capture numbers start again at 0), not as frames lost."""

    def __init__(self) -> None:
        self._last: int | None = None
        self.lost = 0

    def add(self, seq: int) -> None:
        if self._last is not None:
            gap = (seq - self._last - 1) % 65536
            if gap < 32768:
                self.lost += gap
        self._last = seq
