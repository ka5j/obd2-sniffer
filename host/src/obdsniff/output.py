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
    "can_mode": "Mode",
    "can_bitrate": "Bit rate (bit/s)",
    "can_bus": "Bus state",
    "can_rx_frames": "Received",
    "can_rx_dropped": "  dropped",
    "can_rx_overruns": "  overruns",
    "can_queue": "  queued",
    "can_stream": "Streaming",
    "can_streamed": "Sent to laptop",
    "can_tx_frames": "Transmitted",
    "can_test_frames": "Test frames",
    "can_isr_max_us": "Longest IRQ (us)",
    "can_tec": "TX error count",
    "can_rec": "RX error count",
    "obd_tester": "Tester",
    "obd_ecu": "Simulated ECU",
    "obd_rpm": "Engine (rpm)",
    "obd_speed": "Speed (km/h)",
    "obd_throttle": "Throttle (%)",
    "obd_load": "Engine load (%)",
    "obd_coolant": "Coolant (C)",
    "obd_intake": "Intake air (C)",
    "obd_run_time": "Run time (s)",
    "obd_mil": "Warning light",
    "obd_dtc_count": "Codes stored",
    "obd_dtcs": "Trouble codes",
    "obd_vin": "VIN",
    "obd_requests": "Requests",
    "obd_replies": "  answered",
    "obd_timeouts": "  timed out",
    "obd_errors": "  errors",
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
