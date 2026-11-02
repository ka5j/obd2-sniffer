"""The desktop app: `obdsniff gui`.

Needs PySide6 (`pip install -e "host[gui]"`). The window runs on the main
thread; the board connection runs on a second thread so the window stays
responsive while the board is busy or restarting.
"""

from __future__ import annotations

import sys

from PySide6.QtCore import QMetaObject, Qt, QThread
from PySide6.QtWidgets import QApplication
from shiboken6 import delete

from obdsniff.gui.window import MainWindow
from obdsniff.gui.worker import DeviceWorker

__all__ = ["DeviceWorker", "MainWindow", "run"]


def run(port: str | None = None) -> int:
    """Open the window and run until it is closed. Returns the exit code."""
    app = QApplication.instance() or QApplication(sys.argv[:1])
    thread = QThread()
    worker = DeviceWorker()
    worker.moveToThread(thread)
    window = MainWindow(worker, port)
    thread.start()
    window.show()
    code = app.exec()
    # Disconnect on the worker's own thread, wait for that to finish, then
    # stop the thread.
    QMetaObject.invokeMethod(worker, "close_port", Qt.ConnectionType.BlockingQueuedConnection)
    thread.quit()
    thread.wait()
    # Delete the window now, while the application still exists. Left to
    # Python, the signal connections that refer to it can keep it alive until
    # exit, and a window destroyed after the application corrupts memory.
    delete(window)
    return code
