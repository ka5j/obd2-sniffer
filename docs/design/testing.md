# Testing and code coverage

## In short

Every change to this project is tested automatically before it can be merged. The logic that
doesn't depend on the microcontroller's hardware (data buffers, message encoding, the diagnostic
protocol, the safety rules) is written so it can also run on a normal computer, where it is
checked by unit tests.

Those tests must reach **100% MC/DC coverage**, or the change is rejected. MC/DC is the coverage
standard used for flight-control software (DO-178C) and recommended for the highest safety level
of automotive software (ISO 26262 ASIL D). It means that for every decision in the code, each
condition in it was shown to change the outcome on its own. The tests also run under tools that
detect memory errors, undefined behaviour and race conditions between threads.

All C code is also checked against the MISRA C:2012 coding standard; see [misra.md](misra.md).

Results are published automatically:

- **Live report:** <https://ka5j.github.io/obd2-sniffer/>, with every test and the line-by-line
  coverage, updated on each merge
- **Pull requests:** each test appears in the checks tab, and a coverage table is added to the
  build summary

## What gets tested where

| Code | Runs on | How it is tested |
|---|---|---|
| Portable core (`lib/`): buffers, encoding, protocol logic, safety rules | Laptop and CI | Unit tests, 100% MC/DC required |
| Hardware drivers (`firmware/`): clocks, UART, CAN, watchdog | The STM32 board | On the board: serial output, logic analyzer, fault injection |
| Laptop tool (`host/`) | Laptop and CI (Python 3.10 and 3.12) | Python tests (pytest), 100% line and branch coverage required, plus lint (ruff) |

Keeping hardware-specific code thin, and moving every decision that can be moved into the portable
core, is what makes the high coverage target practical.

The laptop tool is tested against a software model of the board's console (`host/src/obdsniff/simulator.py`)
and against scripted fake connections for the failure cases: no reply, malformed replies, a
board that doesn't come back after a restart, a release build that rejects test commands. The
desktop app is tested the same way with pytest-qt: the real window is clicked through against the
simulator, drawing offscreen so it runs in CI without a display. Python has no MC/DC tooling, so
it is held to 100% line and branch coverage instead.

## Checks on the board

Code that drives the hardware can only be proven on the hardware. It is checked on a real
NUCLEO-F446RE board. Where possible the checks are scripted: a Python script types into the
board's serial console and checks what comes back, and the laptop tool is run against the board
as well as the simulator. Each release is checked this way before it is tagged.

**Latest check:** 19 October 2026, Debug and Release builds, serial link at 1.5 Mbaud.

