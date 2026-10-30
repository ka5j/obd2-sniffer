# Build system and start-up sequence

## In short

**Building.** The firmware is compiled from the command line with free, standard tools (GCC for
Arm, CMake). There's no dependency on a vendor IDE, so the exact same build runs on my laptop and
on GitHub's servers for every change. ST's driver library is included as a pinned, unmodified
dependency, and my own code is compiled with strict settings that turn every warning into an
error.

**Memory layout.** I wrote the linker script, the file that decides where code and data live in
the chip's memory, rather than using a generated one. It reserves a small area of memory that
survives a reset, so the device can report what went wrong after a crash. It also makes the build
fail if the program ever leaves too little room for the stack. The firmware never allocates
memory at runtime, which removes a whole class of hard-to-find bugs.

**Start-up.** On power-up, the chip runs a short start-up routine, then my code configures the
clock to 180 MHz, starts the serial port, and prints a start-up report: firmware version, chip ID,
clock speeds, and why the chip last restarted (power-on, reset button, watchdog, crash). That last
item is the first tool for diagnosing problems in the field.

| Term | Meaning |
|---|---|
| Firmware | The program that runs on the microcontroller |
| Toolchain | The compiler, linker and related tools that turn source code into firmware |
| Flash / RAM | Flash keeps the program when power is off; RAM holds working data and is cleared at power-off |
| Linker script | Instructions telling the linker where each part of the program goes in memory |
| Stack | Memory used for function calls and local variables; overflowing it corrupts other data |
| HAL | Hardware Abstraction Layer: ST's library of functions for configuring the chip's peripherals |
| CI | Continuous Integration: GitHub automatically builds and checks every change |

The rest of this document is the engineering detail.

## Build

The firmware is built with CMake and the Arm GNU Toolchain (`arm-none-eabi-gcc`). No IDE project
files are involved, so the same commands work on a laptop and in CI.

| File | Purpose |
|---|---|
| `cmake/arm-none-eabi.cmake` | Toolchain file: compiler, CPU flags, newlib-nano, section garbage collection |
| `CMakePresets.json` | `firmware` (Debug, `-Og -g3`) and `firmware-release` (`-Os -g3`) presets |
| `firmware/CMakeLists.txt` | HAL library, firmware image, post-build outputs, `flash` target |
| `cmake/version.cmake` | Writes `version.h` from `git describe` at build time |

### Compiler flags

`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` targets the Cortex-M4 with its
single-precision FPU and passes floating-point arguments in FPU registers. `-ffunction-sections
-fdata-sections` with `-Wl,--gc-sections` puts every function and object in its own section so
the linker can drop anything unreferenced. That matters most for the HAL, where only a fraction
of each driver is used.

### Vendor code

ST's HAL driver, the CMSIS device package and CMSIS core are git submodules pinned to release
tags, not copies pasted into the tree. Updating them is a reviewed one-line change, and the repo
history contains only project code.

The HAL is compiled as its own static library with warnings disabled. Project code is compiled
with `-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wstrict-prototypes
-Wmissing-prototypes -Wcast-align`. Vendor include directories are marked `SYSTEM` so warnings in
vendor headers don't fail the project build.

Only the HAL modules the firmware needs are compiled: they are listed in `firmware/CMakeLists.txt`
(`HAL_MODULES`) and enabled in `firmware/hal/stm32f4xx_hal_conf.h`, and the two lists must
match.

### Outputs

Each build produces `obd2_sniffer.elf` (with debug info), `.bin` and `.hex` images, a linker
`.map` file with cross references, and a size report. `cmake --build --preset firmware --target
flash` programs the board through the Nucleo's on-board ST-LINK with OpenOCD, verifies the flash
contents and resets the MCU.

### Version stamping

`cmake/version.cmake` runs on every build and writes `git describe --tags --always --dirty` (for
example `v0.2.0`, or `v0.2.0-3-gabc1234-dirty` for a modified tree) into `version.h`.
`configure_file` only rewrites the header when the string changes, so it doesn't trigger a
rebuild every time. The banner therefore always identifies the exact source the image came from.

