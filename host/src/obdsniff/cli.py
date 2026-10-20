"""The `obdsniff` command line."""

from __future__ import annotations

import argparse
import contextlib
import json
import sys
import time
from collections.abc import Callable
from typing import TextIO

from obdsniff import __version__
from obdsniff.cantest import describe, run_test
from obdsniff.device import CommandError, Device, DeviceError, open_device
from obdsniff.output import format_fields
from obdsniff.packets import (
    CanFrame,
    FrameGaps,
    TimestampUnwrapper,
    candump_line,
    format_frame,
    frame_to_json,
)
from obdsniff.ports import list_serial_ports
from obdsniff.shell import run_shell

EXIT_OK = 0
EXIT_COMMAND_FAILED = 1
EXIT_NO_CONNECTION = 2


def hex_int(text: str) -> int:
    try:
        return int(text, 16)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a hex number: {text!r}") from None


def hex_bytes(text: str) -> bytes:
    try:
        return bytes.fromhex(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not hex bytes: {text!r}") from None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="obdsniff",
        description="Laptop tool for the OBD2 CAN Sniffer board.",
        epilog="Use --port sim to try everything without the board.",
    )
    parser.add_argument(
        "--port", help="serial port of the board, or 'sim' for the simulator (default: find it)"
    )
    parser.add_argument("--json", action="store_true", help="machine-readable JSON output")
    parser.add_argument("--version", action="version", version=f"obdsniff {__version__}")

    sub = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    sub.add_parser("ports", help="list serial ports and show which one is the board")
    sub.add_parser("info", help="firmware version and board status")
    sub.add_parser("version", help="firmware version and chip identity")
    sub.add_parser("status", help="uptime, last reset, clocks and serial statistics")
    led = sub.add_parser("led", help="control the green LED")
    led.add_argument("mode", choices=["on", "off", "auto"])
    fault = sub.add_parser("fault", help="details of the last crash")
    fault.add_argument("--clear", action="store_true", help="forget the recorded crash")
    sub.add_parser("reset", help="restart the board")
    crash = sub.add_parser("crash", help="crash the board on purpose (debug firmware, for testing)")
    crash.add_argument("kind", choices=["bus", "usage", "divzero", "hard", "watchdog"])
    can = sub.add_parser("can", help="CAN bus: status, loopback test mode, frames, test traffic")
    can_sub = can.add_subparsers(dest="can_command", metavar="ACTION")
    loopback = can_sub.add_parser(
        "loopback", help="start silent loopback: the board receives its own frames"
    )
    loopback.add_argument(
        "--bitrate",
        type=int,
        default=500,
        metavar="KBPS",
        help="125, 250, 500 or 1000 (default 500)",
    )
    can_sub.add_parser("off", help="stop CAN")
    send = can_sub.add_parser("send", help="send one frame (loopback only)")
    send.add_argument("id", type=hex_int, help="identifier in hex, e.g. 7DF; above 7FF is 29-bit")
    send.add_argument(
        "data", nargs="?", type=hex_bytes, default=b"", help="data in hex, e.g. 0201050000000000"
    )
    stream = can_sub.add_parser(
        "stream", help="send received frames to the laptop (on), or only count them (off)"
    )
    stream.add_argument("state", choices=["on", "off"])
    test = can_sub.add_parser(
        "test", help="send numbered frames in loopback and check that every one arrives"
    )
    test.add_argument("--count", type=int, default=10000, help="frames to send (default 10000)")
    test.add_argument(
        "--rate",
        type=int,
        default=0,
        metavar="PER_S",
        help="frames per second (default: bus speed)",
    )
    test.add_argument(
        "--bitrate", type=int, default=500, metavar="KBPS", help="if loopback isn't running yet"
    )
    obd = sub.add_parser("obd", help="OBD2: live data, trouble codes and VIN (simulated ECU)")
    obd_sub = obd.add_subparsers(dest="obd_command", metavar="ACTION")
    obd_sub.add_parser("start", help="start the board's OBD2 tester (CAN must be in loopback)")
    obd_sub.add_parser("stop", help="stop the tester")
    watch = obd_sub.add_parser(
        "watch", help="start loopback and the tester if needed, and show the values live"
    )
    watch.add_argument("--duration", type=float, help="stop after this many seconds")
    watch.add_argument(
        "--interval", type=float, default=0.5, help="seconds between updates (default 0.5)"
    )
    monitor = sub.add_parser("monitor", help="print CAN frames and events as they happen")
    monitor.add_argument("--duration", type=float, help="stop after this many seconds")
    monitor.add_argument(
        "--log", metavar="FILE", help="also save frames in candump format (SavvyCAN, can-utils)"
    )
    sub.add_parser("shell", help="interactive session with the board")
    sub.add_parser("gui", help="open the desktop app")
    return parser


