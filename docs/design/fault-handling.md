# Watchdog and crash reporting

## In short

Embedded devices often run unattended. When something goes wrong, you want two things: the device
should **recover by itself**, and afterwards it should be able to **tell you what happened**. This
firmware does both.

- **Recovering from a hang: the watchdog.** A hardware timer, independent of the processor's main
  clock, counts down from about half a second. The main program resets it on every pass of its
  loop. If the program ever gets stuck, the timer runs out and restarts the chip.
- **Recovering from a crash: fault capture.** If the processor detects an illegal operation
  (reading memory that doesn't exist, dividing by zero, ...), the firmware records what the
  processor was doing (the exact instruction address, the reason, and the memory address involved)
  in a small area of memory that survives a restart, then restarts.
- **Explaining it afterwards.** On the next start-up, the report includes a line such as
  `Crash    BusFault at 0x08000D8E: bus error on data access`, and the `fault` command shows the
  full details. The start-up report also always says why the chip last restarted (power-on, reset
  button, watchdog, software).

Both can be tested on demand. Debug builds have a `crash` command that triggers each kind of fault
on purpose, so the recovery path is exercised rather than assumed.

| Term | Meaning |
|---|---|
| Watchdog | A timer that resets the chip unless the software regularly proves it is alive |
| Fault | The processor stopping an illegal operation and jumping to a fault handler |
| Program counter (PC) | The address of the instruction that was running |
| Link register (LR) | The address the current function will return to, roughly "who called it" |

## What gets reported

Output from the board after `crash bus` (addresses depend on the build):

```
obd> fault
  Last crash        BusFault
  Cause             bus error on data access
  At address (PC)   0x08000D8E
  Called from (LR)  0x08000EB9
  Bad address       0x08080000
  Crash time (ms)   1485
  CFSR              0x00008200
  HFSR              0x00000000
```

The PC can be turned into a file and line number on the development machine:

```sh
arm-none-eabi-addr2line -f -e build/firmware/firmware/obd2_sniffer.elf 0x08000D8E
# read_bad_address
# .../firmware/src/fault.c:87
```

Crash testing (debug builds only):

| Command | Fault produced |
|---|---|
| `crash bus` | Read from an unmapped address: BusFault, with the bad address recorded |
| `crash usage` | Unaligned 32-bit read with the unaligned trap on: UsageFault |
| `crash divzero` | Integer division by zero with the divide trap on: UsageFault |
| `crash hard` | Bus error with the BusFault handler disabled: escalates to HardFault |
| `crash watchdog` | Stops the main loop with interrupts off: the watchdog resets the chip |

## Engineering detail

### Watchdog (`firmware/src/watchdog.c`)

- Independent watchdog (IWDG), clocked by the internal low-speed oscillator (LSI, nominally
  32 kHz). Prescaler 64 gives a 2 ms tick; a reload value of 250 gives 500 ms nominal.
- The LSI is only specified to 17-47 kHz, so the real timeout is 340-940 ms. The main loop
  refreshes it every pass, and no pass is allowed to take longer than a few tens of milliseconds:
  serial output waits at most 50 ms for buffer space.
- Started at the end of start-up, after the steps that can legitimately take a while.
- `DBGMCU_APB1_FZ_DBG_IWDG_STOP` pauses it while a debugger has the core halted, so stepping
  through code doesn't trigger resets.

### Fault capture

1. **Separate fault handlers.** `fault_init()` enables the MemManage, BusFault and UsageFault
   exceptions (otherwise everything escalates to HardFault), and the divide-by-zero trap (by
   default, integer division by zero silently returns 0 on Cortex-M).
2. **Entry in assembly** (`firmware/src/fault_entry.S`). Each handler puts its fault type in `r1`
   and jumps to a shared routine. Bit 2 of the `EXC_RETURN` value in `LR` says whether the CPU
   stacked its registers on the main or the process stack; the routine passes that frame's address
   in `r0`. It then **switches to a dedicated 512-byte fault stack** before calling C. If the fault
   was a stack overflow, the normal stack is unusable, and running the capture code on it would
   fault again and lock up the CPU.
3. **Capture in C** (`fault_capture()` in `firmware/src/fault.c`). Copies the stacked `r0`-`r3`,
   `r12`, `LR`, `PC` and `xPSR`, plus the fault status and address registers (`CFSR`, `HFSR`,
   `MMFAR`, `BFAR`) and the uptime, into a `fault_record_t` in the `.noinit` RAM section. Seals it
   with a magic number and CRC-16, and calls `NVIC_SystemReset()`.
4. **Report on the next boot.** The startup code doesn't touch `.noinit`, so the record survives
   the reset. `fault_init()` accepts it only if the magic number and CRC are valid (after power-on
   that RAM holds random data), keeps a copy for the `fault` command, and erases the original so
   the crash is reported once.

Decoding the status registers into readable causes (`fault_cause_str()`) and the record's
validation live in the portable library (`lib/fault_record.c`) and are unit-tested at 100% MC/DC.
Only the register capture is hardware-specific.

### Design choices

- **Reset, don't halt.** A device in the field should recover. While developing, a debugger can
  still stop on the fault: set a breakpoint in `fault_capture()`.
- **No serial output in the handler.** The UART may be in the middle of a DMA transfer, and the
  stack may be unusable. Writing a few words to RAM and resetting is the most reliable option; the
  report is printed by healthy code after the restart.
- **Assembly isolated in one file.** MISRA C Directive 4.3 requires assembly language to be
  encapsulated and isolated. The C code contains no inline assembly.
- **Crash tests only in debug builds.** The `crash` command and `fault_trigger()` are compiled only
  without `NDEBUG`; the release image contains neither (checked with `arm-none-eabi-nm`). The
  deliberate undefined behaviour is recorded as MISRA deviation DEV-04.

### Limits

- If the fault stack itself can't be used (for example after corruption of the fault-handling
  code), the CPU locks up. The watchdog then resets it, and the report shows a watchdog reset
  without crash details.
- An imprecise bus fault (from a buffered write) records the instruction that was running when the
  error arrived, which may be a few instructions after the faulting store. `CFSR` shows
  `IMPRECISERR` in that case.
