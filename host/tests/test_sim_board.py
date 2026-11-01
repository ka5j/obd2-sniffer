from __future__ import annotations

import pytest

from obdsniff.sim.board import LINE_MAX, SimulatedBoard, SimulatedTransport


def run(board: SimulatedBoard, text: str) -> str:
    board.feed(text.encode())
    return board.take_output().decode()


# ---- start-up --------------------------------------------------------------


def test_boot_report_ends_with_the_started_event(clock) -> None:
    out = SimulatedBoard(clock=clock).take_output().decode()
    assert "OBD2 CAN Sniffer" in out
    assert " Reset    power-on\r\n" in out
    assert "Crash" not in out
    assert out.endswith("!started\r\n")


# ---- reading lines ---------------------------------------------------------


def test_command_runs_with_no_echo(board) -> None:
    assert run(board, "can") == ""
    assert run(board, " loopback\r") == "OK\r\n"
    assert board.can.mode == "loopback"


def test_crlf_counts_once(board) -> None:
    assert run(board, "can off\r\n") == "OK\r\n"


def test_ctrl_c_discards_line(board) -> None:
    assert run(board, "reset\x03\r") == ""  # what's left is an empty line
    assert board.reset_cause == "power-on"


def overlong_loopback() -> str:
    """`can loopback` padded to the line limit plus one byte: it would still
    run `can loopback` if the extra byte were simply dropped."""
    return "can loopback".ljust(LINE_MAX) + "x"


def test_overlong_command_is_never_run(board) -> None:
    assert run(board, overlong_loopback()) == ""
    assert run(board, "\r") == "ERR line too long\r\n"
    assert board.can.mode == "off"


def test_ctrl_c_clears_overflow(board) -> None:
    run(board, overlong_loopback() + "\x03")
    assert run(board, "can loopback\r") == "OK\r\n"
    assert board.can.mode == "loopback"


def test_other_bytes_ignored(board) -> None:
    board.feed(bytes([0x1B, 0x80, 0x09, 0x08, 0x7F]))
    assert board.take_output() == b""


def test_empty_line_gets_no_reply(board) -> None:
    assert run(board, "\r\n\r") == ""


# ---- replies ---------------------------------------------------------------


def test_errors(board) -> None:
    assert run(board, "nope\r") == "ERR unknown command\r\n"
    assert run(board, "a b c d e f g h i\r") == "ERR too many arguments\r\n"
    assert run(board, "crash\r") == (
        "ERR wrong number of arguments (usage: crash bus|usage|divzero|hard|watchdog)\r\n"
    )
    for bad in ("help nope", "fault bogus", "crash bogus"):
        assert run(board, bad + "\r") == "ERR invalid argument\r\n"


def test_commands_are_case_insensitive(board) -> None:
    assert run(board, "CAN LOOPBACK\r") == "OK\r\n"
    assert board.can.mode == "loopback"


def test_status_and_version_fields(board, clock) -> None:
    clock.advance(1.5)
    status = run(board, "status\r")
    assert "uptime_ms=1500\r\n" in status
    assert "last_fault=none\r\n" in status
    assert status.endswith("OK\r\n")
    assert "firmware=simulator\r\n" in run(board, "version\r")


def test_help(board) -> None:
    out = run(board, "help\r")
    assert "  fault [clear]           Details of the last crash, or forget it\r\n" in out
    # Too wide for the column: the text goes on the next line, aligned.
    assert "  crash bus|usage|divzero|hard|watchdog\r\n" + " " * 26 + "Crash on purpose" in out
    assert out.endswith("OK\r\n")
    assert run(board, "help FAULT\r") == (
        "usage: fault [clear]\r\n  Details of the last crash, or forget it\r\nOK\r\n"
    )


def test_event(board) -> None:
    board.press_button()
    assert board.take_output() == b"!button pressed\r\n"


# ---- faults, crashes and restarts -------------------------------------------


def test_fault_none(board) -> None:
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


@pytest.mark.parametrize(
    ("kind", "fault_type", "has_address"),
    [("bus", "BusFault", True), ("usage", "UsageFault", False), ("hard", "HardFault", True)],
)
def test_crash_records_fault_and_restarts(board, clock, kind, fault_type, has_address) -> None:
    clock.advance(2)
    assert run(board, f"crash {kind}\r") == ""  # no reply: the board faults first
    board.run_pending()
    report = board.take_output().decode()
    assert " Reset    software\r\n" in report
    assert f" Crash    {fault_type} at 0x" in report
    assert report.endswith("!started\r\n")
    fault = run(board, "fault\r")
    assert f"fault={fault_type}\r\n" in fault
    assert "crash_uptime_ms=2000\r\n" in fault
    assert ("fault_address=" in fault) is has_address
    assert f"last_fault={fault_type}" in run(board, "status\r")
    assert run(board, "fault clear\r") == "OK\r\n"
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


def test_watchdog_crash_has_no_fault_record(board) -> None:
    run(board, "crash watchdog\r")
    board.run_pending()
    report = board.take_output().decode()
    assert " Reset    independent watchdog\r\n" in report
    assert "Crash" not in report


def test_crash_is_reported_once_like_the_firmware(board) -> None:
    run(board, "crash bus\r")
    board.run_pending()
    assert "Crash    BusFault" in board.take_output().decode()
    assert "fault=BusFault\r\n" in run(board, "fault\r")
    run(board, "reset\r")  # any later restart forgets it
    board.run_pending()
    assert "Crash" not in board.take_output().decode()
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


def test_reset_replies_then_restarts(board) -> None:
    assert run(board, "reset\r") == "OK\r\n"
    board.run_pending()
    assert " Reset    software\r\n" in board.take_output().decode()
    board.run_pending()  # nothing pending any more
    assert board.take_output() == b""


# ---- transport -------------------------------------------------------------


def test_transport_reads_output_and_runs_pending_restart(board) -> None:
    transport = SimulatedTransport(board)
    transport.write(b"reset\r")
    assert transport.read(0) == b"OK\r\n"
    assert b"Reset    software" in transport.read(0)
    assert transport.read(0) == b""
    assert transport.read(0.001) == b""  # waits briefly when idle


def test_transport_creates_board_by_default() -> None:
    assert b"OBD2 CAN Sniffer" in SimulatedTransport().read(0)
