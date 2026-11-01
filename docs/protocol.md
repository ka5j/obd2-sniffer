# Serial console and protocol

## In short

The board talks to the laptop over its USB serial port, with a small text protocol made for
programs. The laptop tool (desktop app, command line and shell) sends the commands and does all
the presenting to people, such as labels and tables. The board itself formats nothing for people.
That keeps the firmware small, and there is one way to talk to it, which is the one that gets
tested.

- Every reply ends with `OK` or `ERR <reason>`, and data comes as `key=value` lines.
- Captured CAN frames arrive as compact binary packets between the text lines.
- To type commands yourself, use `obdsniff shell`: it sends them and formats the replies.

A plain serial terminal still shows the start-up report, which helps when the laptop tool isn't
installed or the board won't start.

Serial settings: **1,500,000 baud, 8 data bits, no parity, 1 stop bit**, no flow control. Lines
end with CR, LF or CR LF. (Why this speed: [serial-console.md](design/serial-console.md#speed).)

## Commands

| Command | What it does |
|---|---|
| `help [command]` | List all commands, or show how to use one |
| `version` | Firmware version, build type, chip revision and unique ID, compiler |
| `status` | Uptime, reason for the last reset, clock source and speed, serial statistics |
| `fault [clear]` | Details of the last crash (type, cause, code address, bad memory address), or forget it. A crash is reported after the restart it caused; any later restart forgets it |
| `can` | CAN status: mode, bit rate, bus state, frames received, dropped and sent to the laptop, error counters |
| `can loopback [kbps]` | Start CAN in silent loopback at 125, 250, 500 (default) or 1000 kbit/s: frames the board sends are received internally, nothing reaches the pins |
| `can off` | Stop CAN (CAN is off after every start-up) |
| `can test <count> [per_s]` | Loopback only: send numbered test frames (ID `123`), as fast as the bus allows or at `per_s` frames per second |
| `can stream on\|off` | Whether received frames are sent to the laptop (on after every start-up). Off, they are still received and counted, but not sent: for when the laptop doesn't want them, or to measure what the board can capture without the serial link as the limit |
| `obd` | The board's OBD2 tester: whether it is running, the latest live values (engine speed, vehicle speed, throttle, engine load, coolant and intake temperature, engine run time), warning light, stored trouble codes, VIN, and request counters. A value shows `-` until it has been read |
| `obd start\|stop` | Start or stop the tester. Needs CAN in loopback, where the board also plays the car's engine ECU: the tester's requests come back to it, and its replies back to the tester. `can off` stops the tester |
| `reset` | Restart the board (the reply is sent before it restarts) |
| `crash bus\|usage\|divzero\|hard\|watchdog` | Debug builds only: crash on purpose to test crash capture. See [fault-handling.md](design/fault-handling.md) |

Commands and arguments are not case-sensitive.

## Requests and replies

**Requests** are command lines terminated by CR or LF.

**Replies** are zero or more data lines followed by exactly one final line:

| Final line | Meaning |
|---|---|
| `OK` | The command succeeded |
| `ERR <reason>` | It failed: `unknown command`, `wrong number of arguments (usage: ...)`, `invalid argument`, `too many arguments`, `command failed`, `line too long` (over 96 characters; nothing was run) |

**Data lines** for `version`, `status`, `fault`, `can` and `obd` are `key=value`:

```
> status
< uptime_ms=53211
< reset_cause=reset pin
< clock=HSE 8 MHz bypass
< sysclk_hz=180000000
< watchdog_timeout_ms=500
< last_fault=none
< uart_rx_bytes=7
< uart_rx_dropped=0
< uart_rx_overruns=0
< uart_rx_errors=0
< uart_tx_bytes=1450
< uart_tx_dropped=0
< uart_tx_dma_errors=0
< OK
```

Numbers are plain decimal with the unit in the key (`_ms`, `_hz`, `_bytes`). Text values may
contain spaces, so split each line at the first `=` only. Empty lines produce no reply. Nothing
sent is echoed back.

The laptop tool shows the same reply to people under its own labels, for example in
`obdsniff shell`:

```
obd> status
  Uptime (ms)       53211
  Last reset        reset pin
  Clock source      HSE 8 MHz bypass
  ...
obd> fault blue
error: invalid argument
```

Lines are limited to 96 characters. A longer line is rejected whole with `ERR line too long`. It
is never run cut short, because a shortened command could do something other than what was sent.
Ctrl-C (byte `0x03`) discards whatever has arrived of the current line.

**Events** report things that happen outside a command. They can arrive at any time, so a program
should check every line it reads for a leading `!`:

| Event | Meaning |
|---|---|
| `!started` | Start-up is over and commands can be sent. It is the last line of the start-up report |
| `!button pressed` | The blue USER button was pressed |

## CAN frame packets

While CAN is on, each received frame is sent as a binary packet between the text lines. Text never
contains a zero byte, so a zero marks the start and the end of each packet:

```
0x00 | COBS( type | payload | CRC-16 ) | 0x00
```

- **COBS** (Consistent Overhead Byte Stuffing) rewrites the packet so it contains no zero byte, for
  one extra byte.
- **CRC-16/CCITT-FALSE** (polynomial `0x1021`, initial value `0xFFFF`) covers type and payload, and
  is sent little-endian.
- A packet is written whole, between lines, never inside one.

Frame packet, type `0x01`, all fields little-endian:

| Field | Size | Meaning |
|---|---|---|
| `seq` | 2 | Capture number: counts every frame the controller delivered since `can loopback`, including any the board then had to drop, so a gap means a lost frame. Wraps at 65536 |
| `timestamp_us` | 4 | Receive time in microseconds since start-up; wraps every 71.6 minutes |
| `id` | 4 | Identifier; bit 31 set = 29-bit (extended) ID, bit 30 set = remote frame |
| `dlc` | 1 | Data length code, 0-8 |
| `data` | `dlc` | Data bytes (none for a remote frame) |

A frame with 8 data bytes is 25 bytes on the wire, so the link carries about 5,900 frames per
second, more than a fully loaded 500 kbit/s bus produces. Example: an engine-speed reply from
`7E8`, received at 0x12345678 us, capture number 0x0102:

```
00 0A 01 02 01 78 56 34 12 E8 07 01 07 08 04 41 0C 1A F8 01 01 03 3D 84 00
```

The firmware's encoder (`lib/link/packet.c`) and the laptop tool's decoder
(`host/src/obdsniff/packets.py`) are both tested against this exact sequence.

A program reading the stream should treat a zero as the start of a packet and the next zero as its
end. If a byte is lost, two zeros in a row, or bytes that text never contains, show where the next
packet starts. The laptop tool's `StreamSplitter` does this and counts damaged packets.

## Connecting reliably from a program

A program that stopped half-way through a command may have left part of a line behind. To get to
a known state:

1. Send `Ctrl-C` (byte `0x03`) to discard any partial line.
2. Send `version\r`.
3. Read lines until one is exactly `OK`. Keep any events (lines starting with `!`); anything
   else before the `OK` (for example the end of a start-up report) can be ignored.

After `reset` or a crash, the board restarts and prints its start-up report, which ends with the
`!started` event. Wait for it, then repeat the steps above. Nothing needs to be sent when the
program is finished.

## Engineering detail

The console logic (reading lines, command dispatch and reply formatting) lives in the
portable library (`lib/console/`: `console_core.c`, `cli.c`, `line_reader.c`) and is unit-tested
to 100% MC/DC coverage. The firmware side (`firmware/console/`: `console.c`, `commands.c`) only
moves bytes and supplies the command table. How the bytes move is described in
[design/serial-console.md](design/serial-console.md).
