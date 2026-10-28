"""The simulator's OBD2 model matches the firmware's C code (lib/obd/ecu_sim.c,
obd2.c, ecu_node.c, obd_client.c). The worked values are the ones the C
unit tests check."""

from __future__ import annotations

import pytest

from obdsniff.packets import StreamSplitter
from obdsniff.sim.board import SimulatedBoard
from obdsniff.sim.obd import (
    PID_BY_NUMBER,
    PIDS,
    SimulatedEcu,
    SimulatedTester,
    c_div,
    decode,
    dtc_text,
    ecu_reply,
    encode,
    engine_state,
    format_value,
)


def run(board: SimulatedBoard, text: str) -> str:
    board.feed(text.encode())
    return board.take_output().decode("latin-1")


def test_c_division_rounds_toward_zero() -> None:
    assert (c_div(7, 2), c_div(-7, 2), c_div(7, -2), c_div(-7, -2)) == (3, -3, -3, 3)


# ---- formats (tests/obd/test_obd2.c) --------------------------------------------------


def test_decode_examples() -> None:
    assert decode(PID_BY_NUMBER[0x0C], b"\x1a\xf8") == 1726
    assert decode(PID_BY_NUMBER[0x11], b"\x3c") == 235
    assert decode(PID_BY_NUMBER[0x05], b"\x7b") == 83
    assert decode(PID_BY_NUMBER[0x0F], b"\x00") == -40
    assert decode(PID_BY_NUMBER[0x1F], b"\x02\xf2") == 754
    assert decode(PID_BY_NUMBER[0x0D], b"\x32") == 50
    assert decode(PID_BY_NUMBER[0x0C], b"\x1a") is None


def test_encode_examples_and_limits() -> None:
    assert encode(PID_BY_NUMBER[0x0C], 1726) == b"\x1a\xf8"
    assert encode(PID_BY_NUMBER[0x0C], 20000) == b"\xff\xfc"
    assert encode(PID_BY_NUMBER[0x11], 235) == b"\x3c"
    assert encode(PID_BY_NUMBER[0x11], 1200) == b"\xff"
    assert encode(PID_BY_NUMBER[0x05], -50) == b"\x00"
    assert encode(PID_BY_NUMBER[0x1F], 70000) == b"\xff\xff"
    assert encode(PID_BY_NUMBER[0x0D], 400) == b"\xff"


def test_trouble_code_text_and_values() -> None:
    assert [dtc_text(c) for c in (0x0301, 0x2ABC, 0x4123, 0x9234, 0xC100)] == [
        "P0301",
        "P2ABC",
        "C0123",
        "B1234",
        "U0100",
    ]
    assert format_value(PID_BY_NUMBER[0x11], 235) == "23.5"
    assert format_value(PID_BY_NUMBER[0x05], -40) == "-40"


# ---- the engine (tests/obd/test_ecu_sim.c) -------------------------------------------


@pytest.mark.parametrize(
    ("elapsed_ms", "rpm", "speed", "throttle", "coolant"),
    [
        (0, 800, 0, 0, 20),
        (7000, 2000, 25, 225, 25),
        (9000, 3200, 50, 450, 27),
        (29999, 800, 0, 0, 42),
        (45000, 2325, 73, 205, 55),
        (90000, 800, 0, 0, 90),
    ],
)
def test_drive_cycle(elapsed_ms: int, rpm: int, speed: int, throttle: int, coolant: int) -> None:
    s = engine_state(elapsed_ms)
    assert (s.rpm, s.speed_kmh, s.throttle, s.coolant_c) == (rpm, speed, throttle, coolant)
    assert s.load == 180 + (throttle * 3) // 2
    assert s.intake_c == 25


def test_run_time_limit() -> None:
    assert engine_state(754_999).run_time_s == 754
    assert engine_state(0xFFFF_FFFF).run_time_s == 65535


def test_replies() -> None:
    assert ecu_reply(0, b"\x01\x00").hex() == "4100981a8002"
    assert ecu_reply(0, b"\x01\x01").hex() == "410182000000"
    assert ecu_reply(0, b"\x01\x0c").hex() == "410c0c80"
    assert ecu_reply(0, b"\x03").hex() == "430203010420"
    assert ecu_reply(0, b"\x09\x00").hex() == "490040000000"
    assert ecu_reply(0, b"\x09\x02") == b"\x49\x02\x011M8GDM9AXKP042788"
    for silent in (b"", b"\x01\x02", b"\x01", b"\x01\x0c\x0d", b"\x03\x00", b"\x09\x04", b"\x04"):
        assert ecu_reply(0, silent) == b""


