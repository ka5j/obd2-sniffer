"""The simulator's OBD2 side: models of the firmware's simulated engine ECU
(lib/obd/ecu_sim.c, ecu_node.c), its tester (obd_client.c), the reply
formats (obd2.c) and the `obd` command in commands.c.

The C code is the reference; this follows it closely enough that the laptop
tool can be built and tested against the simulator: the same requests and
replies on the bus, the same values, the same `obd` output.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field

ISOTP_PADDING = 0xCC
REQUEST_ID = 0x7DF  # tester requests: to every ECU
ECU_REQUEST_ID = 0x7E0  # requests and flow control to the engine ECU
REPLY_ID = 0x7E8  # the engine ECU's replies

GAP_US = 10_000  # obd_client.c: between a reply and the next request
REFRESH_US = 5_000_000  # trouble codes and monitor status
DTC_MAX = 8

CYCLE_MS = 30_000
VIN = "1M8GDM9AXKP042788"
DTCS = (0x0301, 0x0420)  # P0301, P0420
USAGE = "[start|stop]"

# ecu_sim.c: (at_ms, rpm, speed_kmh, throttle in tenths of a percent)
DRIVE = (
    (0, 800, 0, 0),
    (5000, 800, 0, 0),
    (9000, 3200, 50, 450),
    (12000, 2400, 70, 220),
    (20000, 2200, 80, 180),
    (25000, 900, 15, 0),
    (27000, 800, 0, 0),
    (CYCLE_MS, 800, 0, 0),
)


@dataclass(frozen=True)
class Pid:
    """obd2.c's table: a live-data PID whose reply is one number."""

    pid: int
    formula: str
    decimals: int
    key: str
    label: str


PIDS = (
    Pid(0x0C, "rpm", 0, "obd_rpm", "Engine (rpm)"),
    Pid(0x0D, "byte", 0, "obd_speed", "Speed (km/h)"),
    Pid(0x11, "percent", 1, "obd_throttle", "Throttle (%)"),
    Pid(0x04, "percent", 1, "obd_load", "Engine load (%)"),
    Pid(0x05, "temperature", 0, "obd_coolant", "Coolant (C)"),
    Pid(0x0F, "temperature", 0, "obd_intake", "Intake air (C)"),
    Pid(0x1F, "word", 0, "obd_run_time", "Run time (s)"),
)
PID_BY_NUMBER = {p.pid: p for p in PIDS}


def c_div(a: int, b: int) -> int:
    """Integer division as in C: rounds toward zero (Python's // floors)."""
    quotient = abs(a) // abs(b)
    return quotient if (a >= 0) == (b > 0) else -quotient


def clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


# ---- reply formats (obd2.c) -------------------------------------------------------


def encode(pid: Pid, value: int) -> bytes:
    if pid.formula == "percent":
        return bytes([c_div(clamp(value, 0, 1000) * 255 + 500, 1000)])
    if pid.formula == "temperature":
        return bytes([clamp(value, -40, 215) + 40])
    if pid.formula == "rpm":
        return (clamp(value, 0, 16383) * 4).to_bytes(2, "big")
    if pid.formula == "word":
        return clamp(value, 0, 65535).to_bytes(2, "big")
    return bytes([clamp(value, 0, 255)])


def decode(pid: Pid, data: bytes) -> int | None:
    size = 2 if pid.formula in ("rpm", "word") else 1
    if len(data) < size:
        return None
    a = data[0]
    if pid.formula == "percent":
        return c_div(a * 1000 + 127, 255)
    if pid.formula == "temperature":
        return a - 40
    if pid.formula == "rpm":
        return c_div(a * 256 + data[1], 4)
    if pid.formula == "word":
        return a * 256 + data[1]
    return a


def dtc_text(code: int) -> str:
    return "PCBU"[code >> 14] + f"{(code >> 12) & 3}{code & 0xFFF:03X}"


def format_value(pid: Pid, value: int) -> str:
    """As the firmware prints it: tenths for percentages."""
    if pid.decimals == 1:
        return f"{value // 10}.{value % 10}"
    return str(value)


# ---- the simulated engine (ecu_sim.c) -------------------------------------------------


@dataclass(frozen=True)
class EngineState:
    rpm: int
    speed_kmh: int
    throttle: int
    load: int
    coolant_c: int
    intake_c: int
    run_time_s: int

    def value(self, pid: int) -> int:
        return {
            0x0C: self.rpm,
            0x0D: self.speed_kmh,
            0x11: self.throttle,
            0x04: self.load,
            0x05: self.coolant_c,
            0x0F: self.intake_c,
        }.get(pid, self.run_time_s)