| Area | Check | Result |
|---|---|---|
| Clocks | Start-up report shows the external 8 MHz clock, 180 MHz CPU, 45 and 90 MHz buses | Pass |
| Clocks | Board uptime against the laptop's clock over 30 s | Pass: -19 ppm, within the 1 ms resolution of the measurement. The internal oscillator would be off by up to 1% (10,000 ppm) |
| Serial console | Every command and error message; Backspace, Ctrl-C, the 96-character limit | Pass |
| Serial console | The reply to `reset` arrives complete before the board restarts | Pass |
| Serial console | Bursts of 20 and 50 commands sent without waiting, at 115200 baud: every reply intact (28 KB at 100% of the line rate), nothing dropped, no restart | Pass |
| Button and LED | Button presses reported in both modes, with a half-typed line shown again; LED heartbeat | Pass |
| Crash capture | `crash bus`, `usage`, `divzero`, `hard`: right fault type and cause after the restart; the saved code address points at the faulting line (`fault.c:87` for `bus`); bad address `0x08080000` | Pass |
| Watchdog | Main loop stopped on purpose: reset after 535 ms (allowed: 340 to 940 ms), reported as a watchdog reset | Pass |
| Reset causes | Software reset, watchdog and reset pin each reported correctly | Pass |
| Release build | The `crash` test command is absent; everything else works | Pass |
| Laptop tool | Board found by its USB IDs; `info`, `--json status`, `led`, `shell` reconnecting after `reset` and `crash` | Pass |
| Desktop app | Connect, board details, console commands, `crash bus` then clear the record; the window keeps responding while the board restarts; the board is handed back in interactive mode on exit | Pass |
| Serial link | Stress test at 921,600, 1,000,000, 1,500,000 and 2,250,000 baud: every reply intact, about 99% of the line rate, no errors; 1.5 Mbaud chosen ([serial-console.md](serial-console.md#speed)) | Pass |
| CAN capture | Silent loopback, every frame checked by number and content: 100,000 frames at 500 kbit/s (4,230 frames/s, 100% load) and 10,000 at 250 kbit/s, none lost or damaged; frame spacing matches the frame length on the bus | Pass |
| CAN capture | 1 Mbit/s overload: frames dropped on the board when the serial link can't keep up, and the laptop sees exactly that many gaps in the capture numbers | Pass |
| CAN capture | Longest receive interrupt: 7 µs | Pass |
| CAN tools | `can test`, `monitor --log` (candump file), shell; desktop-app CAN tab: 4,000 test frames shown, none lost; a second program can't open the port | Pass |

The checks found problems that the unit tests and the simulator had not, all fixed:

- A line longer than 96 characters was cut short and then run as a command. It is now rejected
  whole ([#16](https://github.com/ka5j/obd2-sniffer/pull/16)).
- The laptop tool left the board in host mode, so a serial terminal opened afterwards showed no
  prompt or echo ([#15](https://github.com/ka5j/obd2-sniffer/pull/15)).
- In `help` and `fault` output, the longest entries were out of alignment
  ([#16](https://github.com/ka5j/obd2-sniffer/pull/16)).
- A burst of commands (for example pasted into a terminal) ran in a single pass of the main loop.
  At 115200 baud the replies took over a second to send, so the watchdog restarted the board; with
  that fixed, replies were partly dropped because the transmit buffer freed space only in large
  steps. Commands now run one per pass, only when their reply fits, and DMA transfers are capped at
  256 bytes ([#24](https://github.com/ka5j/obd2-sniffer/pull/24)).
- At 1.5 Mbaud the laptop tool got no replies, although a hand-written test worked. Changing any
  serial setting makes pyserial re-apply them all, and on macOS 1.5 Mbaud is applied through a
  placeholder speed, so every read briefly switched the ST-LINK's speed and lost bytes. The port is
  now configured once ([#26](https://github.com/ka5j/obd2-sniffer/pull/26)).
- Frame sequence numbers were first given when frames were sent, so frames dropped on the board
  left no gap. The overload test showed no gaps while thousands of frames were lost. They are now
  numbered on arrival ([#29](https://github.com/ka5j/obd2-sniffer/pull/29)).
- Two programs could open the board's port at once and split its output between them. The laptop
  tool now opens it exclusively ([#30](https://github.com/ka5j/obd2-sniffer/pull/30)).

## Coverage measures

| Measure | Meaning |
|---|---|
| Lines | Every line ran in at least one test |
| Functions | Every function was called |
| Regions | Every block of code ran, including each part of a short-circuit expression |
| Branches | Every decision went both ways (true and false) |
| MC/DC | Every condition in a decision independently changed the decision's outcome |

Example: `if (rb == NULL || storage == NULL || capacity < 2 || !power_of_two)`. Branch coverage
only needs one test that rejects and one that accepts. MC/DC needs a test for each of the four
conditions, where only that condition is true and it alone causes the rejection. That proves
every check is actually needed and actually works.

## Engineering detail

### Tools

- **Unity**: a small C unit-test framework, fetched by CMake at a pinned version.
- **CTest**: CMake's test runner. Every Unity test function is registered as its own CTest test
  (`ring_buffer.test_counters_wrap_past_uint32_max`), so each case is reported individually.
  Unity's default `RUN_TEST` ignores the name filter, so the tests use a `UNIT_TEST()` wrapper
  that applies it (`tests/test_support.h`).
- **Clang source-based coverage** with `-fcoverage-mcdc` (Clang 18 or newer).
  `scripts/coverage.py` merges the results, writes the HTML report, and fails the build if any of
  the five measures is below 100%.
- **Sanitizers** on every test run:
  - *AddressSanitizer*: out-of-bounds access, use-after-free
  - *UndefinedBehaviorSanitizer*: integer overflow, misaligned access, invalid shifts
  - *ThreadSanitizer*: data races. This matters for the lock-free ring buffer, whose stress test
    runs a producer and a consumer thread at full speed.

  Any sanitizer finding stops the test immediately (`-fno-sanitize-recover=all`).

### Checking the checks

A test suite is only useful if it can fail, so each guard was verified by breaking the code on
purpose:

- **Coverage gate:** when it was introduced (the library then held only the ring buffer), skipping
  the one test of `ring_init`'s argument checks dropped MC/DC to 0% and branch coverage to 80%, and
  the gate failed.
- **ThreadSanitizer:** weakening the ring buffer's memory ordering from release/acquire to relaxed
  makes the CI run report `data race ... in memcpy`.
- **Name filter:** a test name that matches nothing makes Unity report "0 Tests", which CTest
  treats as a failure.

ThreadSanitizer is checked on Linux in CI. On current macOS it misses even a textbook race, so a
local pass there is not taken as evidence.

### Running locally

```sh
# Unit tests with AddressSanitizer + UndefinedBehaviorSanitizer (configure, build, run)
cmake --workflow --preset host-tests

# Coverage report (Clang 18+), opens build/host-coverage/report/coverage/index.html
cmake --preset host-coverage && cmake --build --preset host-coverage
ctest --preset host-coverage && python3 scripts/coverage.py

# Report site as published on GitHub Pages
python3 scripts/build_report_site.py --out build/site \
    --junit "C unit tests=build/host-coverage/junit-c.xml" \
    --coverage "C core library=build/host-coverage/report"
```
