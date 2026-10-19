"""Human-readable formatting shared by the command line and the shell."""

from __future__ import annotations

# Display names for the board's data keys (the same wording as the board's
# interactive mode).
LABELS: dict[str, str] = {
    "firmware": "Firmware",
    "build": "Build",
    "mcu": "MCU",
    "revision": "Silicon rev",
    "uid": "Unique ID",
    "compiler": "Compiler",
    "uptime_ms": "Uptime (ms)",
    "reset_cause": "Last reset",
    "clock": "Clock source",
    "sysclk_hz": "CPU clock (Hz)",
    "watchdog_timeout_ms": "Watchdog (ms)",
    "last_fault": "Last crash",
    "led": "LED",
    "uart_rx_bytes": "Serial received",
    "uart_rx_dropped": "  dropped",
    "uart_rx_overruns": "  overruns",
    "uart_rx_errors": "  errors",
    "uart_tx_bytes": "Serial sent",
    "uart_tx_dropped": "  dropped",
    "uart_tx_dma_errors": "  DMA errors",
    "fault": "Last crash",
    "cause": "Cause",
    "pc": "At address (PC)",
    "lr": "Called from (LR)",
    "fault_address": "Bad address",
    "crash_uptime_ms": "Crash time (ms)",
    "cfsr": "CFSR",
    "hfsr": "HFSR",
}


def format_fields(fields: dict[str, str]) -> list[str]:
    """Aligned "label  value" rows."""
    labels = {key: LABELS.get(key, key) for key in fields}
    width = max((len(label) for label in labels.values()), default=0) + 2
    return [f"  {labels[key]:<{width}}{value}" for key, value in fields.items()]


def format_reply(lines: list[str]) -> list[str]:
    """Format a command's data lines: aligned rows if they are all key=value,
    otherwise the lines as they are (e.g. `help`)."""
    if lines and all("=" in line for line in lines):
        fields = dict(line.partition("=")[::2] for line in lines)
        return format_fields(fields)
    return list(lines)
