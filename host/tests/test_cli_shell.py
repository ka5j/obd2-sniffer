from __future__ import annotations

import json
import runpy
import sys

import pytest

import obdsniff.cli as cli
from obdsniff.device import CommandError, Device, ProtocolError
from obdsniff.ports import PortInfo
from obdsniff.shell import run_shell
from obdsniff.sim.board import SimulatedBoard, SimulatedTransport


def run_cli(capsys, *args: str) -> tuple[int, str, str]:
    code = cli.main(list(args))
    out, err = capsys.readouterr()
    return code, out, err


@pytest.fixture
def sim_board(monkeypatch) -> SimulatedBoard:
    """Make `--port sim` use a board the test can see and control."""
    board = SimulatedBoard()
    board.take_output()  # discard the start-up report

    def fake_open(port: str | None) -> Device:
        dev = Device(SimulatedTransport(board), timeout=0.5)
        dev.connect()
        return dev

    monkeypatch.setattr(cli, "open_device", fake_open)
    return board


# ---- one-shot commands --------------------------------------------------------


def test_info(capsys) -> None:
    code, out, _ = run_cli(capsys, "--port", "sim", "info")
    assert code == 0
    assert out.startswith("Board\n  Firmware")
    assert "\nStatus\n  Uptime (ms)" in out


def test_info_json(capsys) -> None:
    _, out, _ = run_cli(capsys, "--port", "sim", "--json", "info")
    data = json.loads(out)
    assert data["version"]["mcu"] == "STM32F446RE"
    assert data["status"]["last_fault"] == "none"


@pytest.mark.parametrize("command", ["version", "status"])
def test_version_and_status(capsys, command) -> None:
    code, out, _ = run_cli(capsys, "--port", "sim", command)
    assert code == 0
    assert out.startswith("  ")
    code, out, _ = run_cli(capsys, "--port", "sim", "--json", command)
    assert isinstance(json.loads(out), dict)


def test_led(capsys, sim_board) -> None:
    code, out, _ = run_cli(capsys, "led", "off")
    assert (code, out) == (0, "LED set to off.\n")
    assert sim_board.led == "off"
    code, out, _ = run_cli(capsys, "--json", "led", "on")
    assert json.loads(out) == {"led": "on"}


def test_crash_then_fault_then_clear(capsys, sim_board) -> None:
    code, out, _ = run_cli(capsys, "crash", "bus")
    assert code == 0
    assert "What it recorded:" in out
    assert "Bad address" in out
    code, out, _ = run_cli(capsys, "--json", "fault")
    assert json.loads(out)["fault"] == "BusFault"
    code, out, _ = run_cli(capsys, "fault")
    assert "BusFault" in out
    code, out, _ = run_cli(capsys, "fault", "--clear")
    assert out == "Crash record cleared.\n"
    code, out, _ = run_cli(capsys, "--json", "fault", "--clear")
    assert json.loads(out) == {"cleared": True}


def test_reset(capsys, sim_board) -> None:
    code, out, _ = run_cli(capsys, "reset")
    assert (code, out) == (0, "Board restarted.\n")
    assert sim_board.reset_cause == "software"


# ---- monitor ------------------------------------------------------------------


def test_monitor_prints_events(capsys, sim_board) -> None:
    sim_board.mode = "host"
    sim_board.press_button()  # queued before the monitor starts reading
    code, out, _ = run_cli(capsys, "monitor", "--duration", "0.3")
    assert code == 0
    assert out.startswith("Watching for CAN frames and events.")
    assert "] button pressed" in out


def test_monitor_json(capsys, sim_board) -> None:
    sim_board.mode = "host"
    sim_board.press_button()
    _, out, _ = run_cli(capsys, "--json", "monitor", "--duration", "0.3")
    events = [json.loads(line) for line in out.splitlines()]
    assert events[0]["event"] == "button pressed"