def engine_state(elapsed_ms: int) -> EngineState:
    elapsed_ms &= 0xFFFF_FFFF
    t = elapsed_ms % CYCLE_MS
    nxt = next(i for i, frame in enumerate(DRIVE) if frame[0] > t)
    (t0, *a), (t1, *b) = DRIVE[nxt - 1], DRIVE[nxt]
    rpm, speed, throttle = (
        v0 + c_div((v1 - v0) * (t - t0), t1 - t0) for v0, v1 in zip(a, b, strict=True)
    )
    seconds = elapsed_ms // 1000
    coolant = 90 if seconds >= 90 else 20 + c_div(seconds * 70, 90)
    return EngineState(
        rpm=rpm,
        speed_kmh=speed,
        throttle=throttle,
        load=180 + c_div(throttle * 3, 2),
        coolant_c=coolant,
        intake_c=25,
        run_time_s=min(seconds, 65535),
    )


def supported_mask() -> bytes:
    mask = 0x8000_0000 >> (0x01 - 1)  # monitor status
    for pid in PIDS:
        mask |= 0x8000_0000 >> (pid.pid - 1)
    return mask.to_bytes(4, "big")


def ecu_reply(elapsed_ms: int, request: bytes) -> bytes:
    """ecu_sim_reply(): the reply to one request, or b"" for none."""
    if not request:
        return b""
    service = request[0]
    if service == 0x01 and len(request) == 2:
        pid = request[1]
        if pid == 0x00:
            return bytes([0x41, 0x00]) + supported_mask()
        if pid == 0x01:
            return bytes([0x41, 0x01, 0x80 | len(DTCS), 0, 0, 0])
        if pid in PID_BY_NUMBER:
            value = engine_state(elapsed_ms).value(pid)
            return bytes([0x41, pid]) + encode(PID_BY_NUMBER[pid], value)
    elif service == 0x03 and len(request) == 1:
        return bytes([0x43, len(DTCS)]) + b"".join(code.to_bytes(2, "big") for code in DTCS)
    elif service == 0x09 and len(request) == 2:
        if request[1] == 0x00:
            return bytes([0x49, 0x00, 0x40, 0, 0, 0])
        if request[1] == 0x02:
            return bytes([0x49, 0x02, 0x01]) + VIN.encode("ascii")
    return b""


# ---- ISO-TP framing (isotp.c), as the two nodes use it ----------------------------------


def pad(data: bytes) -> bytes:
    return data + bytes([ISOTP_PADDING]) * (8 - len(data))


def single_frame(message: bytes) -> bytes:
    return pad(bytes([len(message)]) + message)


def first_frame(message: bytes) -> bytes:
    return bytes([0x10 | (len(message) >> 8), len(message) & 0xFF]) + message[:6]


