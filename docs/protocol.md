# Serial console and protocol

## In short

The board has a text console on its USB serial port. You can type commands into any serial
terminal, or let the laptop tool drive it. The same commands work both ways; only the output
format changes:

- **Interactive mode** (the default) is made for people: there's a prompt, what you type is
  echoed, and results are shown as an aligned table.
- **Host mode** is made for programs: nothing is echoed, every reply ends with `OK` or
  `ERR <reason>`, and data comes as `key=value` lines that are easy to parse.

Captured CAN frames follow the same rule: a readable line each in interactive mode, a compact
binary packet each in host mode.

Serial settings: **1,500,000 baud, 8 data bits, no parity, 1 stop bit**, no flow control. Lines
end with CR, LF or CR LF. (Why this speed: [serial-console.md](design/serial-console.md#speed).)

## Commands

| Command | What it does |
|---|---|
| `help [command]` | List all commands, or show how to use one |
| `version` | Firmware version, build type, chip revision and unique ID, compiler |
| `status` | Uptime, reason for the last reset, clock source and speed, LED mode, serial statistics |
| `led on\|off\|auto` | Green LED on, off, or blinking once per second (`auto`, the default) |
| `mode interactive\|host` | Switch the console mode |
| `fault [clear]` | Details of the last crash (type, cause, code address, bad memory address), or forget it. A crash is reported after the restart it caused; any later restart forgets it |
| `can` | CAN status: mode, bit rate, bus state, frames received, dropped and sent to the laptop, error counters |
| `can loopback [kbps]` | Start CAN in silent loopback at 125, 250, 500 (default) or 1000 kbit/s: frames the board sends are received internally, nothing reaches the pins |
| `can off` | Stop CAN (CAN is off after every start-up) |
| `can send <id> [data]` | Loopback only: send one frame. Hex, as in Linux's `cansend`: `can send 7DF 0201050000000000`. IDs above `7FF` are 29-bit |
| `can test <count> [per_s]` | Loopback only: send numbered test frames (ID `123`), as fast as the bus allows or at `per_s` frames per second |
| `can stream on\|off` | Whether received frames are sent to the terminal or laptop (on after every start-up). Off, they are still received and counted, but not sent: useful in a terminal while the board is busy with other traffic |
| `obd` | The board's OBD2 tester: whether it is running, the latest live values (engine speed, vehicle speed, throttle, engine load, coolant and intake temperature, engine run time), warning light, stored trouble codes, VIN, and request counters. A value shows `-` until it has been read |
| `obd start\|stop` | Start or stop the tester. Needs CAN in loopback, where the board also plays the car's engine ECU: the tester's requests come back to it, and its replies back to the tester. `can off` stops the tester |
| `reset` | Restart the board (the reply is sent before it restarts) |
| `crash bus\|usage\|divzero\|hard\|watchdog` | Debug builds only: crash on purpose to test crash capture. See [fault-handling.md](design/fault-handling.md) |

Commands and arguments are not case-sensitive.

## Interactive mode

```
obd> status
  Uptime (ms)       53211
  Last reset        reset pin
  Clock source      HSE 8 MHz bypass
  CPU clock (Hz)    180000000
  Watchdog (ms)     500
  Last crash        none
  LED               auto
  Serial received   7
    dropped         0
  ...
obd> led blue
error: invalid argument
obd> lde on
error: unknown command (type 'help' for a list)
obd> led
error: wrong number of arguments (usage: led on|off|auto)
```

Editing keys: **Backspace** deletes the last character, **Ctrl-C** discards the line. If an event
happens while you're typing (for example the blue button is pressed), it is printed on its own
line and your partly typed command is shown again below it.

While CAN is on, each frame received is printed on its own line: receive time in seconds, ID
(3 hex digits, or 8 for a 29-bit ID), length, data. When the traffic stops for 200 ms, the prompt
and anything you had typed are shown again.

```
obd> can loopback
obd> can test 3
obd>
  4.709922  123       [8]  00 00 00 00 FF FF FF FF
  4.710166  123       [8]  01 00 00 00 FE FF FF FF
  4.710414  123       [8]  02 00 00 00 FD FF FF FF
obd>
```

Text output is about twice the size of a binary packet, so at full load on a fast bus a terminal
can't keep up and the board drops frames (counted by `can`). The laptop tool uses host mode.

To read OBD2 data in a terminal, start loopback and the tester, and switch frame lines off so they
don't scroll the answers away (the tester sends about 100 requests a second):

```
obd> can loopback
obd> can stream off
obd> obd start
obd> obd
  Tester            running
  Simulated ECU     on
  Engine (rpm)      2735
  Speed (km/h)      61
  Throttle (%)      31.4
  Engine load (%)   65.1
  Coolant (C)       27
  Intake air (C)    25
  Run time (s)      10
  Warning light     on
  Codes stored      2
  Trouble codes     P0301 P0420
  VIN               1M8GDM9AXKP042788
  Requests          838
    answered        838
    timed out       0
    errors          0
```

Lines are limited to 96 characters. When a line is full the terminal bell rings, and when you
press Enter the whole line is rejected with `error: line too long`. It is never run cut short,
because a shortened command could do something other than what you typed. Press Ctrl-C to start
over.

## Host mode

For programs that talk to the board. Enter it with `mode host`; leave it with
`mode interactive`. After a reset the board always starts in interactive mode.

**Requests** are command lines terminated by CR or LF.

**Replies** are zero or more data lines followed by exactly one final line:

| Final line | Meaning |
|---|---|
| `OK` | The command succeeded |
| `ERR <reason>` | It failed: `unknown command`, `wrong number of arguments (usage: ...)`, `invalid argument`, `too many arguments`, `command failed`, `line too long` (over 96 characters; nothing was run) |

**Data lines** for `version`, `status`, `fault` and `can` are `key=value`:

```
> status
< uptime_ms=53211
< reset_cause=reset pin
< clock=HSE 8 MHz bypass
< sysclk_hz=180000000
< watchdog_timeout_ms=500
< last_fault=none
< led=auto
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
contain spaces, so split each line at the first `=` only. Empty lines produce no reply.

**Events** report things that happen outside a command. They can arrive at any time, so a program
should check every line it reads for a leading `!`:

| Event | Meaning |
|---|---|
| `!button pressed` | The blue USER button was pressed |

### CAN frame packets

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

### Connecting reliably from a program

The board may be in either mode, and a person may have left half a line typed. To get to a known
state:

1. Send `Ctrl-C` (byte `0x03`) to discard any partial line, then `\r`.
2. Send `mode host\r`.
3. Read lines until one is exactly `OK`. Anything before it is echo or prompt text from
   interactive mode and can be ignored.

After `reset`, the board restarts and prints its start-up report in interactive mode. Wait for the
`obd> ` prompt, then repeat the steps above.

When finished, send `mode interactive\r` so that a person opening a serial terminal next gets the
prompt and echo. A reply always uses the mode that is active once the command has run, so this
one ends with the `obd> ` prompt, not `OK`. (The same rule is why `mode host` answers `OK`.)

## Engineering detail

The console logic (line editing, modes, command dispatch and reply formatting) lives in the
portable library (`lib/console/`: `console_core.c`, `cli.c`, `line_editor.c`) and is unit-tested
to 100% MC/DC coverage. The firmware side (`firmware/console/`: `console.c`, `commands.c`) only
moves bytes and supplies the command table. How the bytes move is described in
[design/serial-console.md](design/serial-console.md).
