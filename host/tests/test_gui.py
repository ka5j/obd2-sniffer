"""Desktop app tests: the real window and worker, driven against the simulator.

The worker stays on the test's thread here, so every click is handled
straight away and in order. The app's own two-thread setup is covered by
test_run_opens_and_closes.
"""

from __future__ import annotations

import sys
import time

import pytest
import serial
from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import QApplication

import obdsniff
import obdsniff.cli as cli
from obdsniff import gui
from obdsniff.device import Device, DeviceError, ProtocolError
from obdsniff.gui.can_panel import MAX_ROWS, FrameTableModel, frame_row
from obdsniff.gui.window import SIMULATOR, MainWindow
from obdsniff.gui.worker import DeviceWorker
from obdsniff.packets import CanFrame
from obdsniff.ports import PortInfo
from obdsniff.simulator import SimulatedBoard, SimulatedTransport

BOARD_PORT = PortInfo("/dev/cu.usbmodem1", "STM32 STLink", True)
OTHER_PORT = PortInfo("/dev/cu.Bluetooth", "n/a", False)


@pytest.fixture
def sim() -> SimulatedBoard:
    board = SimulatedBoard()
    board.take_output()  # discard the start-up report
    return board


@pytest.fixture
def opened() -> list[str | None]:
    """Ports the worker was asked to open."""
    return []


@pytest.fixture
def worker(sim, opened) -> DeviceWorker:
    def opener(port: str | None) -> Device:
        opened.append(port)
        dev = Device(SimulatedTransport(sim), timeout=0.5)
        dev.connect()
        return dev

    return DeviceWorker(opener=opener)


def make_window(qtbot, worker, ports, port=None) -> MainWindow:
    window = MainWindow(worker, port, list_ports=lambda: list(ports))
    qtbot.addWidget(window)
    return window


@pytest.fixture
def window(qtbot, worker) -> MainWindow:
    return make_window(qtbot, worker, [OTHER_PORT, BOARD_PORT])


@pytest.fixture
def connected(window) -> MainWindow:
    window.connect_button.click()
    return window


def type_command(window: MainWindow, line: str) -> None:
    window.console_input.setText(line)
    window.console_input.returnPressed.emit()


# ---- ports --------------------------------------------------------------------


def test_starts_disconnected_with_the_board_selected(window) -> None:
    assert window.selected_port() == BOARD_PORT.device
    assert window.connect_button.text() == "Connect"
    assert not window.console_input.isEnabled()
    assert not window.led_buttons["on"].isEnabled()
    assert window.port_box.isEnabled()


def test_simulator_is_the_default_without_a_board(qtbot, worker) -> None:
    assert make_window(qtbot, worker, [OTHER_PORT]).selected_port() == SIMULATOR


def test_port_given_on_the_command_line_is_selected(qtbot, worker) -> None:
    assert make_window(qtbot, worker, [BOARD_PORT], "/dev/custom").selected_port() == "/dev/custom"
    assert make_window(qtbot, worker, [BOARD_PORT], SIMULATOR).selected_port() == SIMULATOR


def test_refresh_finds_a_board_plugged_in_later(qtbot, worker) -> None:
    ports = [OTHER_PORT]
    window = make_window(qtbot, worker, ports)
    ports.append(BOARD_PORT)
    window.ports_button.click()
    assert window.selected_port() == BOARD_PORT.device


# ---- connecting ---------------------------------------------------------------


def test_connect_shows_the_board(connected, opened) -> None:
    assert opened == [BOARD_PORT.device]
    assert connected.connect_button.text() == "Disconnect"
    assert connected.info_value("Board", "Firmware") == "simulator"
    assert connected.info_value("Status", "LED") == "auto"
    assert connected.info_value("Last crash", "Last crash") == "none"
    assert connected.info_value("Board", "No such label") is None
    assert connected.led_buttons["auto"].isChecked()
    assert not connected.clear_fault_button.isEnabled()
    assert not connected.port_box.isEnabled()
    assert f"Connected to {BOARD_PORT.device}." in connected.console_text()


def test_connect_to_the_simulator(qtbot, worker) -> None:
    window = make_window(qtbot, worker, [])
    window.connect_button.click()
    assert "Connected to the simulator." in window.console_text()


def test_connect_failure_is_reported(qtbot) -> None:
    def no_board(port: str | None) -> Device:
        raise DeviceError("no board found")

    window = make_window(qtbot, DeviceWorker(opener=no_board), [])
    window.connect_button.click()
    assert "error: no board found" in window.console_text()
    assert window.connect_button.text() == "Connect"
    assert window.connect_button.isEnabled()


def test_disconnect_hands_the_board_back(connected, sim) -> None:
    connected.connect_button.click()
    assert sim.mode == "interactive"  # a serial terminal works again
    assert connected.connect_button.text() == "Connect"
    assert connected.info_tree.topLevelItemCount() == 0
    assert not connected.console_input.isEnabled()
    assert connected.console_text().endswith("Disconnected.")


