# MISRA C coding standard

## In short

**MISRA C** is a set of rules for writing C, created by the automotive industry and now used for
most safety-critical embedded software, including cars, aircraft, medical devices and industrial
controls. C allows many things that compile without complaint but behave unexpectedly: silent type
conversions, unclear operator precedence, hidden side effects, and behaviour that differs between
compilers. MISRA rules ban those constructs.

All C code in this project is checked against **MISRA C:2012** on every change:

- **Live compliance report:** <https://ka5j.github.io/obd2-sniffer/misra.html>
- **CI:** any finding that is neither fixed nor covered by a documented exception blocks the
  merge, and is shown on the pull request at the exact line

Current result: **no open findings**. Every remaining exception is written down with its reason,
its risk controls, and the exact lines it applies to.

## How the rules work

| Category | Meaning | In this project |
|---|---|---|
| Mandatory | Must always be followed | Always followed. The checker refuses any exception to these. |
| Required | Must be followed unless a formal *deviation* is recorded | 4 deviations, each tagged at the exact lines it covers, or with its location recorded where the tool can't detect it |
| Advisory | Recommended; a project may decide not to apply one, with a reason | 4 not applied, each with a recorded reason |

Most findings when the check was introduced were fixed in the code. Examples:

- **Shift widths (Rule 12.2):** `1U << 25` is a violation under MISRA's type model, so register bit
  masks became full 32-bit constants (`0x02000000U`).
- **Unused return values (Rule 17.7):** results of `memcpy`/`memset` are now explicitly discarded
  with `(void)`, which shows the result was considered.
- **Modified parameters (Rule 17.8):** functions work on local copies instead of changing their
  arguments.
- **Pointer arithmetic (Rule 18.4):** `data + n` became `&data[n]`, which keeps bounds visible.
- **Implicit truth tests (Rule 14.4):** `if (flags)` became `if ((flags & BIT) != 0U)`.

All changes were checked by the existing unit tests and the 100% MC/DC coverage gate.

## Exceptions

### Deviations from required rules

| ID | Rules | What and why |
|---|---|---|
| DEV-01 | 17.1, 21.6 | `printf`-style formatting for the diagnostic console, confined to two functions in `console.c`. Format strings are checked by the compiler and output is bounded by a fixed buffer. |
| DEV-02 | 10.8, 14.4 | Expansions of ST's HAL macros (for example `do { } while (0U)` in clock-enable macros). This is third-party code used as intended, pinned to a fixed version. |
| DEV-03 | 11.6, 21.2 | The C library (newlib) requires functions with reserved names such as `_sbrk`, which must return `(void *)-1` on failure. |
| DEV-04 | 1.3 | The debug-only `crash` command divides by zero and reads an unmapped address on purpose, to prove crash capture works. Never compiled into release builds. |

### Advisory rules not applied

| Rule | Where | Why |
|---|---|---|
| 15.5 (single exit point) | All code | Early returns for input checks are clearer than flag variables and nesting |
| 8.7 (limit external linkage) | All code | Library functions are a public API used by the firmware and tests, which the tool doesn't see together |
| 11.4 (pointer/integer conversion) | Firmware drivers only | Hardware registers are accessed at fixed addresses, which requires this conversion. Fully applied to the portable library. |
| 2.5 (unused macros) | HAL configuration header only | Every macro in it is read by ST's HAL, outside the analysed code |

### Tool false positives

| ID | Rule | Detail |
|---|---|---|
| FP-01 | 7.3 | The checker reports the field name `Pull` (from ST's GPIO structure) as a lowercase literal suffix. Rule 7.3 is about number suffixes like `12l`. Reproduced in isolation with a two-line test file. |

The full text of each exception, including risk controls and every source location it covers, is
in [misra/config.toml](../../misra/config.toml) and on the live report.

## Engineering detail

### Tooling

- **Checker:** cppcheck 2.21.0 with its MISRA addon, which has automated checks for 130 MISRA C:2012
  rules. CI builds this exact version from source (and caches it) so local and CI results are
  identical.
- **Target model:** analysis uses cppcheck's `arm32-wchar_t4` platform, so type sizes match the
  Cortex-M4 rather than the development machine.
- **Scope:** project code in `lib/` and `firmware/`, including headers. ST's HAL and CMSIS are
  third-party and excluded. The unit tests are excluded too, as test code usually is.
- **Policy engine:** `scripts/misra.py` runs the analysis and classifies every finding as open,
  deviated, disapplied or false positive using `misra/config.toml`. It fails if:
  - any finding is open, including general cppcheck warnings (likely bugs);
  - a source tag names an unknown deviation;
  - a deviation is used for a rule it doesn't list;
  - a mandatory rule is deviated, or a non-advisory rule is disapplied.

  Tags that no longer cover a finding are reported so they get removed.

### Assembly language

All assembly is in one file, `firmware/src/fault_entry.S` (the fault entry stubs), as Directive 4.3
requires. There is no inline assembly in the C code. The checker reports inline `asm` statements as
implicitly declared functions (Rule 17.3, mandatory), so keeping assembly out of C also avoids a
false report against a rule that must never be deviated.

### Deviation tags

A deviation applies only where it is tagged, and only to the rules it lists:

```c
__HAL_RCC_GPIOA_CLK_ENABLE(); /* MISRA DEV-02 */
```

A tag covers its own line. A tag on a comment-only line also covers the line directly below it.
A trailing comment never covers the next statement.

### Checking the checker

Two ways the analysis could silently report nothing were found and closed:

1. **Severity filter.** The MISRA addon reports at "style" severity. Without `--enable=style`,
   every finding is dropped and the run looks clean. The script now starts with a **self-test**:
   it analyses a snippet with known violations using the same settings and stops with an error if
   they aren't found.
2. **Tag placement.** An early version let any tag cover the line below it. That accidentally
   covered findings on the next line, which is how the `Pull` false positive was first hidden.
   Tags now follow the stricter rule above.

The gate itself was tested by adding a violation (`if (len)`, an implicit truth test) to the
library: the check reported Rule 14.4 at that line and failed.

### Limits

Static analysis cannot decide every rule. Some rules depend on run-time behaviour or design intent,
and the MISRA directives cover process topics such as documentation and testing. Those are covered
by code review, the unit tests and the sanitizers described in [testing.md](testing.md). This
project is checked against MISRA C:2012 with a documented compliance record, not certified by a
third party.

### Running locally

```sh
brew install cppcheck            # or build 2.21.0 from source to match CI exactly
python3 scripts/misra.py         # writes build/misra/misra-summary.json and cppcheck.xml
```
