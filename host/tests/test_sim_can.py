"""The simulator's CAN model behaves like the firmware's `can` command."""

from __future__ import annotations

import struct
from itertools import pairwise

import pytest

from obdsniff.packets import CanFrame, StreamSplitter
from obdsniff.sim.board import FRAMES_PER_READ, SimulatedBoard
from obdsniff.sim.can import (
    APB1_HZ,
    bit_timing_exists,
    frame_bits,
    parse_u32,
)

USAGE_ERROR = "wrong number of arguments (usage: can "


def run(board: SimulatedBoard, text: str) -> str:
    board.feed(text.encode())
    return board.take_output().decode("latin-1")


def host_frames(board: SimulatedBoard, command: str) -> tuple[str, list[CanFrame]]:
    """Run a host-mode command; returns its text reply and the frames sent."""
    splitter = StreamSplitter()
    board.feed(f"{command}\r".encode())
    text = splitter.feed(board.take_output())
    while board.can.busy():
        text += splitter.feed(board.take_output())
    assert splitter.bad_packets == 0
    return text.decode(), list(splitter.frames)


def loop_frame(
    board: SimulatedBoard, ident: int, data: bytes = b"", extended: bool = False
) -> None:
    """A frame on the simulated bus, as if the board had sent it in loopback.
    No board command sends an arbitrary frame, so tests go in directly."""
    board.can._transmit(ident, data, extended=extended)


# ---- helpers that mirror the C code ---------------------------------------------


def test_bit_timing_matches_the_firmware_calculator() -> None:
    for kbps in (125, 250, 500, 1000):
        assert bit_timing_exists(APB1_HZ, kbps * 1000)
    assert not bit_timing_exists(APB1_HZ, 333_000)  # no exact divisor
    assert not bit_timing_exists(APB1_HZ, 0)
    assert not bit_timing_exists(APB1_HZ, 6_000_000)  # fewer than 8 quanta
    assert not bit_timing_exists(APB1_HZ, 1000)  # prescaler above 1024
    assert not bit_timing_exists(21_000, 1000)  # only 21 quanta: segment 1 too long


def test_number_parsers() -> None:
    assert parse_u32("500") == 500
    assert parse_u32("0x1f4") == 500
    for bad in ("", "12a", "0x", "0xg", "4294967296"):
        assert parse_u32(bad) is None


def test_extended_frames_take_longer_on_the_bus() -> None:
    ext = CanFrame(timestamp_us=1_000_005, id=0x18DAF110, data=b"\x02\x10\x03", extended=True)
    assert frame_bits(ext) == frame_bits(CanFrame(timestamp_us=0, id=1, data=b"\x02\x10\x03")) + 20


# ---- the `can` command -------------------------------------------------------------


def test_starts_off(board) -> None:
    out = run(board, "can\r")
    assert "can_mode=off\r\n" in out
    assert "can_bus=off\r\n" in out
    assert run(board, "can test 5\r") == "ERR command failed\r\n"


def test_loopback_and_off(board) -> None:
    assert run(board, "can loopback\r") == "OK\r\n"
    assert "can_bitrate=500000\r\n" in run(board, "can\r")
    assert run(board, "can loopback 250\r") == "OK\r\n"
    assert "can_bus=error-active\r\n" in run(board, "can\r")
    assert run(board, "can off\r") == "OK\r\n"
    assert "can_mode=off\r\n" in run(board, "can\r")


@pytest.mark.parametrize(
    ("command", "error"),
    [
        ("can loopback 333", "invalid argument"),
        ("can loopback 0", "invalid argument"),
        ("can loopback 2000", "invalid argument"),
        ("can loopback x", "invalid argument"),
        ("can loopback 500 1", USAGE_ERROR),
        ("can off now", USAGE_ERROR),
        ("can send 123", "invalid argument"),  # no such command: frames can't be sent by hand
        ("can test", USAGE_ERROR),
        ("can test x", "invalid argument"),
        ("can test 5 y", "invalid argument"),
        ("can test 0", "invalid argument"),
        ("can test 10000001", "invalid argument"),
        ("can test 5 1000001", "invalid argument"),
        ("can stream", USAGE_ERROR),
        ("can stream maybe", "invalid argument"),
        ("can bogus", "invalid argument"),
    ],
)
def test_errors(board, command: str, error: str) -> None:
    run(board, "can loopback\r")
    assert run(board, command + "\r").startswith("ERR " + error)


