# obdsniff: laptop tool

`obdsniff` is the laptop side of the OBD2 CAN Sniffer. It connects to the board over USB and lets
you check its status, control it, watch events as they happen, and work with it interactively,
from the command line or from a desktop app. A built-in **simulator** (`--port sim`) behaves like
the real board, so everything can be tried without hardware.

## Install

Requires Python 3.10 or newer. From the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -e host
```

## Use

```sh
obdsniff ports                     # list serial ports; the Nucleo is marked
obdsniff info                      # firmware version and board status
obdsniff version                   # firmware version and chip identity only
obdsniff status                    # uptime, last reset, clocks, serial statistics
obdsniff led on                    # on | off | auto (heartbeat)
obdsniff fault                     # details of the last crash, if any (--clear forgets it)
obdsniff reset                     # restart the board and reconnect
obdsniff crash bus                 # debug firmware: crash on purpose, show what was recorded
obdsniff monitor                   # print events (button presses) as they happen
obdsniff shell                     # interactive session
obdsniff gui                       # desktop app (see below)
```

The board is found automatically. Use `--port /dev/tty.usbmodem1103` (or `COM5` on Windows) to
choose one, or `--port sim` for the simulator. Add `--json` for machine-readable output:

```sh
$ obdsniff --port sim --json status
{
  "uptime_ms": "1534",
  "reset_cause": "power-on",
  ...
}
```

Exit codes: `0` success, `1` the board rejected the command, `2` no connection or invalid
arguments.

When it finishes, the tool switches the board back to interactive mode, so a serial terminal
opened afterwards works as usual.

### Interactive shell

```
$ obdsniff --port sim shell
Connected. Type board commands (try 'help'); 'exit' or Ctrl-D quits.
obd> status
  Uptime (ms)       5210
  Last reset        power-on
  ...
obd> crash bus
Board restarted after the crash. What it recorded:
  Last crash        BusFault
  Cause             bus error on data access
  ...
```

Commands are passed to the board. The shell keeps the board in host mode underneath, formats the
replies, shows events as they arrive, and reconnects automatically after `reset` or `crash`.

## Desktop app

```sh
pip install -e "host[gui]"         # adds PySide6 (Qt)
obdsniff gui                       # or: obdsniff --port sim gui
```

![The desktop app connected to the board, after a deliberate crash](../docs/images/desktop-app.png)

Pick the port (the Nucleo is found and selected automatically; the simulator is always listed)
and press **Connect**. The left side shows the board's version, status and last crash, and has
buttons for the LED, restarting the board and clearing the crash record. The console on the right
takes the same commands as the shell, and events such as button presses appear there as they
happen.

The connection to the board runs on its own thread, so the window stays responsive even while the
board restarts. When the app closes, the board is handed back in interactive mode.

## As a library

```python
from obdsniff import open_device

with open_device("sim") as board:  # or a port path, or None to find the board
    print(board.status()["uptime_ms"])
    board.set_led("on")
    print(board.poll_events(timeout=1.0))  # e.g. ['button pressed']
```

## Development

```sh
pip install -e "host[dev]"
cd host
ruff check . && ruff format --check .
python -m pytest --cov            # 100% line and branch coverage is required
```

| Module | Role |
|---|---|
| `device.py` | Protocol client: reliable connect, commands, `key=value` parsing, events, restarts |
| `transport.py` | Byte transport: the serial port (pyserial) |
| `simulator.py` | Software model of the board's console, used by `--port sim` and the tests |
| `ports.py` | Finds the board by its ST-LINK USB IDs |
| `cli.py`, `shell.py`, `output.py` | Command line, interactive shell, formatting |
| `gui/window.py` | Desktop app window: displays results, turns clicks into requests |
| `gui/worker.py` | Desktop app's board connection, run on its own thread; talks to the window only through Qt signals |

The serial protocol is described in [docs/protocol.md](../docs/protocol.md).