def test_every_pid_reports_the_engine_state() -> None:
    s = engine_state(45000)
    for pid in PIDS:
        reply = ecu_reply(45000, bytes([0x01, pid.pid]))
        assert abs(decode(pid, reply[2:]) - s.value(pid.pid)) <= 2  # type: ignore[operator]


# ---- the two nodes ----------------------------------------------------------------------


def test_ecu_node_sends_the_vin_after_flow_control() -> None:
    sent: list[tuple[int, str]] = []

    def send(ident: int, data: bytes) -> None:
        sent.append((ident, data.hex()))

    ecu = SimulatedEcu()
    ecu.frame(0x7E8, bytes.fromhex("020902cccccccccc"), 0, send)  # not its identifier
    ecu.frame(0x7DF, bytes.fromhex("020902cccccccccc"), 0, send)
    assert sent == [(0x7E8, "1014490201314d38")]
    ecu.frame(0x7E0, bytes.fromhex("300000cccccccccc"), 0, send)
    assert sent[1:] == [(0x7E8, "2147444d3941584b"), (0x7E8, "2250303432373838")]
    ecu.frame(0x7DF, bytes.fromhex("020102cccccccccc"), 0, send)  # unsupported: silent
    ecu.frame(0x7DF, b"", 0, send)
    assert len(sent) == 3


