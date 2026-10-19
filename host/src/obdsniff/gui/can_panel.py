"""The desktop app's CAN tab: loopback control, test traffic and live frames.

Like the rest of the window it only displays things and turns clicks into
requests; the DeviceWorker talks to the board.
"""

from __future__ import annotations

from collections import deque

from PySide6.QtCore import (
    QAbstractTableModel,
    QModelIndex,
    QPersistentModelIndex,
    Qt,
    QTimer,
    Signal,
)
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QSpinBox,
    QTableView,
    QVBoxLayout,
    QWidget,
)

from obdsniff.packets import CanFrame, FrameGaps, TimestampUnwrapper

BITRATES_KBPS = (125, 250, 500, 1000)
MAX_ROWS = 500  # newest frames kept in the table

Index = QModelIndex | QPersistentModelIndex


class FrameTableModel(QAbstractTableModel):
    """The newest frames, as text: time, ID, length, data."""

    HEADERS = ("Time (s)", "ID", "Len", "Data")

    def __init__(self, max_rows: int = MAX_ROWS) -> None:
        super().__init__()
        self._rows: deque[tuple[str, str, str, str]] = deque(maxlen=max_rows)

    def add(self, rows: list[tuple[str, str, str, str]]) -> None:
        # Frames arrive in batches of up to a few hundred; redrawing the whole
        # (small) table once per batch is cheaper than row-by-row updates.
        self.beginResetModel()
        self._rows.extend(rows)
        self.endResetModel()

    def clear(self) -> None:
        self.beginResetModel()
        self._rows.clear()
        self.endResetModel()

    def rowCount(self, parent: Index = QModelIndex()) -> int:  # noqa: B008
        return len(self._rows)

    def columnCount(self, parent: Index = QModelIndex()) -> int:  # noqa: B008
        return len(self.HEADERS)

    def data(self, index: Index, role: int = Qt.ItemDataRole.DisplayRole) -> object:
        if role != Qt.ItemDataRole.DisplayRole:
            return None
        return self._rows[index.row()][index.column()]

    def headerData(
        self, section: int, orientation: Qt.Orientation, role: int = Qt.ItemDataRole.DisplayRole
    ) -> object:
        if role != Qt.ItemDataRole.DisplayRole or orientation != Qt.Orientation.Horizontal:
            return None
        return self.HEADERS[section]


def frame_row(frame: CanFrame, seconds: float) -> tuple[str, str, str, str]:
    ident = f"{frame.id:08X}" if frame.extended else f"{frame.id:03X}"
    data = "remote request" if frame.rtr else " ".join(f"{b:02X}" for b in frame.data)
    return (f"{seconds:.6f}", ident, str(frame.length), data)


class CanPanel(QWidget):
    # Requests for the worker.
    loopback_requested = Signal(int)  # kbit/s
    off_requested = Signal()
    test_requested = Signal(int)  # frames

    def __init__(self) -> None:
        super().__init__()
        self._running = False
        self._clock = TimestampUnwrapper()
        self.gaps = FrameGaps()
        self.total = 0
        self.damaged = 0
        self.rate = 0
        self._counted = 0  # total at the last rate update

        self.bitrate_box = QComboBox()
        for kbps in BITRATES_KBPS:
            self.bitrate_box.addItem(f"{kbps} kbit/s", kbps)
        self.bitrate_box.setCurrentIndex(BITRATES_KBPS.index(500))
        self.start_button = QPushButton("Start loopback")
        self.start_button.clicked.connect(self._on_start_clicked)
        self.count_box = QSpinBox()
        self.count_box.setRange(1, 1_000_000)
        self.count_box.setValue(1000)
        self.count_box.setSuffix(" frames")
        self.test_button = QPushButton("Send test frames")
        self.test_button.clicked.connect(lambda: self.test_requested.emit(self.count_box.value()))
        controls = QHBoxLayout()
        for widget in (self.bitrate_box, self.start_button, self.count_box, self.test_button):
            controls.addWidget(widget)
        controls.addStretch(1)

        self.mode_label = QLabel()
        self.stats_label = QLabel()
        self.model = FrameTableModel()
        self.table = QTableView()
        self.table.setModel(self.model)
        self.table.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        rows = self.table.verticalHeader()
        rows.setVisible(False)
        rows.setDefaultSectionSize(self.table.fontMetrics().height() + 6)  # compact rows
        columns = self.table.horizontalHeader()
        for column in range(3):
            columns.setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
        columns.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)

        layout = QVBoxLayout(self)
        layout.addLayout(controls)
        layout.addWidget(self.mode_label)
        layout.addWidget(self.stats_label)
        layout.addWidget(self.table, 1)

        self._rate_timer = QTimer(self)
        self._rate_timer.setInterval(1000)
        self._rate_timer.timeout.connect(self.update_rate)
        self._rate_timer.start()
        self.on_can_state({"can_mode": "off"})
        self.set_enabled(False)
        self._show_stats()

    # ---- from the window and worker -------------------------------------------

    def set_enabled(self, enabled: bool) -> None:
        for widget in (self.bitrate_box, self.start_button, self.count_box):
            widget.setEnabled(enabled)
        self.test_button.setEnabled(enabled and self._running)

    def on_can_state(self, status: dict[str, str]) -> None:
        self._running = status.get("can_mode") == "loopback"
        self.start_button.setText("Stop" if self._running else "Start loopback")
        self.test_button.setEnabled(self._running and self.start_button.isEnabled())
        if self._running:
            kbps = int(status.get("can_bitrate", "0")) // 1000
            self.mode_label.setText(
                f"Loopback at {kbps} kbit/s: the board receives its own frames; "
                "nothing reaches the pins."
            )
        else:
            self.mode_label.setText("CAN is off.")

    def on_frames(self, frames: list[CanFrame], damaged: int) -> None:
        rows = []
        for frame in frames:
            rows.append(frame_row(frame, self._clock.seconds(frame.timestamp_us)))
            self.gaps.add(frame.seq)
        self.model.add(rows[-MAX_ROWS:])
        self.table.scrollToBottom()
        self.total += len(frames)
        self.damaged = damaged
        self._show_stats()

    def update_rate(self) -> None:
        """Frames per second, measured over the last second."""
        self.rate = self.total - self._counted
        self._counted = self.total
        self._show_stats()

    # ---- own actions ------------------------------------------------------------

    def _on_start_clicked(self) -> None:
        if self._running:
            self.off_requested.emit()
        else:
            self.loopback_requested.emit(int(self.bitrate_box.currentData()))

    def _show_stats(self) -> None:
        self.stats_label.setText(
            f"Frames {self.total:,}    Rate {self.rate:,}/s    "
            f"Lost {self.gaps.lost:,}    Damaged {self.damaged:,}"
        )
