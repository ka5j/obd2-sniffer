from __future__ import annotations

import pytest

from obdsniff.packets import (
    MAX_BODY,
    CanFrame,
    PacketError,
    StreamSplitter,
    cobs_decode,
    cobs_encode,
    crc16,
    decode_packet,
    encode_can_frame,
)

# The same golden vectors as the firmware's tests (tests/link/test_packet.c): if
# either side changes the format, both suites fail.
GOLDEN_STANDARD = bytes.fromhex(
    "00 0A 01 02 01 78 56 34 12 E8 07 01 07 08 04 41 0C 1A F8 01 01 03 3D 84 00"
)
GOLDEN_EXTENDED_REMOTE = bytes.fromhex("00 04 01 FF FF 01 01 01 08 10 F1 DA D8 08 F3 01 00")

STANDARD = CanFrame(
    timestamp_us=0x12345678, id=0x7E8, data=bytes.fromhex("04410C1AF8000000"), dlc=8, seq=0x0102
)
EXTENDED_REMOTE = CanFrame(
    timestamp_us=0, id=0x18DAF110, dlc=8, extended=True, rtr=True, seq=0xFFFF
)


def body(packet: bytes) -> bytes:
    """The bytes between a packet's two zeros."""
    return packet[1:-1]


# ---- CRC and COBS -------------------------------------------------------------


def test_crc_check_value() -> None:
    assert crc16(b"123456789") == 0x29B1


@pytest.mark.parametrize(
    ("raw", "encoded"),
    [
        (b"", b"\x01"),
        (b"\x00", b"\x01\x01"),
        (b"\x00\x00", b"\x01\x01\x01"),
        (b"\x11\x22\x00\x33", b"\x03\x11\x22\x02\x33"),
        (b"\x11\x22\x33\x44", b"\x05\x11\x22\x33\x44"),
        (b"\x11\x00\x00\x00", b"\x02\x11\x01\x01\x01"),
        (bytes(range(1, 255)), b"\xff" + bytes(range(1, 255))),
        (bytes(range(0, 255)), b"\x01\xff" + bytes(range(1, 255))),
        (bytes(range(1, 256)), b"\xff" + bytes(range(1, 255)) + b"\x02\xff"),
    ],
)
def test_cobs_reference_examples(raw: bytes, encoded: bytes) -> None:
    assert cobs_encode(raw) == encoded
    assert cobs_decode(encoded) == raw


@pytest.mark.parametrize(
    ("data", "reason"),
    [
        (b"", "empty"),
        (b"\x00", "zero byte"),
        (b"\x04\x11", "truncated"),
        (b"\x03\x11\x00", "truncated"),
    ],
)
def test_cobs_rejects_invalid_input(data: bytes, reason: str) -> None:
    with pytest.raises(PacketError, match=reason):
        cobs_decode(data)


# ---- frame packets ------------------------------------------------------------


def test_golden_vectors_decode() -> None:
    assert decode_packet(body(GOLDEN_STANDARD)) == STANDARD
    frame = decode_packet(body(GOLDEN_EXTENDED_REMOTE))
    assert frame == EXTENDED_REMOTE
    assert frame.length == 8
    assert frame.data == b""


def test_golden_vectors_encode() -> None:
    assert encode_can_frame(STANDARD) == GOLDEN_STANDARD
    assert encode_can_frame(EXTENDED_REMOTE) == GOLDEN_EXTENDED_REMOTE


def test_dlc_defaults_to_data_length() -> None:
    frame = CanFrame(timestamp_us=1, id=0x123, data=b"\x01\x02")
    assert frame.length == 2
    assert decode_packet(body(encode_can_frame(frame))).dlc == 2


def corrupt(packet_body: bytes, index: int) -> bytes:
    raw = bytearray(cobs_decode(packet_body))
    raw[index] ^= 0xFF
    return cobs_encode(bytes(raw))


def with_crc(raw: bytes) -> bytes:
    return cobs_encode(raw + crc16(raw).to_bytes(2, "little"))


