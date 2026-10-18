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
| Laptop tool (`host/`) | Laptop and CI | Python tests (pytest) |

Keeping hardware-specific code thin, and moving every decision that can be moved into the portable
core, is what makes the high coverage target practical.

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

- **Coverage gate:** skipping the one test of `ring_init`'s argument checks drops MC/DC to 0% and
  branch coverage to 80%, and the gate fails.
- **ThreadSanitizer:** weakening the ring buffer's memory ordering from release/acquire to relaxed
  makes the CI run report `data race ... in memcpy`.
- **Name filter:** a test name that matches nothing makes Unity report "0 Tests", which CTest
  treats as a failure.

ThreadSanitizer is checked on Linux in CI. On current macOS it misses even a textbook race, so a
local pass there is not taken as evidence.

### Running locally

```sh
# Unit tests with AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

# Coverage report (Clang 18+), opens build/host-coverage/report/coverage/index.html
cmake --preset host-coverage && cmake --build --preset host-coverage
ctest --preset host-coverage && python3 scripts/coverage.py

# Report site as published on GitHub Pages
python3 scripts/build_report_site.py --out build/site \
    --junit "C unit tests=build/host-coverage/junit-c.xml" \
    --coverage "C core library=build/host-coverage/report"
```
