"""The desktop app's main window.

The window only displays things and turns clicks into requests. All talking
to the board happens in the DeviceWorker (see worker.py); the two are wired
together with signals.
"""

from __future__ import annotations

import time
from collections.abc import Callable

from PySide6.QtCore import Signal
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (
    QButtonGroup,
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTabWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from obdsniff.gui.can_panel import CanPanel
from obdsniff.gui.obd_panel import ObdPanel
from obdsniff.gui.worker import DeviceWorker
from obdsniff.output import LABELS
from obdsniff.ports import PortInfo, list_serial_ports

SIMULATOR = "sim"
LED_MODES = ("on", "off", "auto")


class MainWindow(QMainWindow):
    # Requests to the worker. Qt queues them to the worker's thread.
    open_requested = Signal(str)
    close_requested = Signal()
    refresh_requested = Signal()
    led_requested = Signal(str)
    clear_fault_requested = Signal()
    reset_requested = Signal()
    line_requested = Signal(str)

    def __init__(
        self,
        worker: DeviceWorker,
        port: str | None = None,
        list_ports: Callable[[], list[PortInfo]] = list_serial_ports,
    ) -> None:
        super().__init__()
        self._list_ports = list_ports
        self._connected = False
        self.setWindowTitle("OBD2 CAN Sniffer")
        self.resize(1100, 760)

        self._build_connection_bar()
        self._build_board_panel()
        self._build_console()
        self.can_panel = CanPanel()
        self.obd_panel = ObdPanel()
        self.tabs = QTabWidget()
        self.tabs.addTab(self._console_panel, "Console")
        self.tabs.addTab(self.can_panel, "CAN bus")
        self.tabs.addTab(self.obd_panel, "OBD2")
        splitter = QSplitter()
        splitter.addWidget(self._board_panel)
        splitter.addWidget(self.tabs)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([440, 660])
        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addLayout(self._connection_bar)
        layout.addWidget(splitter, 1)
        self.setCentralWidget(central)

        self._wire(worker)
        self.refresh_ports(select=port)
        self._show_disconnected()
        self.statusBar().showMessage("Choose a port and press Connect.")

    # ---- layout ---------------------------------------------------------------

    def _build_connection_bar(self) -> None:
        self.port_box = QComboBox()
        self.port_box.setMinimumWidth(320)
        self.ports_button = QPushButton("Refresh ports")
        self.ports_button.clicked.connect(lambda: self.refresh_ports())
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self._on_connect_clicked)
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Port"))
        bar.addWidget(self.port_box, 1)
        bar.addWidget(self.ports_button)
        bar.addWidget(self.connect_button)
        self._connection_bar = bar

    def _build_board_panel(self) -> None:
        self.info_tree = QTreeWidget()
        self.info_tree.setColumnCount(2)
        self.info_tree.setHeaderHidden(True)
        self.info_tree.setColumnWidth(0, 190)

        self.led_buttons: dict[str, QPushButton] = {}
        self._led_group = QButtonGroup(self)
        led_row = QHBoxLayout()
        for mode in LED_MODES:
            button = QPushButton(mode.capitalize())
            button.setCheckable(True)
            # `clicked` fires for the user only, not when the display is updated.
            button.clicked.connect(lambda _=False, m=mode: self.led_requested.emit(m))
            self._led_group.addButton(button)
            self.led_buttons[mode] = button
            led_row.addWidget(button)
        led_box = QGroupBox("Green LED")
        led_box.setLayout(led_row)

        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.clicked.connect(self.refresh_requested)
        self.reset_button = QPushButton("Restart board")
        self.reset_button.clicked.connect(self._on_reset_clicked)
        self.clear_fault_button = QPushButton("Clear crash record")
        self.clear_fault_button.clicked.connect(self.clear_fault_requested)
        actions = QHBoxLayout()
        actions.addWidget(self.refresh_button)
        actions.addWidget(self.reset_button)
        actions.addWidget(self.clear_fault_button)

        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.info_tree, 1)
        layout.addWidget(led_box)
        layout.addLayout(actions)
        self._board_panel = panel

    def _build_console(self) -> None:
        mono = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
        self.console_view = QPlainTextEdit()
        self.console_view.setReadOnly(True)
        self.console_view.setFont(mono)
        self.console_input = QLineEdit()
        self.console_input.setFont(mono)
        self.console_input.setPlaceholderText("Type a board command and press Enter, e.g. help")
        self.console_input.returnPressed.connect(self._on_console_entered)
        box = QWidget()  # shown in the "Console" tab
        layout = QVBoxLayout(box)
        layout.addWidget(self.console_view, 1)
        layout.addWidget(self.console_input)
        self._console_panel = box

    def _wire(self, worker: DeviceWorker) -> None:
        self.open_requested.connect(worker.open_port)
        self.close_requested.connect(worker.close_port)
        self.refresh_requested.connect(worker.refresh)
        self.led_requested.connect(worker.set_led)
        self.clear_fault_requested.connect(worker.clear_fault)
        self.reset_requested.connect(worker.reset)
        self.line_requested.connect(worker.run_line)
        worker.connected.connect(self._on_connected)
        worker.disconnected.connect(self._on_disconnected)
        worker.info.connect(self._on_info)
        worker.console.connect(self._on_console_rows)
        worker.event.connect(self._on_event)
        worker.failed.connect(self._on_failed)
        self.can_panel.loopback_requested.connect(worker.can_loopback)
        self.can_panel.off_requested.connect(worker.can_off)
        self.can_panel.test_requested.connect(worker.can_test)
        self.can_panel.stream_requested.connect(worker.can_stream)
        worker.frames.connect(self.can_panel.on_frames)
        worker.can_state.connect(self.can_panel.on_can_state)
        self.obd_panel.start_requested.connect(worker.obd_start)
        self.obd_panel.stop_requested.connect(worker.obd_stop)
        worker.obd_state.connect(self.obd_panel.on_obd_state)

    # ---- ports ----------------------------------------------------------------

    def refresh_ports(self, select: str | None = None) -> None:
        """List the serial ports and the simulator. Selects `select` if given,
        otherwise the board if one is found, otherwise the simulator."""
        self.port_box.clear()
        for port in self._list_ports():
            text = f"{port.device}  ({port.description})"
            if port.is_board:
                text += "  - Nucleo board"
            self.port_box.addItem(text, port.device)
        self.port_box.addItem("Simulator (no board needed)", SIMULATOR)
        if select is not None and self.port_box.findData(select) < 0:
            self.port_box.addItem(select, select)
        wanted = select if select is not None else self._first_board()
        self.port_box.setCurrentIndex(max(self.port_box.findData(wanted), 0))

    def _first_board(self) -> str:
        boards = [p.device for p in self._list_ports() if p.is_board]
        return boards[0] if boards else SIMULATOR

    def selected_port(self) -> str:
        return str(self.port_box.currentData())

    # ---- user actions ---------------------------------------------------------

    def _on_connect_clicked(self) -> None:
        if self._connected:
            self.close_requested.emit()
            return
        # Shown until the worker answers with connected or failed.
        self.connect_button.setEnabled(False)
        self.connect_button.setText("Connecting...")
        self.open_requested.emit(self.selected_port())

    def _on_reset_clicked(self) -> None:
        self.statusBar().showMessage("Restarting the board...")
        self.reset_requested.emit()

    def _on_console_entered(self) -> None:
        line = self.console_input.text().strip()
        self.console_input.clear()
        if not line:
            return
        self.append_console(f"obd> {line}")
        self.line_requested.emit(line)

    # ---- results from the worker ---------------------------------------------

    def _on_connected(self, port: str) -> None:
        self._connected = True
        where = "the simulator" if port == SIMULATOR else port
        self.statusBar().showMessage(f"Connected to {where}.")
        self.append_console(f"Connected to {where}. Type 'help' for the board's commands.")
        self.connect_button.setText("Disconnect")
        self.connect_button.setEnabled(True)
        self._set_board_controls(True)

    def _on_disconnected(self, reason: str) -> None:
        self._show_disconnected()
        message = f"Disconnected: {reason}." if reason else "Disconnected."
        self.statusBar().showMessage(message)
        self.append_console(message)

    def _show_disconnected(self) -> None:
        self._connected = False
        self.connect_button.setText("Connect")
        self.connect_button.setEnabled(True)
        self.info_tree.clear()
        self._set_board_controls(False)

    def _set_board_controls(self, enabled: bool) -> None:
        self.port_box.setEnabled(not enabled)
        self.ports_button.setEnabled(not enabled)
        for widget in (
            *self.led_buttons.values(),
            self.refresh_button,
            self.reset_button,
            self.console_input,
        ):
            widget.setEnabled(enabled)
        self.can_panel.set_enabled(enabled)
        self.obd_panel.set_enabled(enabled)
        self.clear_fault_button.setEnabled(False)  # until a crash is shown

    def _on_info(
        self, version: dict[str, str], status: dict[str, str], fault: dict[str, str]
    ) -> None:
        self.info_tree.clear()
        for title, fields in (("Board", version), ("Status", status), ("Last crash", fault)):
            section = QTreeWidgetItem([title])
            for key, value in fields.items():
                section.addChild(QTreeWidgetItem([LABELS.get(key, key), value]))
            self.info_tree.addTopLevelItem(section)
            section.setExpanded(True)
        led = self.led_buttons.get(status.get("led", ""))
        if led is not None:
            led.setChecked(True)
        crashed = fault.get("fault", "none") != "none"
        self.clear_fault_button.setEnabled(crashed)

    def _on_console_rows(self, rows: list[str]) -> None:
        for row in rows:
            self.append_console(row)

    def _on_event(self, text: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self.append_console(f"[{stamp}] event: {text}")
        self.statusBar().showMessage(f"Event: {text}", 5000)

    def _on_failed(self, message: str) -> None:
        self.append_console(f"error: {message}")
        self.statusBar().showMessage(f"Error: {message}")
        if not self._connected:
            self.connect_button.setText("Connect")
            self.connect_button.setEnabled(True)

    def append_console(self, text: str) -> None:
        self.console_view.appendPlainText(text)
        self.console_view.ensureCursorVisible()

    def console_text(self) -> str:
        return self.console_view.toPlainText()

    def info_value(self, section: str, label: str) -> str | None:
        """The value shown for `label` under `section`, or None."""
        for i in range(self.info_tree.topLevelItemCount()):
            top = self.info_tree.topLevelItem(i)
            if top.text(0) != section:
                continue
            for j in range(top.childCount()):
                if top.child(j).text(0) == label:
                    return top.child(j).text(1)
        return None
