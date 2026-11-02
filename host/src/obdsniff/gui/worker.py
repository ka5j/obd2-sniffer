"""The board connection for the desktop app.

Every request to the board waits for its reply: usually a few milliseconds,
but up to several seconds while the board restarts. The worker owns the
Device and runs on its own thread, so the window never freezes. The window
and the worker only talk through Qt signals, which Qt queues between the two
threads, so nothing is shared and no locks are needed.
"""

from __future__ import annotations

import contextlib
from collections.abc import Callable

from PySide6.QtCore import QObject, QTimer, Signal, Slot

from obdsniff.device import CommandError, Device, DeviceError, open_device
from obdsniff.obdplan import ObdReader
from obdsniff.shell import execute_line

# How often to check for events and frames, and, while reading OBD2, to ask
# the next question: the board spaces requests at least 50 ms apart anyway.
EVENT_POLL_MS = 50


class DeviceWorker(QObject):
    """Runs requests against the board and reports the results as signals."""

    connected = Signal(str)  # port
    disconnected = Signal(str)  # reason; "" when disconnected on request
    info = Signal(object, object, object)  # version, status, fault (dicts)
    console = Signal(object)  # rows for the console pane (list of str)
    event = Signal(str)  # e.g. "button pressed"
    failed = Signal(str)  # a request failed; the connection stays up
    frames = Signal(object, int)  # new CAN frames (list), damaged packets so far
    can_state = Signal(object)  # the board's `can` status (dict)
    obd_state = Signal(object, bool)  # the board's `obd` status (dict), and whether reading

    def __init__(self, opener: Callable[[str | None], Device] = open_device) -> None:
        super().__init__()
        self._opener = opener
        self._device: Device | None = None
        self._reader: ObdReader | None = None  # asks the next question on each poll
        # A child of the worker, so it moves to the worker's thread with it.
        self._timer = QTimer(self)
        self._timer.setInterval(EVENT_POLL_MS)
        self._timer.timeout.connect(self.poll)

    # ---- connection -----------------------------------------------------------

    @Slot(str)
    def open_port(self, port: str) -> None:
        """Connect to a port ("sim" for the simulator, "" to find the board)."""
        self.close_port()
        try:
            self._device = self._opener(port or None)
        except DeviceError as exc:
            self.failed.emit(str(exc))
            return
        self._timer.start()
        self.connected.emit(port)
        self.refresh()

    @Slot()
    def close_port(self) -> None:
        """Disconnect from the board."""
        if self._device is None:
            return
        self._timer.stop()
        device, self._device = self._device, None
        device.close()
        self.disconnected.emit("")

    def _lost(self, device: Device, exc: OSError) -> None:
        """The port stopped working, e.g. the board was unplugged."""
        self._timer.stop()
        self._device = None
        with contextlib.suppress(OSError):
            device.close()
        self.disconnected.emit(f"connection lost: {exc}")

    def _run(self, action: Callable[[Device], None]) -> None:
        """Run one request; turn failures into signals."""
        device = self._device
        if device is None:
            self.failed.emit("not connected")
            return
        try:
            action(device)
        except CommandError as exc:
            self.failed.emit(f"{exc.command}: {exc.reason}")
        except DeviceError as exc:
            self.failed.emit(str(exc))
        except OSError as exc:
            self._lost(device, exc)

    # ---- requests -------------------------------------------------------------

    def _send_obd(self, device: Device) -> None:
        self.obd_state.emit(device.obd_status(), self._reader is not None)

    def _step_obd(self, device: Device, reader: ObdReader) -> None:
        try:
            status = reader.step(device)
        except CommandError:
            self._reader = None  # the board refused, e.g. CAN was switched off
            self._send_obd(device)  # the tab shows that asking has stopped
            raise
        self.obd_state.emit(status, True)

    def _send_info(self, device: Device) -> None:
        self.info.emit(device.version(), device.status(), device.fault())
        self.can_state.emit(device.can_status())
        self._send_obd(device)

    @Slot()
    def refresh(self) -> None:
        self._run(self._send_info)

    @Slot()
    def clear_fault(self) -> None:
        def action(device: Device) -> None:
            device.clear_fault()
            self._send_info(device)

        self._run(action)

    @Slot()
    def reset(self) -> None:
        def action(device: Device) -> None:
            device.reset()
            self.console.emit(["Board restarted."])
            self._send_info(device)

        self._run(action)

    @Slot(str)
    def run_line(self, line: str) -> None:
        """A command typed in the console pane, handled like the shell does."""

        def action(device: Device) -> None:
            self.console.emit(execute_line(device, line))
            self._send_info(device)  # the command may have changed something

        self._run(action)

    @Slot(int)
    def can_loopback(self, kbps: int) -> None:
        def action(device: Device) -> None:
            device.can_loopback(kbps)
            self.can_state.emit(device.can_status())

        self._run(action)

    @Slot()
    def can_off(self) -> None:
        def action(device: Device) -> None:
            device.can_off()
            self.can_state.emit(device.can_status())

        self._run(action)

    @Slot(bool)
    def can_stream(self, on: bool) -> None:
        def action(device: Device) -> None:
            device.can_stream(on)
            self.can_state.emit(device.can_status())

        self._run(action)

    @Slot()
    def obd_start(self) -> None:
        """Start asking the plan's questions, and CAN loopback first if it is off."""

        def action(device: Device) -> None:
            if device.can_status().get("can_mode") != "loopback":
                device.can_loopback()
            self.can_state.emit(device.can_status())
            self._reader = ObdReader()
            self._step_obd(device, self._reader)

        self._run(action)

    @Slot()
    def obd_stop(self) -> None:
        """Stop asking: the board sends nothing more."""

        def action(device: Device) -> None:
            self._reader = None
            self._send_obd(device)

        self._run(action)

    @Slot(int)
    def can_test(self, count: int) -> None:
        self._run(lambda device: device.can_test(count))

    @Slot()
    def poll(self) -> None:
        """New events and CAN frames. Frames go out as one batch per poll,
        not one signal each: there can be thousands a second."""

        def action(device: Device) -> None:
            for event in device.poll_events():
                self.event.emit(event)
            frames = device.poll_frames()
            if frames:
                self.frames.emit(frames, device.bad_packets)
            if self._reader is not None:
                self._step_obd(device, self._reader)

        self._run(action)
