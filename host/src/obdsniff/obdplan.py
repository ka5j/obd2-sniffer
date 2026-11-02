"""What the laptop asks the board's OBD2 tester, and when.

The board never asks the car anything on its own (docs/design/safety.md): it
sends one request each time the laptop says `obd ask <what>`. The plan of
questions lives here instead:

  * the VIN, until it has been read (asked again every 5 s if it fails);
  * the stored trouble codes, then the warning light, every 5 s;
  * otherwise each live value in turn.

`ObdReader.step()` keeps the board's tester busy: it reads the latest answers
and, when the tester is free, asks the next question. The board spaces its
requests at least 50 ms apart, so stepping more often doesn't send more.
"""

from __future__ import annotations

import time
from collections.abc import Callable

from obdsniff.device import Device
from obdsniff.sim.obd import PIDS

REFRESH_S = 5.0  # trouble codes and warning light
VIN_RETRY_S = 5.0
# `obd ask` names a live value by its key without the "obd_" prefix.
VALUE_NAMES = tuple(pid.key.removeprefix("obd_") for pid in PIDS)


class ObdPlan:
    def __init__(self, clock: Callable[[], float] = time.monotonic) -> None:
        self._clock = clock
        self._vin_due = self._refresh_due = clock()
        self._status_next = False
        self._value = 0

    def next_question(self, have_vin: bool) -> str:
        now = self._clock()
        if not have_vin and now >= self._vin_due:
            self._vin_due = now + VIN_RETRY_S
            return "vin"
        if now >= self._refresh_due:
            self._refresh_due = now + REFRESH_S
            self._status_next = True
            return "codes"
        if self._status_next:
            self._status_next = False
            return "status"
        name = VALUE_NAMES[self._value]
        self._value = (self._value + 1) % len(VALUE_NAMES)
        return name


class ObdReader:
    """Asks the board's tester the plan's questions, one at a time."""

    def __init__(self, plan: ObdPlan | None = None) -> None:
        self.plan = plan if plan is not None else ObdPlan()

    def step(self, device: Device) -> dict[str, str]:
        """The tester's latest answers; asks the next question if it is free.
        Raises CommandError if the board refuses (for example, CAN is off)."""
        fields = device.obd_status()
        if fields.get("obd_request") == "idle":
            device.obd_ask(self.plan.next_question(fields.get("obd_vin", "-") != "-"))
        return fields