def test_lost_connection(connected, worker, monkeypatch) -> None:
    def unplugged(self: Device, timeout: float = 0.0) -> list[str]:
        raise serial.SerialException("device disconnected")

    monkeypatch.setattr(Device, "poll_events", unplugged)
    worker.poll()
    assert "Disconnected: connection lost: device disconnected." in connected.console_text()
    assert connected.connect_button.text() == "Connect"


# ---- controls -----------------------------------------------------------------


def test_led_buttons(connected, sim) -> None:
    connected.led_buttons["on"].click()
    assert sim.led == "on"
    assert connected.info_value("Status", "LED") == "on"
    assert connected.led_buttons["on"].isChecked()


def test_unknown_led_mode_checks_no_button(window) -> None:
    window._on_info({}, {"led": "blink"}, {"fault": "none"})
    assert window._led_group.checkedButton() is None


def test_restart_button(connected) -> None:
    connected.reset_button.click()
    assert "Board restarted." in connected.console_text()
    assert connected.info_value("Status", "Last reset") == "software"


def test_crash_then_clear_the_record(connected) -> None:
    type_command(connected, "crash bus")
    assert "Board restarted after the crash. What it recorded:" in connected.console_text()
    assert connected.info_value("Last crash", "Last crash") == "BusFault"
    assert connected.clear_fault_button.isEnabled()
    connected.clear_fault_button.click()
    assert connected.info_value("Last crash", "Last crash") == "none"
    assert not connected.clear_fault_button.isEnabled()


def test_refresh_failure_keeps_the_connection(connected, monkeypatch) -> None:
    def silent(self: Device) -> dict[str, str]:
        raise ProtocolError("no reply to 'status'")

    monkeypatch.setattr(Device, "status", silent)
    connected.refresh_button.click()
    assert "error: no reply to 'status'" in connected.console_text()
    assert connected.connect_button.text() == "Disconnect"


# ---- console ------------------------------------------------------------------


def test_console_runs_commands(connected) -> None:
    type_command(connected, "status")
    text = connected.console_text()
    assert "obd> status" in text
    assert "Uptime (ms)" in text


def test_console_ignores_empty_lines(connected) -> None:
    before = connected.console_text()
    type_command(connected, "   ")
    assert connected.console_text() == before


def test_console_reports_board_errors(connected) -> None:
    type_command(connected, "led blue")
    assert "error: led blue: invalid argument" in connected.console_text()


def test_events_appear_as_they_happen(connected, sim, qtbot) -> None:
    sim.press_button()
    qtbot.waitUntil(lambda: "event: button pressed" in connected.console_text(), timeout=2000)


# ---- worker on its own --------------------------------------------------------


def test_requests_need_a_connection(worker, qtbot) -> None:
    with qtbot.waitSignal(worker.failed) as blocker:
        worker.refresh()
    assert blocker.args == ["not connected"]


def test_disconnect_when_not_connected_does_nothing(worker, qtbot) -> None:
    with qtbot.assertNotEmitted(worker.disconnected):
        worker.close_port()


# ---- starting the app ---------------------------------------------------------


def test_run_opens_and_closes(qtbot, monkeypatch) -> None:
    monkeypatch.setattr("obdsniff.gui.window.list_serial_ports", lambda: [])
    QTimer.singleShot(0, QApplication.quit)
    assert gui.run(SIMULATOR) == 0


def test_gui_command_starts_the_app(monkeypatch) -> None:
    started: list[str | None] = []
    monkeypatch.setattr(gui, "run", lambda port: started.append(port) or 0)
    assert cli.main(["--port", "sim", "gui"]) == 0
    assert started == ["sim"]


def test_gui_command_without_pyside(monkeypatch, capsys) -> None:
    monkeypatch.delattr(obdsniff, "gui")
    monkeypatch.setitem(sys.modules, "obdsniff.gui", None)  # import now fails
    assert cli.main(["gui"]) == cli.EXIT_NO_CONNECTION
    assert 'pip install -e "host[gui]"' in capsys.readouterr().err


# ---- CAN tab ----------------------------------------------------------------------


def test_can_tab_loopback_test_traffic_and_stop(connected, worker) -> None:
    panel = connected.can_panel
    assert panel.mode_label.text() == "CAN is off."
    assert not panel.test_button.isEnabled()

    panel.bitrate_box.setCurrentIndex(panel.bitrate_box.findData(250))
    panel.start_button.click()
    assert panel.mode_label.text().startswith("Loopback at 250 kbit/s")
    assert panel.start_button.text() == "Stop"
    assert panel.test_button.isEnabled()

    panel.count_box.setValue(50)
    panel.test_button.click()
    worker.poll()
    assert panel.model.rowCount() == 50
    assert panel.stats_label.text().startswith("Frames 50 ")
    assert "Lost 0" in panel.stats_label.text()
    assert panel.model.data(panel.model.index(0, 1)) == "123"

    panel.update_rate()
    assert "Rate 50/s" in panel.stats_label.text()

    panel.start_button.click()
    assert panel.mode_label.text() == "CAN is off."
    assert not panel.test_button.isEnabled()


