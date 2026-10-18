from __future__ import annotations

import pytest
import serial

import obdsniff.device as device_module
from obdsniff.device import (
    CommandError,
    Device,
    DeviceError,
    ProtocolError,
    open_device,
    parse_fields,
)
from obdsniff.simulator import SimulatedBoard, SimulatedTransport

from .conftest import ScriptedTransport

# ---- connecting -------------------------------------------------------------


def test_connect_recovers_from_half_typed_line(board) -> None:
    board.feed(b"led o")  # someone left a partial command in the terminal
    board.take_output()
    dev = Device(SimulatedTransport(board), timeout=0.5)
    dev.connect()
    assert board.mode == "host"
    assert board.led == "auto"  # the partial command was discarded, not run


def test_connect_keeps_events_from_a_board_already_in_host_mode() -> None:
    dev = Device(ScriptedTransport([b"!button pressed\r\n", b"OK\r\n"]), timeout=0.05)
    dev.connect()
    assert dev.poll_events() == ["button pressed"]


def test_connect_times_out_without_reply() -> None:
    dev = Device(ScriptedTransport(), timeout=0.05)
    with pytest.raises(ProtocolError, match="no reply from the board"):
        dev.connect()


# ---- commands ---------------------------------------------------------------


def test_queries_return_fields(device, board) -> None:
    assert device.version()["firmware"] == "simulator"
    assert device.status()["led"] == "auto"
    assert device.fault() == {"fault": "none"}
    assert any(line.startswith("  led ") for line in device.help())


def test_set_led_and_clear_fault(device, board) -> None:
    device.set_led("on")
    assert board.led == "on"
    device.crash("bus")
    assert device.fault()["fault"] == "BusFault"
    device.clear_fault()
    assert device.fault() == {"fault": "none"}


def test_error_reply_raises_command_error(device) -> None:
    with pytest.raises(CommandError) as info:
        device.command("led blue")
    assert info.value.reason == "invalid argument"
    assert info.value.command == "led blue"


def test_command_must_be_one_line(device) -> None:
    with pytest.raises(ValueError, match="single line"):
        device.command("led on\rreset")


def test_command_times_out() -> None:
    dev = Device(ScriptedTransport(), timeout=0.05)
    with pytest.raises(ProtocolError, match="no reply to 'status'"):
        dev.command("status")


def test_query_rejects_non_field_lines(device) -> None:
    with pytest.raises(ProtocolError, match="expected key=value"):
        device.query("help")


def test_parse_fields_splits_at_first_equals() -> None:
    assert parse_fields(["a=b=c", "empty="]) == {"a": "b=c", "empty": ""}


# ---- events -----------------------------------------------------------------


def test_events_received_during_a_command_are_kept(device, board) -> None:
    board.press_button()
    assert device.status()["led"] == "auto"
    assert device.poll_events() == ["button pressed"]
    assert device.poll_events() == []


def test_event_interleaved_with_a_reply_is_kept() -> None:
    dev = Device(ScriptedTransport([b"led=auto\r\n!button pressed\r\nOK\r\n"]), timeout=0.05)
    assert dev.status() == {"led": "auto"}
    assert dev.poll_events() == ["button pressed"]


def test_poll_events_without_waiting_reads_new_events(device, board) -> None:
    board.press_button()  # arrives with no command in progress
    assert device.poll_events() == ["button pressed"]


def test_stale_reply_lines_are_discarded_before_a_command(device, board) -> None:
    # Leftovers from an earlier command that timed out, plus an event.
    device._buffer += b"uptime_ms=1\r\nOK\r\n!button pressed\r\n"
    assert device.status()["led"] == "auto"  # its own reply, not the stale OK
    assert device.poll_events() == ["button pressed"]


def test_poll_events_waits_for_new_events(device, board) -> None:
    board.press_button()
    assert device.poll_events(timeout=0.05) == ["button pressed"]


def test_poll_events_ignores_stray_lines() -> None:
    dev = Device(ScriptedTransport(), timeout=0.05)
    dev._buffer += b"stray\r\n!button pressed\r\n"
    assert dev.poll_events() == ["button pressed"]


# ---- restarts ---------------------------------------------------------------


def test_reset_reconnects(device, board) -> None:
    device.set_led("on")
    device.reset()
    assert board.reset_cause == "software"
    assert board.mode == "host"
    assert device.status()["led"] == "auto"


def test_reset_fails_if_board_does_not_come_back(monkeypatch) -> None:
    monkeypatch.setattr(device_module, "BOOT_TIMEOUT", 0.05)
    dev = Device(ScriptedTransport([b"OK\r\n"]), timeout=0.05)
    with pytest.raises(ProtocolError, match="did not come back"):
        dev.reset()


def test_crash_waits_for_restart(device, board) -> None:
    device.crash("usage")
    assert board.mode == "host"
    assert device.fault()["fault"] == "UsageFault"


def test_crash_rejected_by_release_firmware() -> None:
    dev = Device(ScriptedTransport([b"ERR unknown command\r\n"]), timeout=0.05)
    with pytest.raises(CommandError) as info:
        dev.crash("bus")
    assert info.value.reason == "unknown command"


def test_crash_fails_if_board_does_not_restart(monkeypatch) -> None:
    monkeypatch.setattr(device_module, "BOOT_TIMEOUT", 0.05)
    # Start-up text arrives, but never the prompt.
    dev = Device(ScriptedTransport([b"========\r\n"]), timeout=0.05)
    with pytest.raises(ProtocolError, match="did not restart"):
        dev.crash("bus")


# ---- life cycle -------------------------------------------------------------


def test_context_manager_closes_transport() -> None:
    transport = ScriptedTransport()
    with Device(transport):
        pass
    assert transport.closed


def test_open_simulator() -> None:
    with open_device("sim") as dev:
        assert dev.version()["mcu"] == "STM32F446RE"


def test_open_without_board_explains_options(monkeypatch) -> None:
    monkeypatch.setattr("obdsniff.ports.find_board", lambda: None)
    with pytest.raises(DeviceError, match="--port sim"):
        open_device(None)


def test_open_finds_board_automatically(monkeypatch) -> None:
    opened = []

    def fake_serial(port: str) -> SimulatedTransport:
        opened.append(port)
        return SimulatedTransport(SimulatedBoard())

    monkeypatch.setattr("obdsniff.ports.find_board", lambda: "/dev/ttyACM0")
    monkeypatch.setattr(device_module, "SerialTransport", fake_serial)
    with open_device(None) as dev:
        assert dev.status()["clock"] == "HSE 8 MHz bypass"
    assert opened == ["/dev/ttyACM0"]


def test_open_reports_unusable_port(monkeypatch) -> None:
    def broken(port: str) -> None:
        raise serial.SerialException("busy")

    monkeypatch.setattr(device_module, "SerialTransport", broken)
    with pytest.raises(DeviceError, match="cannot open /dev/x: busy"):
        open_device("/dev/x")


def test_open_closes_port_when_board_does_not_answer(monkeypatch) -> None:
    transport = ScriptedTransport()
    monkeypatch.setattr(device_module, "SerialTransport", lambda port: transport)
    with pytest.raises(ProtocolError):
        open_device("/dev/x", timeout=0.05)
    assert transport.closed