def test_streaming_off_receives_frames_without_sending_them(board) -> None:
    run(board, "can loopback\r")
    assert "can_stream=on\r\n" in run(board, "can\r")  # on after start-up
    assert run(board, "can stream off\r") == "OK\r\n"
    text, frames = host_frames(board, "can test 50")
    assert (text, frames) == ("OK\r\n", [])
    status = run(board, "can\r")
    assert "can_stream=off\r\n" in status
    assert "can_rx_frames=50\r\n" in status  # received and counted all the same
    assert "can_streamed=0\r\n" in status
    assert run(board, "can stream on\r") == "OK\r\n"
    _, frames = host_frames(board, "can test 5")
    assert len(frames) == 5


def test_standard_and_extended_frames_reach_the_laptop(board) -> None:
    run(board, "can loopback\r")
    loop_frame(board, 0x321, bytes.fromhex("0201050000000000"))
    _, frames = host_frames(board, "can stream on")
    assert frames == [
        CanFrame(
            timestamp_us=frames[0].timestamp_us,
            id=0x321,
            data=bytes.fromhex("0201050000000000"),
            dlc=8,
        )
    ]
    loop_frame(board, 0x18DAF110, extended=True)
    _, (ext,) = host_frames(board, "can stream on")
    assert (ext.id, ext.extended, ext.length, ext.seq) == (0x18DAF110, True, 0, 1)
    assert "can_tx_frames=2\r\n" in run(board, "can\r")


def test_test_traffic_is_numbered_and_spaced_like_a_bus(board) -> None:
    run(board, "can loopback 500\r")
    _, frames = host_frames(board, f"can test {FRAMES_PER_READ * 2 + 5}")
    assert len(frames) == FRAMES_PER_READ * 2 + 5
    for n, frame in enumerate(frames):
        assert (frame.id, frame.seq) == (0x123, n)
        assert struct.unpack("<II", frame.data) == (n, ~n & 0xFFFF_FFFF)
    spacing = frames[1].timestamp_us - frames[0].timestamp_us
    assert 230 <= spacing <= 250  # an 8-byte frame at 500 kbit/s, as measured on the board
    status = run(board, "can\r")
    assert "can_test_frames=405/405\r\n" in status
    assert "can_streamed=405\r\n" in status


def test_test_traffic_at_a_set_rate(board) -> None:
    run(board, "can loopback\r")
    _, frames = host_frames(board, "can test 3 1000")
    assert [b.timestamp_us - a.timestamp_us for a, b in pairwise(frames)] == [1000, 1000]


def test_restarting_or_stopping_ends_a_series(board) -> None:
    run(board, "can loopback\r")
    board.feed(b"can test 1000\r")
    board.take_output()  # the first batch only
    assert run(board, "can loopback\r").startswith("OK")  # restarts: the series stops
    host_frames(board, "can")  # drain what was already received
    assert not board.can.busy()
    assert f"can_test_frames={FRAMES_PER_READ}/1000\r\n" in run(board, "can\r")  # counts stay
    assert "can_rx_frames=0\r\n" in run(board, "can\r")  # fresh counters
    board.feed(b"can test 1000\r")
    board.take_output()
    run(board, "can off\r")
    host_frames(board, "can")
    assert not board.can.busy()


def test_restart_switches_can_off(board) -> None:
    run(board, "can loopback\r")
    run(board, "reset\r")
    board.run_pending()
    board.take_output()
    assert "can_mode=off\r\n" in run(board, "can\r")
