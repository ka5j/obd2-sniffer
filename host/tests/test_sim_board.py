from __future__ import annotations

import pytest

from obdsniff.sim.board import LINE_MAX, SimulatedBoard, SimulatedTransport


def run(board: SimulatedBoard, text: str) -> str:
    board.feed(text.encode())
    return board.take_output().decode()


def host(board: SimulatedBoard) -> SimulatedBoard:
    run(board, "mode host\r")
    return board


# ---- start-up --------------------------------------------------------------


def test_boot_report_ends_with_prompt(clock) -> None:
    out = SimulatedBoard(clock=clock).take_output().decode()
    assert "OBD2 CAN Sniffer" in out
    assert " Reset    power-on\r\n" in out
    assert "Crash" not in out
    assert out.endswith("obd> ")


# ---- interactive editing ---------------------------------------------------


def test_echo_and_command(board) -> None:
    assert run(board, "led") == "led"
    assert run(board, " on\r") == " on\r\nobd> "
    assert board.led == "on"


def test_crlf_counts_once(board) -> None:
    assert run(board, "led off\r\n") == "led off\r\nobd> "


def test_backspace_and_delete(board) -> None:
    run(board, "lex")
    assert run(board, "\b") == "\b \b"
    assert run(board, "\x7f\x7f\x7f") == "\b \b\b \b"  # third has nothing to erase


def test_ctrl_c_discards_line(board) -> None:
    assert run(board, "reset\x03") == "reset^C\r\nobd> "


def overlong_led_on() -> str:
    """`led on` padded to the line limit plus one byte: it would still run
    `led on` if the extra byte were simply dropped."""
    return "led on".ljust(LINE_MAX) + "x"


def test_overflow_rings_bell_and_rejects_line(board) -> None:
    run(board, "x" * LINE_MAX)
    assert run(board, "y") == "\a"
    assert run(board, "\r") == "\r\nerror: line too long\r\nobd> "


def test_overlong_command_is_never_run(board) -> None:
    run(board, overlong_led_on())
    run(board, "\r")
    assert board.led == "auto"
    host(board)
    assert run(board, overlong_led_on() + "\r") == "ERR line too long\r\n"
    assert board.led == "auto"


def test_ctrl_c_clears_overflow(board) -> None:
    run(board, overlong_led_on() + "\x03")
    assert run(board, "led on\r") == "led on\r\nobd> "
    assert board.led == "on"


def test_other_bytes_ignored(board) -> None:
    board.feed(bytes([0x1B, 0x80, 0x09]))
    assert board.take_output() == b""


def test_empty_line_prompts(board) -> None:
    assert run(board, "\r") == "\r\nobd> "


# ---- errors ----------------------------------------------------------------


def test_interactive_errors(board) -> None:
    assert run(board, "lde on\r") == (
        "lde on\r\nerror: unknown command (type 'help' for a list)\r\nobd> "
    )
    assert run(board, "led\r") == (
        "led\r\nerror: wrong number of arguments (usage: led on|off|auto)\r\nobd> "
    )
    assert run(board, "led blue\r").endswith("error: invalid argument\r\nobd> ")


def test_host_errors(board) -> None:
    host(board)
    assert run(board, "nope\r") == "ERR unknown command\r\n"
    assert run(board, "a b c d e f g h i\r") == "ERR too many arguments\r\n"
    for bad in ("mode x", "help nope", "fault bogus", "crash bogus"):
        assert run(board, bad + "\r") == "ERR invalid argument\r\n"


# ---- host mode -------------------------------------------------------------


def test_switch_modes(board) -> None:
    assert run(board, "mode host\r") == "mode host\r\nOK\r\n"
    assert run(board, "led on\r") == "OK\r\n"
    assert run(board, "\r") == ""
    assert run(board, "x" * (LINE_MAX + 1) + "\b\x03") == ""  # silent editing
    assert run(board, "mode interactive\r") == "obd> "


def test_commands_are_case_insensitive(board) -> None:
    host(board)
    assert run(board, "LED ON\r") == "OK\r\n"
    assert board.led == "on"


def test_status_and_version_fields(board, clock) -> None:
    host(board)
    clock.advance(1.5)
    status = run(board, "status\r")
    assert "uptime_ms=1500\r\n" in status
    assert "last_fault=none\r\n" in status
    assert status.endswith("OK\r\n")
    assert "firmware=simulator\r\n" in run(board, "version\r")


