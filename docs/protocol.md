# Serial console and protocol

## In short

The board has a text console on its USB serial port. You can type commands into any serial
terminal, or let the laptop tool drive it. The same commands work both ways; only the output
format changes:

- **Interactive mode** (the default) is made for people: there's a prompt, what you type is
  echoed, and results are shown as an aligned table.
- **Host mode** is made for programs: nothing is echoed, every reply ends with `OK` or
  `ERR <reason>`, and data comes as `key=value` lines that are easy to parse.

Serial settings: **115200 baud, 8 data bits, no parity, 1 stop bit**, no flow control. Lines end
with CR, LF or CR LF.

## Commands

| Command | What it does |
|---|---|
| `help [command]` | List all commands, or show how to use one |
| `version` | Firmware version, build type, chip revision and unique ID, compiler |
| `status` | Uptime, reason for the last reset, clock source and speed, LED mode, serial statistics |
| `led on\|off\|auto` | Green LED on, off, or blinking once per second (`auto`, the default) |
| `mode interactive\|host` | Switch the console mode |
| `fault [clear]` | Details of the last crash (type, cause, code address, bad memory address), or forget it |
| `reset` | Restart the board (the reply is sent before it restarts) |
| `crash bus\|usage\|divzero\|hard\|watchdog` | Debug builds only: crash on purpose to test crash capture. See [fault-handling.md](design/fault-handling.md) |

Commands and arguments are not case-sensitive.

## Interactive mode

```
obd> status
  Uptime (ms)     53211
  Last reset      reset pin
  Clock source    HSE 8 MHz bypass
  CPU clock (Hz)  180000000
  Watchdog (ms)   500
  Last crash      none
  LED             auto
  Serial received 7
    dropped       0
  ...
obd> led blue
error: invalid argument
obd> lde on
error: unknown command (type 'help' for a list)
obd> led
error: wrong number of arguments (usage: led on|off|auto)
```

Editing keys: **Backspace** deletes the last character, **Ctrl-C** discards the line. Lines are
limited to 96 characters. If an event happens while you're typing (for example the blue button is
pressed), it is printed on its own line and your partly typed command is shown again below it.

## Host mode

For programs that talk to the board. Enter it with `mode host`; leave it with
`mode interactive`. After a reset the board always starts in interactive mode.

**Requests** are command lines terminated by CR or LF.

**Replies** are zero or more data lines followed by exactly one final line:

| Final line | Meaning |
|---|---|
| `OK` | The command succeeded |
| `ERR <reason>` | It failed: `unknown command`, `wrong number of arguments (usage: ...)`, `invalid argument`, `too many arguments`, `command failed` |

**Data lines** for `version`, `status` and `fault` are `key=value`:

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

### Connecting reliably from a program

The board may be in either mode, and a person may have left half a line typed. To get to a known
state:

1. Send `Ctrl-C` (byte `0x03`) to discard any partial line, then `\r`.
2. Send `mode host\r`.
3. Read lines until one is exactly `OK`. Anything before it is echo or prompt text from
   interactive mode and can be ignored.

After `reset`, the board restarts and prints its start-up report in interactive mode. Wait for the
`obd> ` prompt, then repeat the steps above.

## Engineering detail

The console logic (line editing, modes, command dispatch and reply formatting) lives in the
portable library (`lib/console_core.c`, `lib/cli.c`, `lib/line_editor.c`) and is unit-tested to
100% MC/DC coverage. The firmware side (`firmware/src/console.c`, `firmware/src/commands.c`) only
moves bytes and supplies the command table. How the bytes move is described in
[design/serial-console.md](design/serial-console.md).