def emit(data: object, as_json: bool, text: list[str]) -> None:
    if as_json:
        print(json.dumps(data, indent=2))
    else:
        for line in text:
            print(line)


# ---- commands that need a board ---------------------------------------------


def cmd_info(device: Device, args: argparse.Namespace) -> int:
    version, status = device.version(), device.status()
    text = ["Board", *format_fields(version), "", "Status", *format_fields(status)]
    emit({"version": version, "status": status}, args.json, text)
    return EXIT_OK


def cmd_version(device: Device, args: argparse.Namespace) -> int:
    fields = device.version()
    emit(fields, args.json, format_fields(fields))
    return EXIT_OK


def cmd_status(device: Device, args: argparse.Namespace) -> int:
    fields = device.status()
    emit(fields, args.json, format_fields(fields))
    return EXIT_OK


def cmd_led(device: Device, args: argparse.Namespace) -> int:
    device.set_led(args.mode)
    emit({"led": args.mode}, args.json, [f"LED set to {args.mode}."])
    return EXIT_OK


def cmd_fault(device: Device, args: argparse.Namespace) -> int:
    if args.clear:
        device.clear_fault()
        emit({"cleared": True}, args.json, ["Crash record cleared."])
        return EXIT_OK
    fields = device.fault()
    emit(fields, args.json, format_fields(fields))
    return EXIT_OK


def cmd_reset(device: Device, args: argparse.Namespace) -> int:
    device.reset()
    emit({"reset": True}, args.json, ["Board restarted."])
    return EXIT_OK


def cmd_crash(device: Device, args: argparse.Namespace) -> int:
    device.crash(args.kind)
    fields = device.fault()
    emit(
        fields,
        args.json,
        ["Board restarted after the crash. What it recorded:", *format_fields(fields)],
    )
    return EXIT_OK


def cmd_can(device: Device, args: argparse.Namespace) -> int:
    action = args.can_command
    if action == "test":
        report = run_test(device, args.count, args.rate, args.bitrate)
        bitrate = int(device.can_status().get("can_bitrate", "0")) // 1000
        if args.json:
            print(json.dumps({**vars(report), "lost": report.lost, "passed": report.passed}))
        else:
            print("\n".join(describe(report, f"{bitrate} kbit/s", args.rate)))
        return EXIT_OK if report.passed else EXIT_COMMAND_FAILED
    if action == "loopback":
        device.can_loopback(args.bitrate)
        message = f"CAN in loopback at {args.bitrate} kbit/s."
    elif action == "off":
        device.can_off()
        message = "CAN off."
    elif action == "send":
        device.can_send(args.id, args.data)
        message = "Frame sent."
    elif action == "stream":
        device.can_stream(args.state == "on")
        message = f"Streaming {args.state}."
    else:
        fields = device.can_status()
        emit(fields, args.json, format_fields(fields))
        return EXIT_OK
    emit({"done": action}, args.json, [message])
    return EXIT_OK


class FrameOutput:
    """Prints frames and events for `monitor`, and writes the optional log."""

    def __init__(self, as_json: bool, log: TextIO | None) -> None:
        self._as_json = as_json
        self._log = log
        self._clock = TimestampUnwrapper()
        self._epoch: float | None = None  # wall-clock time of the board's time zero
        self.gaps = FrameGaps()
        self.frames = 0

    def show(self, frames: list[CanFrame], events: list[str]) -> None:
        lines = []
        log_lines = []
        for event in events:
            stamp = time.strftime("%H:%M:%S")
            lines.append(
                json.dumps({"time": stamp, "event": event})
                if self._as_json
                else f"[{stamp}] {event}"
            )
        for frame in frames:
            seconds = self._clock.seconds(frame.timestamp_us)
            if self._epoch is None:
                self._epoch = time.time() - seconds
            lines.append(
                json.dumps(frame_to_json(frame, seconds))
                if self._as_json
                else format_frame(frame, seconds)
            )
            if self._log is not None:
                log_lines.append(candump_line(frame, self._epoch + seconds))
            self.gaps.add(frame.seq)
        self.frames += len(frames)
        if lines:  # one write per batch: thousands of frames a second
            sys.stdout.write("\n".join(lines) + "\n")
            sys.stdout.flush()
        if log_lines and self._log is not None:
            self._log.write("\n".join(log_lines) + "\n")