def test_interactive_field_alignment(board) -> None:
    out = run(board, "status\r")
    assert "  Uptime (ms)       0\r\n" in out  # padded to the value column
    assert f"  Serial received   {board.rx_bytes}\r\n" in out


def test_help(board) -> None:
    out = run(board, "help\r")
    assert "  led on|off|auto         Green LED: on, off, or heartbeat (auto)\r\n" in out
    # Too wide for the column: the text goes on the next line, aligned.
    assert "  crash bus|usage|divzero|hard|watchdog\r\n" + " " * 26 + "Crash on purpose" in out
    assert run(board, "help LED\r") == (
        "help LED\r\nusage: led on|off|auto\r\n  Green LED: on, off, or heartbeat (auto)\r\nobd> "
    )


# ---- events ----------------------------------------------------------------


def test_event_in_host_mode(board) -> None:
    host(board).press_button()
    assert board.take_output() == b"!button pressed\r\n"


def test_event_restores_partial_line(board) -> None:
    run(board, "le")
    board.press_button()
    assert board.take_output() == b"\r\nbutton pressed\r\nobd> le"


def test_event_after_completed_line(board) -> None:
    run(board, "led on\r")
    board.press_button()
    assert board.take_output() == b"\r\nbutton pressed\r\nobd> "


# ---- faults, crashes and restarts -------------------------------------------


def test_fault_none(board) -> None:
    host(board)
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


@pytest.mark.parametrize(
    ("kind", "fault_type", "has_address"),
    [("bus", "BusFault", True), ("usage", "UsageFault", False), ("hard", "HardFault", True)],
)
def test_crash_records_fault_and_restarts(board, clock, kind, fault_type, has_address) -> None:
    host(board)
    clock.advance(2)
    assert run(board, f"crash {kind}\r") == ""  # no reply: the board faults first
    board.run_pending()
    report = board.take_output().decode()
    assert " Reset    software\r\n" in report
    assert f" Crash    {fault_type} at 0x" in report
    host(board)
    fault = run(board, "fault\r")
    assert f"fault={fault_type}\r\n" in fault
    assert "crash_uptime_ms=2000\r\n" in fault
    assert ("fault_address=" in fault) is has_address
    assert f"last_fault={fault_type}" in run(board, "status\r")
    assert run(board, "fault clear\r") == "OK\r\n"
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


def test_fault_shown_interactively(board) -> None:
    host(board)
    run(board, "crash divzero\r")
    board.run_pending()
    board.take_output()
    out = run(board, "fault\r")
    assert "  Called from (LR)  0x" in out  # the longest label still lines up
    assert "  Cause             divide by zero\r\n" in out


def test_watchdog_crash_has_no_fault_record(board) -> None:
    host(board)
    run(board, "crash watchdog\r")
    board.run_pending()
    report = board.take_output().decode()
    assert " Reset    independent watchdog\r\n" in report
    assert "Crash" not in report


def test_crash_is_reported_once_like_the_firmware(board) -> None:
    host(board)
    run(board, "crash bus\r")
    board.run_pending()
    assert "Crash    BusFault" in board.take_output().decode()
    assert "  Last crash        BusFault\r\n" in run(board, "fault\r")
    run(board, "reset\r")  # any later restart forgets it
    board.run_pending()
    assert "Crash" not in board.take_output().decode()
    host(board)
    assert run(board, "fault\r") == "fault=none\r\nOK\r\n"


def test_reset_replies_then_restarts(board) -> None:
    host(board)
    assert run(board, "reset\r") == "OK\r\n"
    board.run_pending()
    assert " Reset    software\r\n" in board.take_output().decode()
    board.run_pending()  # nothing pending any more
    assert board.take_output() == b""


# ---- transport -------------------------------------------------------------


def test_transport_reads_output_and_runs_pending_restart(board) -> None:
    transport = SimulatedTransport(board)
    transport.write(b"reset\r")
    assert transport.read(0) == b"reset\r\nobd> "
    assert b"Reset    software" in transport.read(0)
    assert transport.read(0) == b""
    assert transport.read(0.001) == b""  # waits briefly when idle


def test_transport_creates_board_by_default() -> None:
    assert b"OBD2 CAN Sniffer" in SimulatedTransport().read(0)
