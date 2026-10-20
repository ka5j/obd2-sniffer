"""The desktop app's OBD2 tab: the board's tester and what it reads from the
simulated engine ECU.

Like the rest of the window it only displays things and turns clicks into
requests; the DeviceWorker talks to the board.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from obdsniff.output import LABELS

# Live values, in the board's order: (key, name shown, unit)
VALUES = (
    ("obd_rpm", "Engine speed", "rpm"),
    ("obd_speed", "Vehicle speed", "km/h"),
    ("obd_throttle", "Throttle", "%"),
    ("obd_load", "Engine load", "%"),
    ("obd_coolant", "Coolant", "°C"),
    ("obd_intake", "Intake air", "°C"),
    ("obd_run_time", "Engine run time", "s"),
)
COLUMNS = 4


class ObdPanel(QWidget):
    # Requests for the worker.
    start_requested = Signal()
    stop_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        self._running = False

        self.start_button = QPushButton("Start")
        self.start_button.clicked.connect(self._on_start_clicked)
        self.state_label = QLabel()
        controls = QHBoxLayout()
        controls.addWidget(self.start_button)
        controls.addWidget(self.state_label, 1)

        big = QFont()
        big.setPointSize(big.pointSize() * 2)
        big.setBold(True)
        grid = QGridLayout()
        self.value_labels: dict[str, QLabel] = {}
        for n, (key, name, unit) in enumerate(VALUES):
            tile = QFrame()
            tile.setFrameShape(QFrame.Shape.StyledPanel)
            box = QVBoxLayout(tile)
            caption = QLabel(f"{name} ({unit})")
            value = QLabel("-")
            value.setFont(big)
            value.setAlignment(Qt.AlignmentFlag.AlignRight)
            box.addWidget(caption)
            box.addWidget(value)
            self.value_labels[key] = value
            grid.addWidget(tile, n // COLUMNS, n % COLUMNS)

        self.mil_label = QLabel()
        self.dtc_label = QLabel()
        self.vin_label = QLabel()
        self.vin_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.counters_label = QLabel()

        layout = QVBoxLayout(self)
        layout.addLayout(controls)
        layout.addLayout(grid)
        layout.addWidget(self.mil_label)
        layout.addWidget(self.dtc_label)
        layout.addWidget(self.vin_label)
        layout.addWidget(self.counters_label)
        layout.addStretch(1)
        self.on_obd_state({})
        self.set_enabled(False)

    # ---- from the window and worker ---------------------------------------------

    def set_enabled(self, enabled: bool) -> None:
        self.start_button.setEnabled(enabled)

    def on_obd_state(self, status: dict[str, str]) -> None:
        self._running = status.get("obd_tester") == "running"
        self.start_button.setText("Stop" if self._running else "Start")
        if self._running:
            self.state_label.setText(
                "Asking the simulated engine ECU over CAN loopback, about 100 requests a second."
            )
        else:
            self.state_label.setText(
                "Start runs the board's OBD2 tester; CAN loopback is started if it is off."
            )
        for key, label in self.value_labels.items():
            label.setText(status.get(key, "-"))
        mil = status.get("obd_mil", "-")
        warning = mil == "on"
        self.mil_label.setText(f"{LABELS['obd_mil']}: {mil}")
        self.mil_label.setStyleSheet("color: #cf222e; font-weight: bold;" if warning else "")
        self.dtc_label.setText(f"{LABELS['obd_dtcs']}: {status.get('obd_dtcs', '-')}")
        self.vin_label.setText(f"{LABELS['obd_vin']}: {status.get('obd_vin', '-')}")
        self.counters_label.setText(
            f"Requests {status.get('obd_requests', '0')}, "
            f"answered {status.get('obd_replies', '0')}, "
            f"timed out {status.get('obd_timeouts', '0')}, "
            f"errors {status.get('obd_errors', '0')}"
        )

    # ---- own actions ----------------------------------------------------------------

    def _on_start_clicked(self) -> None:
        if self._running:
            self.stop_requested.emit()
        else:
            self.start_requested.emit()
