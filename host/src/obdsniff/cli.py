"""The `obdsniff` command line."""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections.abc import Callable

from obdsniff import __version__
from obdsniff.device import CommandError, Device, DeviceError, open_device
from obdsniff.output import format_fields
from obdsniff.ports import list_serial_ports
from obdsniff.shell import run_shell

EXIT_OK = 0
EXIT_COMMAND_FAILED = 1
EXIT_NO_CONNECTION = 2


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
    monitor = sub.add_parser("monitor", help="print events (button presses) as they happen")
    monitor.add_argument("--duration", type=float, help="stop after this many seconds")
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


def cmd_monitor(device: Device, args: argparse.Namespace) -> int:
    if not args.json:
        print("Watching for events. Press Ctrl-C to stop.")
    end = None if args.duration is None else time.monotonic() + args.duration
    try:
        while end is None or time.monotonic() < end:
            for event in device.poll_events(timeout=0.2):
                stamp = time.strftime("%H:%M:%S")
                if args.json:
                    print(json.dumps({"time": stamp, "event": event}), flush=True)
                else:
                    print(f"[{stamp}] {event}", flush=True)
    except KeyboardInterrupt:
        pass
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
