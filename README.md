# OBD2 CAN Sniffer — STM32F446RE

A CAN bus sniffer and OBD2 diagnostic tool built on the STM32F446RE (Nucleo-64),
with a Python host tool and live dashboard.

> 🚧 Work in progress — built in incremental, releasable phases. See the roadmap below.

## Planned features
- **Passive CAN sniffer** — listen-only mode, µs timestamps, hardware filters, binary COBS/CRC16 link to the PC
- **OBD2 diagnostics** — live PIDs (SAE J1979), ISO-TP multi-frame, VIN, read/clear DTCs
- **Safety-by-design** — silent boot, guarded transmit path, bus-health fallback, watchdog, FMEA + fault-injection tests
- **Low power** — STOP mode when the bus is idle, wake on CAN activity
- **Python dashboard** — live plots, CAN ID monitor, CSV / candump export, replay
- **Custom PCB** — KiCad shield with protected 12 V input (final phase)

## Roadmap
| Release | Phase | Status |
|---|---|---|
| v0.1.0 | Bring-up: toolchain, clocks, LED, UART | ⏳ |
| v0.2.0 | Console + board services (CLI, watchdog, fault capture) | ⏳ |
| v0.3.0 | CAN loopback + binary link to PC | ⏳ |
| v0.4.0 | OBD2 over loopback (ISO-TP, PIDs, VIN, DTCs) | ⏳ |
| v0.5.0 | Two-node single-wire CAN bus + throughput benchmark | ⏳ |
| v0.6.0 | Safety-by-design + fault injection | ⏳ |
| v0.7.0 | Python dashboard | ⏳ |
| v0.8.0 | Low power (STOP + wake-on-CAN) | ⏳ |
| v0.9.0 | Transceivers + real vehicle | ⏳ |
| v1.0.0 | Portfolio polish | ⏳ |
| v1.1.0 | Custom PCB | ⏳ |

## License
MIT