### Release files

The files attached to a GitHub Release are built by CI from the tag, not on a laptop. Pushing a
tag `vX.Y.Z` runs `.github/workflows/release.yml`, which:

1. fails unless the version in `CMakeLists.txt`, the laptop tool and the sample start-up report
   in `getting-started.md` is `X.Y.Z`;
2. builds both firmware builds with the same toolchain as every CI run;
3. fails unless both report exactly `vX.Y.Z` (not `vX.Y.Z-dirty`, or a later commit);
4. attaches the Debug and Release `.bin`, `.hex` and `.elf` files and `SHA256SUMS.txt` to a
   **draft** release.

The tag is first built locally and checked on the board. Before the draft is published,
`scripts/release_check.py vX.Y.Z` compares CI's files with that local build: the `.bin` and
`.hex` files must be byte-identical, and the `.elf` files identical once debug info is stripped.
The debug info records the folder each build ran in, so it differs between machines; the code
and data do not. So the published files are the ones that passed the board checks.

## Memory layout

`firmware/linker/STM32F446RETx_FLASH.ld` is written for this project rather than taken from a
generated template.

```
FLASH 0x0800_0000 (512 KiB)            RAM 0x2000_0000 (128 KiB)
+---------------------------+          +---------------------------+  0x2000_0000
| .isr_vector               |          | .data   (copied from flash)|
| .text                     |          | .bss    (zeroed)           |
| .rodata                   |          | .noinit (kept across reset)|
| .ARM.exidx, init/fini     |          |                           |
| .data initial values  ----+--copy--> |          free             |
+---------------------------+          |                           |
                                       |  stack (grows down)       |
                                       +---------------------------+  0x2002_0000 = _estack
```

- **`.isr_vector`** comes first. On reset the Cortex-M core reads the initial stack pointer from
  address 0 and the reset handler address from address 4. Flash is aliased at address 0.
- **`.data`** has a load address in flash and a run address in RAM. The startup code copies it.
- **`.bss`** is zeroed by the startup code.
- **`.noinit`** is not touched by the startup code, so it keeps its contents across a warm reset
  (watchdog, software reset or fault). That is where crash information is kept so it can be
  reported after the reboot. Its contents are random after power-on, so anything stored there
  must carry a magic number and checksum.
- **Stack guard.** `._stack_check` reserves `_Min_Stack_Size` (8 KiB) after all static data. If
  static RAM grows too large, the link fails instead of the stack silently overflowing into
  `.bss` at runtime.
- **Worst-case stack use** is checked in CI against those 8 KiB by `scripts/stack_usage.py`. It
  disassembles the linked ELF, reads how many bytes each function takes from the stack, and
  follows the deepest call chain from the reset handler and from each interrupt handler. The
  total assumes every handler is nested on top of the main loop at once, each with the CPU's
  108-byte exception frame (FPU registers included): an upper bound. It is 2,544 bytes in the
  Release build and 2,764 in the Debug build (about a third of the 8 KiB). The deepest path is a
  console command formatting text with `vsnprintf`. The fault handlers run on their own 512-byte
  stack, of which the crash capture uses 24 bytes (96 in Debug).

  The check fails on recursion (MISRA C:2012 rule 17.2), on an instruction that moves the stack
  pointer by an amount it can't work out, and on a call through a function pointer that is not in
  its reviewed list of targets (the command handlers, the console's output function, newlib's
  `printf` internals). Every frame is also compared with GCC's own figure from `-fstack-usage`,
  so a mistake in reading the disassembly shows up as a mismatch. The C library has no such
  figures; its frames come from the disassembly alone.
- **No heap.** `_sbrk` in `syscalls.c` always fails, so `malloc` returns NULL. All buffers are
  sized at compile time.
