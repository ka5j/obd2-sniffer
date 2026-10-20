"""The simulator's CAN controller: a model of firmware/src/can_drv.c, can_gen.c
and the `can` command in commands.c, in silent loopback.

Frames are produced as the board's output is read, a batch at a time, with
timestamps spaced the way they would be on a real bus.
"""

from __future__ import annotations

import struct
from collections.abc import Callable

from obdsniff.packets import CanFrame, format_frame
from obdsniff.sim_obd import GAP_US, REQUEST_ID, SimulatedEcu, SimulatedTester, single_frame

APB1_HZ = 45_000_000  # the board's CAN clock
QUEUE_FRAMES = 409  # 8 KiB of 20-byte frames, as on the board
GEN_ID = 0x123
GEN_MAX_COUNT = 10_000_000
CAN_ID_STD_MAX = 0x7FF
CAN_ID_EXT_MAX = 0x1FFF_FFFF
USAGE = "[loopback [kbps] | off | send <id> [data] | test <count> [per_s] | stream on|off]"


def bit_timing_exists(clock_hz: int, bitrate: int) -> bool:
    """Whether lib/src/can_timing.c finds exact settings for `bitrate`."""
    if bitrate == 0 or bitrate > clock_hz // 8:
        return False
    for tq in range(25, 7, -1):
        quanta = bitrate * tq
        if clock_hz % quanta == 0 and clock_hz // quanta <= 1024:
            before_sample = (tq * 875 + 500) // 1000
            if before_sample - 1 <= 16:
                return True
    return False


def parse_u32(text: str) -> int | None:
    """lib/src/cli.c cli_parse_u32: decimal, or hex with a 0x prefix."""
    digits, base = (text[2:], 16) if text[:2] == "0x" else (text, 10)
    valid = "0123456789abcdef"[:base]
    if not digits or any(c not in valid for c in digits):
        return None
    value = int(digits, base)
    return value if value <= 0xFFFF_FFFF else None


def parse_hex(text: str) -> int | None:
    """cli_parse_hex_u32: 1-8 hex digits, no prefix."""
    if not 1 <= len(text) <= 8 or any(c not in "0123456789abcdef" for c in text):
        return None
    return int(text, 16)


def parse_hex_bytes(text: str) -> bytes | None:
    """cli_parse_hex_bytes: pairs of hex digits, 1-8 bytes."""
    if not text or len(text) % 2 or len(text) > 16:
        return None
    if any(c not in "0123456789abcdef" for c in text):
        return None
    return bytes.fromhex(text)


def frame_bits(frame: CanFrame) -> int:
    """Approximate bits a frame occupies on the bus, including stuff bits
    and the gap after it (measured on the board: about 119 for 8 bytes)."""
    return 55 + (20 if frame.extended else 0) + 8 * len(frame.data)


def format_line(frame: CanFrame) -> str:
    """The board's text form of a frame (lib/src/can_text.c)."""
    return format_frame(frame) + "\r\n"


