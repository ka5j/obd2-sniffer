"""CAN in the laptop tool: presentation helpers, `can test`, the CLI and the shell."""

from __future__ import annotations

import itertools
import json
import struct

import pytest

import obdsniff.cli as cli
from obdsniff.cantest import LoopbackReport, analyse, describe, run_test
from obdsniff.device import CommandError, Device
from obdsniff.packets import (
    CanFrame,
    FrameGaps,
    TimestampUnwrapper,
    candump_line,
    format_frame,
    frame_to_json,
)
from obdsniff.shell import run_shell
from obdsniff.simulator import SimulatedBoard, SimulatedTransport


@pytest.fixture
def sim(monkeypatch) -> SimulatedBoard:
    """A simulated board that the CLI connects to."""
    board = SimulatedBoard()
    board.take_output()

    def fake_open(port: str | None) -> Device:
        dev = Device(SimulatedTransport(board), timeout=0.5)
        dev.connect()
        return dev

    monkeypatch.setattr(cli, "open_device", fake_open)
    return board


def connected(board: SimulatedBoard) -> Device:
    dev = Device(SimulatedTransport(board), timeout=0.5)
    dev.connect()
    return dev


def numbered_frame(n: int, ts: int = 0, seq: int | None = None) -> CanFrame:
    data = struct.pack("<II", n, ~n & 0xFFFF_FFFF)
    return CanFrame(timestamp_us=ts, id=0x123, data=data, dlc=8, seq=n if seq is None else seq)


# ---- presenting frames -------------------------------------------------------------


def test_timestamps_keep_increasing_across_the_counter_wrap() -> None:
    clock = TimestampUnwrapper()
    assert clock.seconds(0xFFFF_FFF0) == pytest.approx(4294.967280)
    assert clock.seconds(0x10) == pytest.approx(4294.967312)  # wrapped
    assert clock.seconds(0x20) == pytest.approx(4294.967328)


def test_formats() -> None:
    frame = CanFrame(timestamp_us=0, id=0x7E8, data=b"\x04\x41", dlc=2, seq=7)
    assert format_frame(frame, 12.345678) == "  12.345678  7E8       [2]  04 41"
    assert candump_line(frame, 1700000000.5) == "(1700000000.500000) can0 7E8#0441"
    ext_remote = CanFrame(timestamp_us=0, id=0x18DAF110, dlc=8, extended=True, rtr=True)
    assert candump_line(ext_remote, 1.0) == "(1.000000) can0 18DAF110#R8"
    assert frame_to_json(frame, 1.25) == {
        "time": 1.25,
        "id": "7E8",
        "extended": False,
        "rtr": False,
        "dlc": 2,
        "data": "0441",
        "seq": 7,
    }


def test_gaps_count_missing_frames_but_not_a_restart() -> None:
    gaps = FrameGaps()
    for seq in (65534, 65535, 0, 3, 4, 1):  # wrap, two missing, then a restart (4 -> 1)
        gaps.add(seq)
    assert gaps.lost == 2


# ---- checking a test series ---------------------------------------------------------


def test_analyse_perfect_series() -> None:
    frames = [numbered_frame(n, ts=1000 + 250 * n) for n in range(5)]
    report = analyse(frames, 5, damaged=0)
    assert report.passed
    assert report.lost == 0
    assert report.frames_per_second == pytest.approx(4000)
    assert report.spacing_us == (250, 250, 250)


def test_analyse_finds_every_problem() -> None:
    bad_inverse = CanFrame(timestamp_us=3, id=0x123, data=struct.pack("<II", 3, 0), dlc=8)
    short = CanFrame(timestamp_us=4, id=0x123, data=b"\x04", dlc=1)
    frames = [numbered_frame(0, 1), numbered_frame(2, 2), bad_inverse, short, numbered_frame(1, 5)]
    report = analyse(frames, 6, damaged=1)
    assert (report.received, report.lost, report.wrong, report.damaged) == (5, 1, 3, 1)
    assert not report.passed
    assert analyse([], 3, 0).spacing_us is None


def test_describe() -> None:
    good = analyse([numbered_frame(n, ts=250 * n) for n in range(3)], 3, 0)
    text = "\n".join(describe(good, "500 kbit/s", 0))
    assert "3 frames at 500 kbit/s, as fast as the bus allows" in text
    assert "none lost" in text
    assert text.endswith("Passed.")
    bad = LoopbackReport(
        requested=3, received=1, wrong=1, damaged=0, frames_per_second=0, spacing_us=None
    )
    text = "\n".join(describe(bad, "250 kbit/s", 100))
    assert "100 per second" in text
    assert "2 lost" in text
    assert "1 wrong" in text
    assert "Spacing" not in text
    assert text.endswith("FAILED.")


def test_run_test_against_the_simulator(sim) -> None:
    dev = connected(sim)
    dev.can_loopback()
    dev.can_send(0x7DF)  # earlier traffic: not part of the test
    report = run_test(dev, 450)
    assert report.passed
    assert report.received == 450


