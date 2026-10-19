"""The simulator's CAN model behaves like the firmware's `can` command."""

from __future__ import annotations

import struct
from itertools import pairwise

import pytest

from obdsniff.packets import CanFrame, StreamSplitter
from obdsniff.sim_can import (
    APB1_HZ,
    bit_timing_exists,
    format_line,
    frame_bits,
    parse_hex,
    parse_hex_bytes,
    parse_u32,
)
from obdsniff.simulator import FRAMES_PER_READ, SimulatedBoard

USAGE_ERROR = "wrong number of arguments (usage: can "


def run(board: SimulatedBoard, text: str) -> str:
    board.feed(text.encode())
    return board.take_output().decode("latin-1")


def host(board: SimulatedBoard) -> SimulatedBoard:
    run(board, "mode host\r")
    return board


def host_frames(board: SimulatedBoard, command: str) -> tuple[str, list[CanFrame]]:
    """Run a host-mode command; returns its text reply and the frames sent."""
    splitter = StreamSplitter()
    board.feed(f"{command}\r".encode())
    text = splitter.feed(board.take_output())
    while board.can.busy():
        text += splitter.feed(board.take_output())
    assert splitter.bad_packets == 0
    return text.decode(), list(splitter.frames)


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
    assert parse_hex("7df") == 0x7DF
    for bad in ("", "123456789", "7g"):
        assert parse_hex(bad) is None
    assert parse_hex_bytes("0201ff") == b"\x02\x01\xff"
    for bad in ("", "021", "001122334455667788", "zz"):
        assert parse_hex_bytes(bad) is None


def test_text_lines_match_the_firmware_format() -> None:
    std = CanFrame(timestamp_us=12_345_678, id=0x7E8, data=bytes.fromhex("04410C1AF8000000"))
    assert format_line(std) == "  12.345678  7E8       [8]  04 41 0C 1A F8 00 00 00\r\n"
    ext = CanFrame(timestamp_us=1_000_005, id=0x18DAF110, data=b"\x02\x10\x03", extended=True)
    assert format_line(ext) == "  1.000005  18DAF110  [3]  02 10 03\r\n"
    remote = CanFrame(timestamp_us=0, id=0x7DF, dlc=8, rtr=True)
    assert format_line(remote) == "  0.000000  7DF       [8]  remote request\r\n"
    assert format_line(CanFrame(timestamp_us=999_999, id=1)) == "  0.999999  001       [0]\r\n"
    assert frame_bits(ext) == frame_bits(CanFrame(timestamp_us=0, id=1, data=b"\x02\x10\x03")) + 20


# ---- the `can` command -------------------------------------------------------------


def test_starts_off(board) -> None:
    out = run(board, "can\r")
    assert "  Mode              off\r\n" in out
    assert "  Bus state         off\r\n" in out
    assert run(board, "can send 123\r").endswith("error: command failed\r\nobd> ")
    assert "error: command failed" in run(board, "can test 5\r")


def test_loopback_and_off(board) -> None:
    host(board)
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
        ("can send", USAGE_ERROR),
        ("can send 7g", "invalid argument"),
        ("can send 20000000", "invalid argument"),  # above 29 bits
        ("can send 123 0", "invalid argument"),
        ("can test", USAGE_ERROR),
        ("can test x", "invalid argument"),
        ("can test 5 y", "invalid argument"),
        ("can test 0", "invalid argument"),
        ("can test 10000001", "invalid argument"),
        ("can test 5 1000001", "invalid argument"),
        ("can bogus", "invalid argument"),
    ],
)
def test_errors(board, command: str, error: str) -> None:
    host(board)
    run(board, "can loopback\r")
    assert run(board, command + "\r").startswith("ERR " + error)


def test_send_standard_and_extended_frames(board) -> None:
    host(board)
    run(board, "can loopback\r")
    text, frames = host_frames(board, "can send 7df 0201050000000000")
    assert text == "OK\r\n"
    assert frames == [
        CanFrame(
            timestamp_us=frames[0].timestamp_us,
            id=0x7DF,
            data=bytes.fromhex("0201050000000000"),
            dlc=8,
        )
    ]
    _, (ext,) = host_frames(board, "can send 18daf110")
    assert (ext.id, ext.extended, ext.length, ext.seq) == (0x18DAF110, True, 0, 1)
    assert "can_tx_frames=2\r\n" in run(board, "can\r")


def test_test_traffic_is_numbered_and_spaced_like_a_bus(board) -> None:
    host(board)
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
    host(board)
    run(board, "can loopback\r")
    _, frames = host_frames(board, "can test 3 1000")
    assert [b.timestamp_us - a.timestamp_us for a, b in pairwise(frames)] == [1000, 1000]


def test_restarting_or_stopping_ends_a_series(board) -> None:
    host(board)
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
    host(board)
    run(board, "can loopback\r")
    run(board, "reset\r")
    board.run_pending()
    board.take_output()
    assert "Mode              off" in run(board, "can\r")


def test_interactive_lines_then_prompt_with_typed_text(board) -> None:
    run(board, "can loopback\r")
    out = run(board, "can send 123 01\r")
    assert out == "can send 123 01\r\nobd> \r\n" + out.split("\r\n")[2] + "\r\nobd> "
    assert "123       [1]  01" in out
    # typed text survives the traffic
    run(board, "led o")
    board.can.command(["send", "7df"], lambda *_: None)
    assert run(board, "").endswith("[0]\r\nobd> led o")


def test_interactive_burst_shows_one_line_per_frame(board) -> None:
    run(board, "can loopback\r")
    out = run(board, "can test 3\r")
    assert out.count("  123       [8]  ") == 3
    assert out.endswith("obd> ")


def test_switching_to_host_mode_mid_traffic_shows_no_prompt(board) -> None:
    run(board, "can loopback\r")
    board.feed(b"can test 1000\r")
    board.take_output()  # interactive lines for the first batch
    host(board)
    rest = bytearray()
    while board.can.busy():
        rest += board.take_output()
    assert b"obd> " not in rest  # the rest arrives as packets, without a prompt
