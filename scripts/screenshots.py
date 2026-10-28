#!/usr/bin/env python3
"""Take the desktop app's screenshots in docs/images/ from a connected board.

Drives the real window: clicks its buttons, then waits until the widgets show
the expected state (or a deadline passes) before grabbing the window. Fixed
delays are not used: on macOS, Qt timers sometimes fired all at once.

Needs the laptop tool with the desktop app (`pip install -e "host[gui]"`).
Close any serial terminal first: only one program can hold the port.

Usage:
    python3 scripts/screenshots.py [--port PORT] [--out DIR] [console] [can] [obd]

console runs `crash bus`, so it needs a Debug build; can and obd work with
either build. With no names, all three are taken.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections.abc import Callable
from pathlib import Path

from obdsniff.gui.window import MainWindow
from obdsniff.gui.worker import DeviceWorker
from PySide6.QtWidgets import QApplication

ROOT = Path(__file__).resolve().parents[1]
POLL_S = 0.1  # how often the app polls the board when it runs normally
# About 2.4 s at full load at 500 kbit/s: long enough for the once-a-second
# rate display to show the bus rate.
CAN_TEST_FRAMES = 10_000
OBD_REQUESTS = 500  # about 5 s of the tester
SHOTS = ("console", "can", "obd")


class Shooter:
    def __init__(self, port: str | None, out: Path) -> None:
        self.app = QApplication.instance() or QApplication(sys.argv[:1])
        # The worker stays on this thread, so waiting is a plain loop of
        # poll() and processEvents() rather than a second thread.
        self.worker = DeviceWorker()
        self.window = MainWindow(self.worker, port)
        self.out = out

    def wait_until(self, what: str, done: Callable[[], bool], timeout: float = 20.0) -> None:
        deadline = time.monotonic() + timeout
        while not done():
            if time.monotonic() > deadline:
                raise TimeoutError(f"timed out waiting for {what}")
            self.worker.poll()
            self.app.processEvents()
            time.sleep(POLL_S)
        self.app.processEvents()

    def save(self, name: str) -> None:
        path = self.out / name
        self.window.grab().save(str(path))
        print(f"saved {path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}")

    def connect(self) -> None:
        self.window.show()
        self.window.connect_button.click()
        self.wait_until("the connection", lambda: self.window.connect_button.text() == "Disconnect")

    def type_line(self, line: str) -> None:
        self.window.console_input.setText(line)
        self.window.console_input.returnPressed.emit()

    def console(self) -> None:
        """`help`, then a deliberate crash: the board restarts and reports it."""
        self.window.tabs.setCurrentIndex(0)
        self.type_line("help")
        self.wait_until("the help text", lambda: "crash bus" in self.window.console_text())
        self.type_line("crash bus")
        self.wait_until(
            "the crash report",
            lambda: self.window.info_value("Last crash", "Last crash") == "BusFault",
        )
        self.save("desktop-app.png")

    def can(self) -> None:
        """Loopback at 500 kbit/s and a run of numbered test frames."""
        panel = self.window.can_panel
        self.window.tabs.setCurrentWidget(panel)
        panel.start_button.click()
        self.wait_until("CAN loopback", lambda: panel.test_button.isEnabled())
        panel.count_box.setValue(CAN_TEST_FRAMES)
        panel.test_button.click()
        self.wait_until("the test frames", lambda: panel.total >= CAN_TEST_FRAMES)
        self.save("desktop-app-can.png")
        panel.start_button.click()  # CAN off again
        self.wait_until("CAN off", lambda: not panel.test_button.isEnabled())

    def obd(self) -> None:
        """The board's OBD2 tester against its simulated engine ECU."""
        panel = self.window.obd_panel

        def requests() -> int:
            text = panel.counters_label.text()  # "Requests 592, answered 592, ..."
            return int(text.split(",")[0].split()[-1])

        self.window.tabs.setCurrentWidget(panel)
        panel.start_button.click()
        self.wait_until("OBD2 replies", lambda: requests() >= OBD_REQUESTS)
        self.save("desktop-app-obd.png")
        panel.start_button.click()
        self.wait_until("the tester to stop", lambda: panel.start_button.text() == "Start")
        self.worker.can_off()

    def close(self) -> None:
        self.worker.close_port()  # hands the board back in interactive mode
        self.window.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial port (default: find the board)")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "images")
    parser.add_argument("shots", nargs="*", metavar="SHOT", help="console, can, obd (default: all)")
    args = parser.parse_args()
    shots = args.shots or list(SHOTS)
    unknown = sorted(set(shots) - set(SHOTS))
    if unknown:
        parser.error(f"unknown screenshot: {', '.join(unknown)} (choose from {', '.join(SHOTS)})")

    shooter = Shooter(args.port, args.out)
    try:
        shooter.connect()
        for shot in shots:
            getattr(shooter, shot)()
    finally:
        shooter.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