def test_run_test_starts_loopback_and_gives_up_when_frames_stop(sim, monkeypatch) -> None:
    dev = connected(sim)
    monkeypatch.setattr(Device, "can_test", lambda self, count, per_second=0: None)
    ticks = itertools.count(step=1.0)  # each check is a second later
    report = run_test(dev, 10, kbps=250, clock=lambda: next(ticks))
    assert dev.can_status()["can_bitrate"] == "250000"
    assert report.received == 0
    assert report.lost == 10


# ---- device and command line --------------------------------------------------------


def test_device_can_methods(sim) -> None:
    dev = connected(sim)
    dev.can_loopback(250)
    dev.can_send(0x18DAF110, b"\x02\x10\x03")
    (frame,) = dev.poll_frames(0.2)
    assert (frame.id, frame.extended, frame.data) == (0x18DAF110, True, b"\x02\x10\x03")
    dev.can_test(3, 1000)
    assert len(dev.poll_frames(0.2)) == 3
    dev.can_off()
    assert dev.can_status()["can_mode"] == "off"
    with pytest.raises(CommandError, match="command failed"):
        dev.can_send(0x123)


def run(capsys, *args: str) -> tuple[int, str, str]:
    code = cli.main(list(args))
    out, err = capsys.readouterr()
    return code, out, err


def test_cli_can_commands(capsys, sim) -> None:
    assert run(capsys, "can", "loopback", "--bitrate", "250") == (
        0,
        "CAN in loopback at 250 kbit/s.\n",
        "",
    )
    code, out, _ = run(capsys, "can")
    assert "Bit rate (bit/s)" in out
    assert "250000" in out
    assert run(capsys, "can", "send", "7df", "0201")[1] == "Frame sent.\n"
    assert json.loads(run(capsys, "--json", "can", "off")[1]) == {"done": "off"}
    code, _, err = run(capsys, "can", "send", "123")
    assert code == cli.EXIT_COMMAND_FAILED
    assert "command failed" in err


def test_cli_rejects_bad_hex(capsys) -> None:
    for args in (["can", "send", "7zz"], ["can", "send", "7df", "0g"]):
        with pytest.raises(SystemExit):
            cli.main(args)
        assert "hex" in capsys.readouterr().err


def test_cli_can_test(capsys, sim) -> None:
    code, out, _ = run(capsys, "can", "test", "--count", "300")
    assert code == 0
    assert "CAN loopback test: 300 frames at 500 kbit/s" in out
    assert out.strip().endswith("Passed.")
    code, out, _ = run(capsys, "--json", "can", "test", "--count", "50", "--rate", "1000")
    result = json.loads(out)
    assert result["passed"] is True
    assert result["received"] == 50


def test_cli_can_test_failure_exit_code(capsys, sim, monkeypatch) -> None:
    failed = LoopbackReport(
        requested=5, received=4, wrong=0, damaged=0, frames_per_second=0, spacing_us=None
    )
    monkeypatch.setattr(cli, "run_test", lambda *args: failed)
    code, out, _ = run(capsys, "can", "test")
    assert code == cli.EXIT_COMMAND_FAILED
    assert out.strip().endswith("FAILED.")


def test_monitor_shows_frames_and_writes_a_log(capsys, sim, tmp_path) -> None:
    sim.mode = "host"
    sim.can.command(["loopback"], lambda *_: None)
    sim.can.command(["test", "3"], lambda *_: None)
    sim.press_button()
    log = tmp_path / "capture.log"
    code, out, err = run(capsys, "monitor", "--duration", "0.3", "--log", str(log))
    assert code == 0
    assert out.count("  123       [8]  ") == 3
    assert "] button pressed" in out
    assert "3 frames, 0 lost, 0 damaged; saved to" in err
    lines = log.read_text().splitlines()
    assert len(lines) == 3
    assert lines[0].endswith(" can0 123#00000000FFFFFFFF")


def test_monitor_json_frames(capsys, sim) -> None:
    sim.mode = "host"
    sim.can.command(["loopback"], lambda *_: None)
    sim.can.command(["send", "7df", "02"], lambda *_: None)
    _, out, err = run(capsys, "--json", "monitor", "--duration", "0.2")
    (frame,) = [json.loads(line) for line in out.splitlines()]
    assert (frame["id"], frame["data"], frame["seq"]) == ("7DF", "02", 0)
    assert "1 frames, 0 lost" in err


# ---- shell ------------------------------------------------------------------------------


def shell_output(dev: Device, lines: list[str]) -> list[str]:
    inputs = iter(lines)
    output: list[str] = []
    run_shell(dev, read_line=lambda prompt: next(inputs), write=output.append)
    return output


def test_shell_shows_frames_between_commands(sim) -> None:
    dev = connected(sim)
    output = shell_output(dev, ["can loopback", "can send 7df 02", "", "can test 25", "", "exit"])
    text = "\n".join(output)
    assert "  7DF       [1]  02" in text
    assert text.count("  123       [8]  ") == 20
    assert "... and 5 more frames" in text