def test_monitor_stops_on_ctrl_c(capsys, monkeypatch, sim_board) -> None:
    def interrupted(self, timeout: float = 0.0) -> list[str]:
        raise KeyboardInterrupt

    monkeypatch.setattr(Device, "poll_events", interrupted)
    code, _, _ = run_cli(capsys, "monitor")
    assert code == 0


# ---- ports and errors -----------------------------------------------------------


def test_ports(capsys, monkeypatch) -> None:
    ports = [PortInfo("/dev/ttyACM0", "STLink", True), PortInfo("/dev/ttyS0", "Serial", False)]
    monkeypatch.setattr(cli, "list_serial_ports", lambda: ports)
    code, out, _ = run_cli(capsys, "ports")
    assert code == 0
    assert out == "/dev/ttyACM0  STLink  <- Nucleo board\n/dev/ttyS0  Serial\n"
    _, out, _ = run_cli(capsys, "--json", "ports")
    assert json.loads(out)[0] == {
        "device": "/dev/ttyACM0",
        "description": "STLink",
        "is_board": True,
    }
    monkeypatch.setattr(cli, "list_serial_ports", lambda: [])
    _, out, _ = run_cli(capsys, "ports")
    assert out == "No serial ports found.\n"


def test_connection_error_exit_code(capsys, monkeypatch) -> None:
    monkeypatch.setattr("obdsniff.ports.find_board", lambda: None)
    code, _, err = run_cli(capsys, "status")
    assert code == cli.EXIT_NO_CONNECTION
    assert err.startswith("error: no board found")


def test_command_error_exit_code(capsys, monkeypatch, sim_board) -> None:
    def refuse(self, mode: str) -> None:
        raise CommandError(f"led {mode}", "invalid argument")

    monkeypatch.setattr(Device, "set_led", refuse)
    code, _, err = run_cli(capsys, "led", "on")
    assert (code, err) == (cli.EXIT_COMMAND_FAILED, "error: invalid argument\n")


def test_shell_command_uses_run_shell(capsys, monkeypatch, sim_board) -> None:
    monkeypatch.setattr(cli, "run_shell", lambda device: 7)
    assert run_cli(capsys, "shell")[0] == 7


def test_module_entry_point(monkeypatch, capsys) -> None:
    monkeypatch.setattr(sys, "argv", ["obdsniff", "--version"])
    with pytest.raises(SystemExit) as info:
        runpy.run_module("obdsniff", run_name="__main__")
    assert info.value.code == 0
    assert capsys.readouterr().out.startswith("obdsniff ")


# ---- shell ----------------------------------------------------------------------


def run_session(device: Device, lines: list[object]) -> list[str]:
    inputs = iter(lines)
    output: list[str] = []

    def read_line(prompt: str) -> str:
        item = next(inputs)
        if isinstance(item, BaseException):
            raise item
        return str(item)

    run_shell(device, read_line=read_line, write=output.append)
    return output


def test_shell_session(device, board) -> None:
    board.press_button()
    output = run_session(
        device,
        [
            "",
            "status",
            "help",
            "led blue",
            "mode interactive",
            "reset",
            "crash bus",
            "crash",
            "exit",
        ],
    )
    text = "\n".join(output)
    assert "[event] button pressed" in text
    assert "  Uptime (ms)" in text
    assert "  led on|off|auto" in text
    assert "error: invalid argument" in text
    assert "The laptop tool manages the console mode itself." in text
    assert "Board restarted." in text
    assert "Board restarted after the crash. What it recorded:" in text
    assert "error: wrong number of arguments (usage: crash bus|usage|divzero|hard|watchdog)" in text


@pytest.mark.parametrize("ending", [EOFError(), KeyboardInterrupt(), "quit"])
def test_shell_endings(device, ending) -> None:
    assert run_session(device, [ending])[0].startswith("Connected.")


def test_shell_reports_protocol_errors(device, monkeypatch) -> None:
    def silent(self, line: str) -> list[str]:
        raise ProtocolError("no reply to 'status'")

    monkeypatch.setattr(Device, "command", silent)
    output = run_session(device, ["status", "exit"])
    assert "error: no reply to 'status'" in output