def consecutive_frames(message: bytes) -> list[bytes]:
    rest, frames = message[6:], []
    for n in range(0, len(rest), 7):
        frames.append(pad(bytes([0x20 | ((n // 7 + 1) & 0x0F)]) + rest[n : n + 7]))
    return frames


FLOW_CONTROL = pad(bytes([0x30, 0x00, 0x00]))  # continue, no block limit, no gap


# ---- the ECU node and the tester on one bus ---------------------------------------------

Send = Callable[[int, bytes], None]  # (CAN identifier, 8 data bytes)


class SimulatedEcu:
    """ecu_node.c: answers requests on 0x7DF and 0x7E0 from 0x7E8. A reply
    longer than one frame waits for flow control on 0x7E0."""

    def __init__(self) -> None:
        self._waiting: list[bytes] = []  # consecutive frames held until flow control

    def frame(self, ident: int, data: bytes, elapsed_ms: int, send: Send) -> None:
        if ident not in (REQUEST_ID, ECU_REQUEST_ID) or not data:
            return
        kind, low = data[0] >> 4, data[0] & 0x0F
        if kind == 3 and low == 0 and self._waiting:  # flow control: continue
            frames, self._waiting = self._waiting, []
            for cf in frames:
                send(REPLY_ID, cf)
        elif kind == 0 and 0 < low < len(data):
            reply = ecu_reply(elapsed_ms, data[1 : 1 + low])
            if len(reply) > 7:
                # Before sending: on the simulated bus the flow control comes
                # back while send() is still running.
                self._waiting = consecutive_frames(reply)
                send(REPLY_ID, first_frame(reply))
            elif reply:
                send(REPLY_ID, single_frame(reply))


@dataclass
class SimulatedTester:
    """obd_client.c: one request at a time, in the same order, and the
    latest answers."""

    running: bool = False
    values: dict[str, int] = field(default_factory=dict)
    mil_on: bool | None = None
    dtc_reported: int | None = None
    dtcs: list[int] | None = None
    vin: str | None = None
    requests: int = 0
    replies: int = 0
    timeouts: int = 0
    errors: int = 0
    next_request_us: int = 0
    _refresh_due_us: int = 0
    _status_next: bool = False
    _pid_index: int = 0
    _reply: bytearray = field(default_factory=bytearray)
    _expected: int = 0

    def start(self, now_us: int) -> None:
        self.__init__()  # type: ignore[misc]
        self.running = True
        self.next_request_us = self._refresh_due_us = now_us

    def stop(self) -> None:
        self.running = False

    def request(self, now_us: int) -> bytes:
        """The next request in the plan."""
        self.requests += 1
        if self.vin is None:  # asked first; the simulated ECU always answers
            return bytes([0x09, 0x02])
        if now_us >= self._refresh_due_us:
            self._refresh_due_us = now_us + REFRESH_US
            self._status_next = True
            return bytes([0x03])
        if self._status_next:
            self._status_next = False
            return bytes([0x01, 0x01])
        pid = PIDS[self._pid_index].pid
        self._pid_index = (self._pid_index + 1) % len(PIDS)
        return bytes([0x01, pid])

    def frame(self, ident: int, data: bytes, send: Send) -> bool:
        """A frame from the bus. True once a whole reply has arrived."""
        if not self.running or ident != REPLY_ID:
            return False
        kind, low = data[0] >> 4, data[0] & 0x0F
        if kind == 0:
            self._store(bytes(data[1 : 1 + low]))
            return True
        if kind == 1:
            self._expected = (low << 8) | data[1]
            self._reply = bytearray(data[2:8])
            send(ECU_REQUEST_ID, FLOW_CONTROL)
        elif kind == 2:
            self._reply += data[1:8]
            if len(self._reply) >= self._expected:
                self._store(bytes(self._reply[: self._expected]))
                return True
        return False

    def _store(self, reply: bytes) -> None:
        if self._keep(reply):
            self.replies += 1
        else:
            self.errors += 1

    def _keep(self, reply: bytes) -> bool:
        """store_reply(): keep what a reply says. False if it is negative,
        malformed or of a kind the tester never asks for."""
        if len(reply) < 2:
            return False
        service, pid, data = reply[0], reply[1], reply[2:]
        if service == 0x41 and pid == 0x01:
            valid = len(data) >= 4
            self.mil_on = bool(data[0] & 0x80) if valid else None
            self.dtc_reported = data[0] & 0x7F if valid else None
            return valid
        if service == 0x41 and pid in PID_BY_NUMBER:
            value = decode(PID_BY_NUMBER[pid], data)
            if value is None:
                self.values.pop(PID_BY_NUMBER[pid].key, None)
                return False
            self.values[PID_BY_NUMBER[pid].key] = value
            return True
        if service == 0x43:
            valid = len(data) >= 2 * pid
            self.dtcs = [int.from_bytes(data[2 * i : 2 * i + 2], "big") for i in range(pid)]
            self.dtcs = self.dtcs if valid else None
            return valid
        if service == 0x49:
            text = reply[3:]
            valid = len(reply) == 20 and pid == 0x02 and reply[2] == 1
            valid = valid and all(0x21 <= c <= 0x7E for c in text)
            self.vin = text.decode("ascii") if valid else None
            return valid
        return False

    def fields(self, ecu_on: bool, field_out: Callable[[str, str, str], None]) -> None:
        """The `obd` command's output (commands.c)."""
        field_out("obd_tester", "Tester", "running" if self.running else "stopped")
        field_out("obd_ecu", "Simulated ECU", "on" if ecu_on else "off")
        for pid in PIDS:
            value = self.values.get(pid.key)
            field_out(pid.key, pid.label, "-" if value is None else format_value(pid, value))
        mil = "-" if self.mil_on is None else ("on" if self.mil_on else "off")
        field_out("obd_mil", "Warning light", mil)
        field_out(
            "obd_dtc_count",
            "Codes stored",
            "-" if self.dtc_reported is None else str(self.dtc_reported),
        )
        if self.dtcs is None:
            codes = "-"
        else:
            codes = " ".join(dtc_text(code) for code in self.dtcs[:DTC_MAX]) or "none"
        field_out("obd_dtcs", "Trouble codes", codes)
        field_out("obd_vin", "VIN", self.vin or "-")
        field_out("obd_requests", "Requests", str(self.requests))
        field_out("obd_replies", "  answered", str(self.replies))
        field_out("obd_timeouts", "  timed out", str(self.timeouts))
        field_out("obd_errors", "  errors", str(self.errors))