def test_can_tab_streaming_switch(connected, worker) -> None:
    panel = connected.can_panel
    assert panel.stream_box.isChecked()
    panel.start_button.click()
    panel.stream_box.click()
    assert not panel.stream_box.isChecked()
    panel.count_box.setValue(20)
    panel.test_button.click()
    worker.poll()
    assert panel.model.rowCount() == 0  # received on the board, not sent here
    panel.stream_box.click()
    assert panel.stream_box.isChecked()
    panel.test_button.click()
    worker.poll()
    assert panel.model.rowCount() == 20


def test_can_tab_shows_the_boards_streaming_state(window) -> None:
    window.can_panel.on_can_state({"can_mode": "off", "can_stream": "off"})
    assert not window.can_panel.stream_box.isChecked()


def test_can_tab_is_disabled_when_disconnected(connected) -> None:
    connected.connect_button.click()
    assert not connected.can_panel.start_button.isEnabled()
    assert not connected.can_panel.test_button.isEnabled()
    assert not connected.can_panel.stream_box.isEnabled()


# ---- OBD2 tab -----------------------------------------------------------------------


def poll_until(worker: DeviceWorker, done) -> None:
    """Let the simulated board run, polling as the worker's timer would."""
    for _ in range(50):
        worker.poll()
        if done():
            return
        time.sleep(0.01)
    raise AssertionError("condition not reached")


def test_obd_tab_reads_the_simulated_ecu(connected, worker) -> None:
    panel = connected.obd_panel
    assert panel.start_button.text() == "Start"
    assert panel.value_labels["obd_rpm"].text() == "-"
    panel.start_button.click()
    assert panel.start_button.text() == "Stop"
    assert connected.can_panel.start_button.text() == "Stop"  # loopback started for it
    poll_until(worker, lambda: panel.vin_label.text().endswith("1M8GDM9AXKP042788"))
    poll_until(worker, lambda: panel.value_labels["obd_run_time"].text() != "-")
    assert panel.value_labels["obd_rpm"].text() == "800"
    assert panel.dtc_label.text() == "Trouble codes: P0301 P0420"
    assert panel.mil_label.text() == "Warning light: on"
    assert "color" in panel.mil_label.styleSheet()
    assert panel.counters_label.text().startswith("Requests ")

    panel.start_button.click()
    assert panel.start_button.text() == "Start"
    panel.start_button.click()  # loopback is still on: just the tester again
    assert panel.start_button.text() == "Stop"


def test_obd_tab_follows_a_tester_started_from_the_console(connected) -> None:
    type_command(connected, "can loopback")
    type_command(connected, "obd start")
    assert connected.obd_panel.start_button.text() == "Stop"
    assert connected.obd_panel.mil_label.styleSheet() == ""  # nothing read yet


def test_obd_tab_is_disabled_when_disconnected(connected) -> None:
    assert connected.obd_panel.start_button.isEnabled()
    connected.connect_button.click()
    assert not connected.obd_panel.start_button.isEnabled()


def test_frame_table_keeps_the_newest_rows(window) -> None:
    panel = window.can_panel
    frames = [
        CanFrame(timestamp_us=n, id=0x7E8, data=b"\x01", seq=n) for n in range(MAX_ROWS + 100)
    ]
    panel.on_frames(frames, damaged=2)
    assert panel.model.rowCount() == MAX_ROWS
    assert panel.model.data(panel.model.index(MAX_ROWS - 1, 0)) == f"{(MAX_ROWS + 99) / 1e6:.6f}"
    assert "Damaged 2" in panel.stats_label.text()
    panel.model.clear()
    assert panel.model.rowCount() == 0


def test_frame_table_model_text() -> None:
    model = FrameTableModel()
    model.add(
        [frame_row(CanFrame(timestamp_us=0, id=0x18DAF110, dlc=8, extended=True, rtr=True), 1.5)]
    )
    assert [model.data(model.index(0, c)) for c in range(4)] == [
        "1.500000",
        "18DAF110",
        "8",
        "remote request",
    ]
    assert model.columnCount() == 4
    assert model.data(model.index(0, 0), Qt.ItemDataRole.ToolTipRole) is None
    assert model.headerData(1, Qt.Orientation.Horizontal) == "ID"
    assert model.headerData(1, Qt.Orientation.Vertical) is None
    assert model.headerData(1, Qt.Orientation.Horizontal, Qt.ItemDataRole.ToolTipRole) is None