@pytest.mark.parametrize(
    ("packet_body", "reason"),
    [
        (cobs_encode(b"\x01\x02\x03"), "too short"),
        (corrupt(body(GOLDEN_STANDARD), 13), "CRC mismatch"),
        (with_crc(bytes([0x02]) + bytes(11)), "unknown packet type"),
        (with_crc(bytes([0x01]) + bytes(10) + bytes([9])), "doesn't match its DLC"),
        (with_crc(bytes([0x01]) + bytes(10) + bytes([2]) + b"\x01"), "doesn't match its DLC"),
    ],
)
def test_decode_rejects_bad_packets(packet_body: bytes, reason: str) -> None:
    with pytest.raises(PacketError, match=reason):
        decode_packet(packet_body)


# ---- splitting text from packets ------------------------------------------------


def test_text_and_packets_are_separated() -> None:
    splitter = StreamSplitter()
    stream = (
        b"OK\r\n" + GOLDEN_STANDARD + b"!button pressed\r\n" + GOLDEN_EXTENDED_REMOTE + b"OK\r\n"
    )
    assert splitter.feed(stream) == b"OK\r\n!button pressed\r\nOK\r\n"
    assert list(splitter.frames) == [STANDARD, EXTENDED_REMOTE]
    assert splitter.bad_packets == 0


def test_packets_split_across_reads() -> None:
    splitter = StreamSplitter()
    stream = GOLDEN_STANDARD + b"led=on\r\n" + GOLDEN_STANDARD
    text = b"".join(splitter.feed(stream[i : i + 1]) for i in range(len(stream)))
    assert text == b"led=on\r\n"
    assert len(splitter.frames) == 2


def test_back_to_back_packets() -> None:
    splitter = StreamSplitter()
    assert splitter.feed(GOLDEN_STANDARD * 3) == b""
    assert len(splitter.frames) == 3


def test_damaged_packet_is_counted_not_passed_on_as_text() -> None:
    splitter = StreamSplitter()
    damaged = b"\x00" + corrupt(body(GOLDEN_STANDARD), 13) + b"\x00"
    assert splitter.feed(damaged + b"OK\r\n" + GOLDEN_STANDARD) == b"OK\r\n"
    assert splitter.bad_packets == 1
    assert list(splitter.frames) == [STANDARD]


def test_recovers_when_a_closing_zero_is_lost() -> None:
    # The next packet's opening zero closes the first one instead; the next
    # packet is then recognised by its non-text bytes and dropped as damaged.
    # Its first byte (0x0A, a COBS code byte) reads as a line break, which
    # the device ignores as an empty line.
    splitter = StreamSplitter()
    lost_zero = GOLDEN_STANDARD[:-1]
    stream = lost_zero + GOLDEN_STANDARD + b"version=x\r\n" + GOLDEN_STANDARD
    assert splitter.feed(stream) == b"\nversion=x\r\n"
    assert list(splitter.frames) == [STANDARD, STANDARD]
    assert splitter.bad_packets == 1


def test_recovers_when_an_opening_zero_is_lost() -> None:
    splitter = StreamSplitter()
    lost_zero = GOLDEN_STANDARD[1:]
    assert splitter.feed(b"OK\r\n" + lost_zero + b"OK\r\n") == b"OK\r\n\nOK\r\n"
    assert splitter.bad_packets == 1


def test_stray_byte_before_text_is_not_mistaken_for_a_packet() -> None:
    # e.g. a glitch on the serial line while the board restarts: the start-up
    # report and prompt must still come through.
    splitter = StreamSplitter()
    report = b"\xff\r\n=======================\r\n OBD2 CAN Sniffer\r\nobd> "
    assert splitter.feed(report) == report
    assert splitter.bad_packets == 0


def test_overlong_packet_is_treated_as_text() -> None:
    splitter = StreamSplitter()
    text = b"x" * (MAX_BODY + 5)
    assert splitter.feed(b"\x00" + text) == text  # no packet is that long
    assert splitter.feed(b"\x00" + body(GOLDEN_STANDARD) + b"\x00") == b""
    assert list(splitter.frames) == [STANDARD]


def test_incomplete_packet_waits_for_the_rest() -> None:
    splitter = StreamSplitter()
    assert splitter.feed(GOLDEN_STANDARD[:10]) == b""
    assert not splitter.frames
    assert splitter.feed(GOLDEN_STANDARD[10:]) == b""
    assert list(splitter.frames) == [STANDARD]


def test_uncollected_frames_are_bounded() -> None:
    splitter = StreamSplitter(max_pending=2)
    splitter.feed(GOLDEN_STANDARD * 3)
    assert len(splitter.frames) == 2
    assert splitter.discarded_frames == 1