CLEAR_SCREEN = "\x1b[H\x1b[J"  # cursor to the top left, clear to the end of the screen


def watch_obd(device: Device, args: argparse.Namespace) -> int:
    """Live view. Starts what it needs, and leaves streaming as it found it."""
    can = device.can_status()
    if can.get("can_mode") != "loopback":
        device.can_loopback()
    streaming = can.get("can_stream") != "off"
    device.can_stream(False)  # the values matter here, not ~200 frames a second
    device.obd_start()
    redraw = sys.stdout.isatty() and not args.json
    end = None if args.duration is None else time.monotonic() + args.duration
    try:
        while end is None or time.monotonic() < end:
            fields = device.obd_status()
            if args.json:
                print(json.dumps(fields), flush=True)
            else:
                text = ["OBD2 live data from the board. Press Ctrl-C to stop.", ""]
                text += format_fields(fields)
                print((CLEAR_SCREEN if redraw else "") + "\n".join(text) + "\n", flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    device.obd_stop()
    device.can_stream(streaming)
    return EXIT_OK


def cmd_obd(device: Device, args: argparse.Namespace) -> int:
    action = args.obd_command
    if action == "watch":
        return watch_obd(device, args)
    if action == "start":
        device.obd_start()
        message = "OBD2 tester started."
    elif action == "stop":
        device.obd_stop()
        message = "OBD2 tester stopped."
    else:
        fields = device.obd_status()
        emit(fields, args.json, format_fields(fields))
        return EXIT_OK
    emit({"done": action}, args.json, [message])
    return EXIT_OK


def cmd_monitor(device: Device, args: argparse.Namespace) -> int:
    device.can_stream(True)  # the frames have to reach the laptop
    if not args.json:
        print("Watching for CAN frames and events. Press Ctrl-C to stop.", flush=True)
    end = None if args.duration is None else time.monotonic() + args.duration
    with contextlib.ExitStack() as stack:
        log = stack.enter_context(open(args.log, "w", encoding="ascii")) if args.log else None
        output = FrameOutput(args.json, log)
        try:
            while end is None or time.monotonic() < end:
                frames = device.poll_frames(timeout=0.1)
                output.show(frames, device.poll_events())
        except KeyboardInterrupt:
            pass
    if output.frames:
        saved = f"; saved to {args.log}" if args.log else ""
        print(
            f"{output.frames} frames, {output.gaps.lost} lost, "
            f"{device.bad_packets} damaged{saved}.",
            file=sys.stderr,
        )
    return EXIT_OK


def cmd_shell(device: Device, args: argparse.Namespace) -> int:
    return run_shell(device)


HANDLERS: dict[str, Callable[[Device, argparse.Namespace], int]] = {
    "info": cmd_info,
    "version": cmd_version,
    "status": cmd_status,
    "led": cmd_led,
    "fault": cmd_fault,
    "reset": cmd_reset,
    "crash": cmd_crash,
    "can": cmd_can,
    "obd": cmd_obd,
    "monitor": cmd_monitor,
    "shell": cmd_shell,
}


# ---- commands that don't ----------------------------------------------------


def cmd_ports(args: argparse.Namespace) -> int:
    ports = list_serial_ports()
    if args.json:
        print(json.dumps([vars(p) for p in ports], indent=2))
    elif not ports:
        print("No serial ports found.")
    else:
        for port in ports:
            marker = "  <- Nucleo board" if port.is_board else ""
            print(f"{port.device}  {port.description}{marker}")
    return EXIT_OK


def cmd_gui(args: argparse.Namespace) -> int:
    """The desktop app manages its own connections; --port preselects one."""
    try:
        from obdsniff import gui
    except ImportError as exc:  # PySide6, or a library it needs, is missing
        print(
            f"error: the desktop app could not start ({exc}).\n"
            'Install it with: pip install -e "host[gui]"',
            file=sys.stderr,
        )
        return EXIT_NO_CONNECTION
    return gui.run(args.port)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "ports":
        return cmd_ports(args)
    if args.command == "gui":
        return cmd_gui(args)
    try:
        with open_device(args.port) as device:
            return HANDLERS[args.command](device, args)
    except CommandError as exc:
        print(f"error: {exc.reason}", file=sys.stderr)
        return EXIT_COMMAND_FAILED
    except DeviceError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_NO_CONNECTION