class SimulatedCan:
    def __init__(self, clock: Callable[[], float]) -> None:
        self._clock = clock
        self.reset()

    def reset(self) -> None:
        """Start-up state: CAN off."""
        self.mode = "off"
        self.stream = True  # frames go to the terminal or laptop
        self.bitrate = 0
        self.rx_frames = 0
        self.tx_frames = 0
        self.streamed = 0
        self.gen_sent = 0
        self.gen_count = 0
        self.gen_running = False
        self._seq = 0
        self._pending: list[CanFrame] = []  # sent but not yet received
        self._gen_spacing_us = 0
        self._bus_time_us = 0
        self._boot_us = self._now_us()  # the simulated ECU's clock is the uptime
        self.ecu = SimulatedEcu()
        self.tester = SimulatedTester()

    # ---- the `can` command ----------------------------------------------------

    def command(self, args: list[str], field: Callable[[str, str, str], None]) -> str | None:
        """Run `can <args>`; returns an error message, or None for OK."""
        if not args:
            self._status(field)
            return None
        sub, rest = args[0], args[1:]
        if sub == "loopback":
            return self._loopback(rest)
        if sub == "off":
            if rest:
                return f"wrong number of arguments (usage: can {USAGE})"
            self.mode, self.bitrate, self.gen_running = "off", 0, False
            self.tester.stop()
            return None
        if sub == "send":
            return self._send(rest)
        if sub == "test":
            return self._test(rest)
        if sub == "stream":
            return self._stream(rest)
        return "invalid argument"

    def _status(self, field: Callable[[str, str, str], None]) -> None:
        on = self.mode == "loopback"
        field("can_mode", "Mode", self.mode)
        field("can_bitrate", "Bit rate (bit/s)", str(self.bitrate))
        field("can_bus", "Bus state", "error-active" if on else "off")
        field("can_rx_frames", "Received", str(self.rx_frames))
        field("can_rx_dropped", "  dropped", "0")
        field("can_rx_overruns", "  overruns", "0")
        field("can_queue", "  queued", f"0/{QUEUE_FRAMES}")
        field("can_stream", "Streaming", "on" if self.stream else "off")
        field("can_streamed", "Sent to laptop", str(self.streamed))
        field("can_tx_frames", "Transmitted", str(self.tx_frames))
        field("can_test_frames", "Test frames", f"{self.gen_sent}/{self.gen_count}")
        field("can_isr_max_us", "Longest IRQ (us)", "0")
        field("can_tec", "TX error count", "0")
        field("can_rec", "RX error count", "0")

    def _loopback(self, rest: list[str]) -> str | None:
        if len(rest) > 1:
            return f"wrong number of arguments (usage: can {USAGE})"
        kbps = parse_u32(rest[0]) if rest else 500
        if kbps is None or not 0 < kbps <= 1000 or not bit_timing_exists(APB1_HZ, kbps * 1000):
            return "invalid argument"
        # As can_drv_start_loopback(): fresh counters and capture numbers;
        # a running test series stops (its counts stay); frames already
        # received are still delivered.
        self.mode, self.bitrate = "loopback", kbps * 1000
        self.rx_frames = self.tx_frames = self._seq = 0
        self.gen_running = False
        self._bus_time_us = self._now_us()
        return None

    def _send(self, rest: list[str]) -> str | None:
        if not 1 <= len(rest) <= 2:
            return f"wrong number of arguments (usage: can {USAGE})"
        ident = parse_hex(rest[0])
        data = parse_hex_bytes(rest[1]) if len(rest) == 2 else b""
        if ident is None or ident > CAN_ID_EXT_MAX or data is None:
            return "invalid argument"
        if self.mode != "loopback":
            return "command failed"
        self._transmit(ident, data, extended=ident > CAN_ID_STD_MAX)
        return None

    def _test(self, rest: list[str]) -> str | None:
        if not 1 <= len(rest) <= 2:
            return f"wrong number of arguments (usage: can {USAGE})"
        count = parse_u32(rest[0])
        per_s = parse_u32(rest[1]) if len(rest) == 2 else 0
        if count is None or per_s is None:
            return "invalid argument"
        if self.mode != "loopback":
            return "command failed"
        if not 0 < count <= GEN_MAX_COUNT or per_s > 1_000_000:
            return "invalid argument"
        self.gen_sent, self.gen_count, self.gen_running = 0, count, True
        self._gen_spacing_us = 0 if per_s == 0 else 1_000_000 // per_s
        self._bus_time_us = max(self._bus_time_us, self._now_us())
        return None

    def _stream(self, rest: list[str]) -> str | None:
        if len(rest) != 1:
            return f"wrong number of arguments (usage: can {USAGE})"
        if rest[0] not in ("on", "off"):
            return "invalid argument"
        self.stream = rest[0] == "on"
        return None

    # ---- frames ---------------------------------------------------------------

    def _now_us(self) -> int:
        return int(self._clock() * 1_000_000)  # frame timestamps wrap; this doesn't

    def _transmit(self, ident: int, data: bytes, extended: bool = False, spacing: int = 0) -> None:
        frame = CanFrame(timestamp_us=0, id=ident, data=data, extended=extended)
        self._bus_time_us += max(frame_bits(frame) * 1_000_000 // self.bitrate, spacing)
        self.tx_frames += 1
        self._pending.append(
            CanFrame(
                timestamp_us=self._bus_time_us & 0xFFFF_FFFF,
                id=ident,
                data=data,
                dlc=len(data),
                extended=extended,
                seq=self._seq & 0xFFFF,
            )
        )
        self._seq += 1
        self.rx_frames += 1
        if not extended:
            # In loopback the board receives its own frames, so they reach its
            # OBD2 tester and simulated ECU too (firmware/src/obd.c).
            elapsed_ms = (self._bus_time_us - self._boot_us) // 1000
            self.ecu.frame(ident, data, elapsed_ms, self._transmit)
            self.tester.frame(ident, data, self._transmit)

    def run_obd(self) -> None:
        """The tester's requests up to now, each answered at once by the
        simulated ECU; the next request follows 10 ms after the reply."""
        now = self._now_us()
        while self.tester.running and self.tester.next_request_us <= now:
            self._bus_time_us = max(self._bus_time_us, self.tester.next_request_us)
            self._transmit(REQUEST_ID, single_frame(self.tester.request(self._bus_time_us)))
            self.tester.next_request_us = self._bus_time_us + GAP_US

    def obd_start(self) -> bool:
        if self.mode != "loopback":
            return False
        self.tester.start(self._now_us())
        return True

    def take(self, limit: int) -> list[CanFrame]:
        """Frames received since the last call, to be sent (test traffic is
        produced up to `limit` frames at a time). With streaming off they
        are received and then dropped, as can_stream.c does."""
        self.run_obd()
        while self.gen_running and len(self._pending) < limit:
            n = self.gen_sent
            data = struct.pack("<II", n, ~n & 0xFFFF_FFFF)
            self._transmit(GEN_ID, data, spacing=self._gen_spacing_us)
            self.gen_sent += 1
            self.gen_running = self.gen_sent < self.gen_count
        frames, self._pending = self._pending[:limit], self._pending[limit:]
        if not self.stream:
            return []
        self.streamed += len(frames)
        return frames

    def busy(self) -> bool:
        """Frames still to come from the current traffic."""
        return bool(self._pending) or self.gen_running