- **Read-only flash segment.** GCC emits `.init_array` and `.fini_array` as writable sections,
  which would make the flash program segment read-write-execute. The `READONLY` keyword keeps the
  segment `R E`, verified with `arm-none-eabi-readelf -l`.

## Boot sequence

1. **Reset.** The core loads `SP = _estack` and jumps to `Reset_Handler` (from ST's
   `startup_stm32f446xx.s`).
2. **`SystemInit()`** (CMSIS `system_stm32f4xx.c`) enables full access to the FPU coprocessor.
   The CPU is still running on the 16 MHz internal HSI oscillator.
3. **C runtime setup.** `.data` is copied from flash to RAM, `.bss` is zeroed, and
   `__libc_init_array` runs static constructors.
4. **`main()`**:
   1. `reset_cause_read()` reads and clears `RCC_CSR` before anything else can disturb it.
   2. `HAL_Init()` enables the flash prefetch and caches, sets NVIC priority grouping, and starts
      the 1 ms SysTick.
   3. `board_init()` configures the LED and button GPIOs.
   4. `clock_init()` brings the system to 180 MHz (below). On total failure it calls
      `board_panic()`, which blinks the LED with a busy-wait loop that needs no timers or
      interrupts.
   5. `fault_init()` picks up a crash record left by the previous run, if any, and enables the
      separate fault handlers ([fault-handling.md](fault-handling.md)).
   6. `console_init()` sets up USART2 with its receive interrupt and transmit DMA
      ([serial-console.md](serial-console.md)).
   7. The start-up report is printed, including a crash summary if there was one, followed by
      the `obd> ` prompt.
   8. `watchdog_init()` starts the independent watchdog. It is started last, once nothing left
      in start-up can take long.
   9. The super-loop starts: refresh the watchdog, process console input, update the LED, check
      the button, and restart if `reset` was requested.

## Clock tree

```
HSE 8 MHz (bypass) --/M=4--> 2 MHz --xN=180--> VCO 360 MHz --/P=2--> SYSCLK 180 MHz
                                                                      |
                     AHB /1  -> HCLK  180 MHz (core, DMA, memory)  <---+
                     APB1 /4 -> PCLK1  45 MHz (bxCAN, USART2; TIM2 at 90 MHz)
                     APB2 /2 -> PCLK2  90 MHz
```

- **HSE bypass.** The Nucleo has no crystal on the HSE pins by default; the ST-LINK's MCU drives
  an 8 MHz clock into PH0. Bypass mode turns off the oscillator amplifier and takes that signal
  directly.
- **PLL input of 2 MHz.** The PLL accepts 1 to 2 MHz at its input; the top of the range gives the
  lowest jitter.
- **Over-drive.** 180 MHz is above the 168 MHz limit of normal mode. The regulator must be in
  voltage scale 1 with over-drive enabled before switching SYSCLK to the PLL.
- **Flash wait states.** Flash needs 5 wait states at 180 MHz and 3.3 V. The ART accelerator
  (prefetch plus instruction and data caches) hides most of that latency.
- **APB1 at 45 MHz** is its maximum, and 45 MHz divides cleanly into CAN bit rates: 500 kbit/s is
  prescaler 6 with 15 time quanta per bit (see [hardware.md](hardware.md#can-bit-timing)).
- **HSI fallback.** If HSE doesn't start within 100 ms, the PLL is rebuilt from HSI (M=8) for
  the same 180 MHz. The banner reports the source because HSI is trimmed to +-1% only at 25 C
  and drifts by several percent over temperature. That is more than CAN bit timing tolerates
  (on the order of 1%), and it would explain intermittent bus errors.

## Reset cause

`RCC_CSR` keeps a sticky flag for each reset source. Several flags can be set at once: a power-on
sets POR, BOR and PIN, and every internal reset also pulls NRST low and sets PIN. The decoder
checks the most specific flags first (low-power, window watchdog, independent watchdog, software)
and only reports "reset pin" when nothing else applies. It then clears the flags so the next boot
reports only its own cause.