def test_tester_keeps_what_the_c_client_keeps() -> None:
    """Replies it can't use count as errors, as obd_client.c counts them."""
    tester = SimulatedTester()
    tester.start(0)

    def reply(hex_data: str) -> None:
        tester.frame(0x7E8, bytes.fromhex(hex_data), lambda *_: None)

    reply("06410182000000cc")
    assert (tester.mil_on, tester.dtc_reported) == (True, 2)
    reply("034101820000cccc")  # monitor status one byte short
    assert tester.mil_on is None
    reply("06430203010420cc")
    assert tester.dtcs == [0x0301, 0x0420]
    reply("03430203cccccccc")  # second code cut off
    assert tester.dtcs is None
    reply("03410b64cccccccc")  # a PID it never asks for
    reply("037f0112cccccccc")  # negative reply
    reply("0141cccccccccccc")  # too short to be anything
    reply("30000000cccccccc")  # flow control: not for the tester
    assert (tester.replies, tester.errors) == (2, 5)

    def vin_reply(message: bytes) -> None:
        sent: list[bytes] = []
        tester.frame(0x7E8, bytes([0x10, len(message)]) + message[:6], lambda _, d: sent.append(d))
        assert sent == [bytes.fromhex("300000cccccccccc")]
        for n in range(6, len(message), 7):
            chunk = message[n : n + 7]
            tester.frame(0x7E8, bytes([0x21 + n // 7]) + chunk + b"\xcc" * (7 - len(chunk)), print)

    vin_reply(b"\x49\x02\x011M8GDM9AXKP042788")
    assert tester.vin == "1M8GDM9AXKP042788"
    vin_reply(b"\x49\x02\x011M8GDM9AX P042788")  # a space: not a valid VIN
    assert tester.vin is None


def test_tester_ignores_frames_until_started() -> None:
    tester = SimulatedTester()
    assert not tester.frame(0x7E8, bytes.fromhex("04410c1af8cccccc"), lambda *_: None)
    tester.start(0)
    assert not tester.frame(0x7E9, bytes.fromhex("04410c1af8cccccc"), lambda *_: None)
    assert tester.frame(0x7E8, bytes.fromhex("04410c1af8cccccc"), lambda *_: None)
    assert tester.values["obd_rpm"] == 1726
    tester.frame(0x7E8, bytes.fromhex("03410c1acccccccc"), lambda *_: None)  # too short
    assert "obd_rpm" not in tester.values  # forgotten, as obd_client.c does


# ---- the `obd` command on the simulated board ---------------------------------------------


def host_board(clock) -> SimulatedBoard:
    board = SimulatedBoard(clock=clock)
    board.take_output()
    run(board, "mode host\r")
    return board


def fields(board: SimulatedBoard) -> dict[str, str]:
    lines = run(board, "obd\r").splitlines()
    assert lines[-1] == "OK"
    return dict(line.split("=", 1) for line in lines[:-1])


def test_obd_needs_loopback(clock) -> None:
    board = host_board(clock)
    assert run(board, "obd start\r") == "ERR command failed\r\n"
    f = fields(board)
    assert (f["obd_tester"], f["obd_ecu"], f["obd_rpm"], f["obd_vin"]) == (
        "stopped",
        "off",
        "-",
        "-",
    )
    assert run(board, "obd bogus\r") == "ERR invalid argument\r\n"


def test_obd_reads_the_simulated_ecu(clock) -> None:
    board = host_board(clock)
    run(board, "can stream off\r")
    run(board, "can loopback\r")
    assert run(board, "obd start\r") == "OK\r\n"
    clock.advance(1.0)
    f = fields(board)
    assert f["obd_tester"] == "running"
    assert f["obd_ecu"] == "on"
    assert f["obd_vin"] == "1M8GDM9AXKP042788"
    assert f["obd_dtcs"] == "P0301 P0420"
    assert (f["obd_mil"], f["obd_dtc_count"]) == ("on", "2")
    assert f["obd_rpm"] == "800"
    assert f["obd_coolant"] == "20"
    assert f["obd_throttle"] == "0.0"
    assert f["obd_requests"] == f["obd_replies"]
    assert 90 <= int(f["obd_requests"]) <= 100  # one every 10 ms or so
    assert (f["obd_timeouts"], f["obd_errors"]) == ("0", "0")

    clock.advance(7.0)  # into the acceleration
    assert int(fields(board)["obd_rpm"]) > 2000
    assert run(board, "obd stop\r") == "OK\r\n"
    requests = fields(board)["obd_requests"]
    clock.advance(1.0)
    assert fields(board)["obd_requests"] == requests  # stopped
    assert fields(board)["obd_tester"] == "stopped"


def test_obd_traffic_is_captured_like_any_other(clock) -> None:
    board = host_board(clock)
    run(board, "can loopback\r")
    splitter = StreamSplitter()
    board.feed(b"obd start\r")  # the first request goes with the reply
    splitter.feed(board.take_output())
    clock.advance(0.05)
    board.feed(b"can\r")
    splitter.feed(board.take_output())
    first = [(f.id, f.data.hex()) for f in list(splitter.frames)[:6]]
    assert first == [
        (0x7DF, "020902cccccccccc"),  # the VIN first
        (0x7E8, "1014490201314d38"),
        (0x7E0, "300000cccccccccc"),
        (0x7E8, "2147444d3941584b"),
        (0x7E8, "2250303432373838"),
        (0x7DF, "0103cccccccccccc"),  # then the trouble codes
    ]
    seqs = [f.seq for f in splitter.frames]
    assert seqs == list(range(len(seqs)))


def test_device_obd_methods(clock) -> None:
    from obdsniff.device import CommandError, Device
    from obdsniff.sim.board import SimulatedTransport

    board = SimulatedBoard(clock=clock)
    board.take_output()
    dev = Device(SimulatedTransport(board), timeout=0.5)
    dev.connect()
    with pytest.raises(CommandError, match="command failed"):
        dev.obd_start()  # CAN is off
    dev.can_loopback()
    dev.obd_start()
    clock.advance(0.5)
    status = dev.obd_status()
    assert (status["obd_tester"], status["obd_vin"]) == ("running", "1M8GDM9AXKP042788")
    dev.obd_stop()
    assert dev.obd_status()["obd_tester"] == "stopped"


def test_can_off_stops_the_tester_and_a_raw_request_is_answered(clock) -> None:
    board = host_board(clock)
    run(board, "can loopback\r")
    splitter = StreamSplitter()
    board.feed(b"can send 7df 02010d\r")  # a request typed by hand
    splitter.feed(board.take_output())
    assert [(f.id, f.data[:4].hex()) for f in splitter.frames] == [
        (0x7DF, "02010d"),
        (0x7E8, "03410d00"),  # speed 0 at idle
    ]
    run(board, "obd start\r")
    run(board, "can off\r")
    assert fields(board)["obd_tester"] == "stopped"
