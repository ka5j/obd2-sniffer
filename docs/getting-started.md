# Getting started

This guide takes you from nothing to the firmware running on your own board. It takes about
15 minutes, most of which is installing tools.

## In short

1. Get a **NUCLEO-F446RE** board and a USB cable.
2. Install the **Arm GNU Toolchain**, **CMake**, **Ninja** and **Git**.
3. Clone and build:
   ```sh
   git clone --recurse-submodules https://github.com/ka5j/obd2-sniffer.git
   cd obd2-sniffer
   cmake --workflow --preset firmware
   ```
4. Copy `build/firmware/firmware/obd2_sniffer.bin` onto the **NODE_F446RE** USB drive that
   appears when the board is plugged in. Then open the board's serial port at 1500000 baud to see
   it running.

Each step is explained below, with a troubleshooting section at the end.

## 1. Hardware

| Item | Needed for | Notes |
|---|---|---|
| [NUCLEO-F446RE](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) | Everything | ST development board. The programmer/debugger (ST-LINK) is built in, so nothing else is needed to load firmware. |
| USB Mini-B cable | Everything | Must be a data cable; some cables only carry power |
| SN65HVD230 CAN transceiver module, OBD2 cable | Connecting to a car's CAN bus | Wiring and safety rules in [hardware.md](design/hardware.md). Check the [project status](../README.md#project-status) for which CAN features the current firmware includes. |

The board takes its power from USB. No external supply is needed.

## 2. Install the tools

| Tool | Version | Why |
|---|---|---|
| [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi-gcc`) | Built and tested with 15.2.Rel1 | Compiler for the microcontroller |
| [CMake](https://cmake.org/download/) | 3.25 or newer | Build system |
| [Ninja](https://ninja-build.org/) | Any recent | Fast build tool that CMake drives |
| [Git](https://git-scm.com/) | Any recent | Fetches the code and ST's driver libraries |
| [OpenOCD](https://openocd.org/) | 0.12 (optional) | Flashing and debugging from the command line |
| A serial terminal | Any | To see the board's output (`screen`, `minicom`, PuTTY, ...) |

### macOS (Homebrew)

```sh
brew install --cask gcc-arm-embedded
brew install cmake ninja open-ocd git
```

Homebrew installs the newest toolchain release, which may be newer than the tested 15.2.Rel1. To
match exactly, use the macOS installer for 15.2.Rel1 from the Arm downloads page instead.

### Linux (Ubuntu / Debian)

```sh
sudo apt update
sudo apt install git cmake ninja-build openocd screen
```

For the compiler, download the **x86_64 Linux hosted, arm-none-eabi** archive from the
[Arm GNU Toolchain downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads),
extract it (for example to `/opt`), and add its `bin` folder to your `PATH`. Ubuntu's own
`gcc-arm-none-eabi` package is an older release and is not what this project is tested with.

To use the serial port without `sudo`, add yourself to the `dialout` group, then log out and back
in:

```sh
sudo usermod -aG dialout "$USER"
```

### Windows

Install the Arm GNU Toolchain (Windows installer, tick **Add path to environment variable**),
CMake (tick **Add CMake to the system PATH**), Git for Windows, and put `ninja.exe` from the
[Ninja releases](https://github.com/ninja-build/ninja/releases) in a folder on your `PATH`. Run
the commands below from **Git Bash** or PowerShell. For the serial terminal, use
[PuTTY](https://www.putty.org/).

The macOS and Linux steps are what the project is developed and continuously built on. The Windows
steps use the standard installers but are not part of the automated builds.

### Check the installation

```sh
arm-none-eabi-gcc --version
cmake --version
ninja --version
```

If the toolchain is installed but not on your `PATH`, point the build at it instead:

```sh
export ARM_TOOLCHAIN_DIR=/path/to/arm-gnu-toolchain/bin
```

## 3. Get the code

```sh
git clone --recurse-submodules https://github.com/ka5j/obd2-sniffer.git
cd obd2-sniffer
```

`--recurse-submodules` also downloads ST's driver libraries (HAL and CMSIS), which are kept as
separate repositories pinned to exact versions. Adding `--shallow-submodules` skips their history
and makes the download about a quarter smaller.

If you already cloned without it:

```sh
git submodule update --init --recursive
```

## 4. Build

```sh
cmake --workflow --preset firmware
```

This configures and builds in one step and ends with a memory report. The output is in
`build/firmware/firmware/`:

| File | Use |
|---|---|
| `obd2_sniffer.bin` | Raw image: drag-and-drop flashing, STM32CubeProgrammer |
| `obd2_sniffer.hex` | Intel HEX image for most flashing tools |
| `obd2_sniffer.elf` | Image with debug information, for OpenOCD and debuggers |
| `obd2_sniffer.map` | Linker map: where everything is placed in memory |

For a smaller, optimised build use `cmake --workflow --preset firmware-release` (output in
`build/firmware-release/`).

**No toolchain?** Download ready-made firmware instead. Each
[release](https://github.com/ka5j/obd2-sniffer/releases) has the `.bin`, `.hex` and `.elf` files
attached, checked on a board before publishing. For the newest, untagged code, open the
repository's **Actions** tab, select the latest successful **CI** run on `develop`, and download
the `obd2_sniffer-firmware-release` artifact (you need to be signed in to GitHub).

## 5. Load the firmware onto the board

Plug the board in. The red **COM** LED next to the USB connector lights up.

### Option A: drag and drop (any operating system)

The board's built-in ST-LINK appears as a USB drive called **NODE_F446RE**. Copy
`obd2_sniffer.bin` onto it. The COM LED flickers while the board is programmed, the drive
reconnects, and the new firmware starts.

### Option B: command line (OpenOCD)

```sh
cmake --build --preset firmware --target flash
```

This programs the board, verifies what was written, and restarts it.

### Option C: STM32CubeProgrammer

ST's free [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
accepts the `.hex`, `.bin` or `.elf` file. Connect with **ST-LINK**, open the file, and click
**Download**.

## 6. See it running

The firmware talks over the same USB cable, which also appears as a serial port.

| System | Port | Command |
|---|---|---|
| macOS | `/dev/tty.usbmodem*` | `screen /dev/tty.usbmodem* 1500000` (quit: Ctrl-A, then K, then Y) |
| Linux | `/dev/ttyACM0` | `screen /dev/ttyACM0 1500000` |
| Windows | `COMx`: see Device Manager, "STMicroelectronics STLink Virtual COM Port" | PuTTY: Serial, the COM port, speed 1500000 |

Press the black **RESET** button on the board. You should see the start-up report:

```
========================================
 OBD2 CAN Sniffer  v0.2.0
----------------------------------------
 MCU      STM32F446RE  rev 0x....
 UID      ........-........-........
 Clock    HSE 8 MHz bypass
 SYSCLK   180 MHz
 PCLK1    45 MHz   PCLK2 90 MHz
 Reset    reset pin
========================================
Type 'help' for a list of commands.
obd>
```

The silicon revision and unique ID differ from board to board. The green **LD2** LED blinks once
per second, and pressing the blue **USER** button prints `button pressed`.

At the `obd>` prompt, try `help`, `status`, `version` or `led on`. All commands are described in
[protocol.md](protocol.md).

To see the diagnostic side, let the board talk to its own simulated engine computer:

```
obd> can loopback
obd> can stream off
obd> obd start
obd> obd
```

`obd` shows live engine values, the warning light, the stored trouble codes and the VIN; type it
again to see the values change. `obd stop` and `can off` stop it.

## 7. Use the laptop tool

The `obdsniff` tool talks to the board from your computer. It needs Python 3.10 or newer:

```sh
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -e "host[gui]"         # without [gui] if you only want the command line

obdsniff info                      # finds the board automatically
obdsniff shell                     # interactive session
obdsniff --port sim shell          # the same, against the built-in simulator
obdsniff obd watch                 # OBD2 live data from the simulated engine computer
obdsniff gui                       # desktop app
```

All commands are listed in [host/README.md](../host/README.md).

## 8. Run the tests (optional, no board needed)

The hardware-independent code has unit tests that run on your computer:

```sh
cmake --workflow --preset host-tests
```

This builds the tests with memory and undefined-behaviour checkers and runs all of them. The laptop
tool's tests run with `cd host && pip install -e ".[dev]" && python -m pytest --cov`. Coverage
measurement and the MISRA C check are described in [testing.md](design/testing.md) and
[misra.md](design/misra.md).

With a board connected and the laptop tool installed, `python3 scripts/board_check.py` checks the
board itself: clocks, the serial console under a burst of commands, and CAN capture at full bus
load. It prints what it measured; see [testing.md](design/testing.md#checks-on-the-board).

## 9. Debugging (optional)

With VS Code, install the recommended extensions (**Cortex-Debug**, **CMake Tools**), then run
**Debug (OpenOCD, ST-LINK)** from the Run and Debug panel. It builds, flashes, and stops at the
start of `main()`, so you can step through the code and inspect variables and registers.

## Troubleshooting

| Problem | Fix |
|---|---|
| CMake: "ST HAL sources are missing" | The submodules weren't downloaded: `git submodule update --init --recursive` |
| `arm-none-eabi-gcc` not found | Install the toolchain and add its `bin` folder to `PATH`, or set `ARM_TOOLCHAIN_DIR` |
| No NODE_F446RE drive and no serial port | Try another USB cable (many are power-only) and check the red COM LED. On older Windows versions, install ST's [ST-LINK driver (STSW-LINK009)](https://www.st.com/en/development-tools/stsw-link009.html) |
| OpenOCD can't find the ST-LINK | Close other programs using the board (STM32CubeIDE, another debug session). On Linux, make sure OpenOCD's udev rules are installed (the `openocd` package does this), then unplug and replug the board |
| OpenOCD or CubeProgrammer report old ST-LINK firmware | Update it with STM32CubeProgrammer (Firmware upgrade) or ST's [ST-LINK upgrade tool](https://www.st.com/en/development-tools/stsw-link007.html) |
| Serial terminal shows nothing | Press RESET after connecting. Check the speed is 1500000 and that no other program has the port open |
| Garbled characters | Wrong speed: use 1500000, 8 data bits, no parity, 1 stop bit. Releases before v0.3.0 use 115200 |
| Typing shows nothing, but commands answer `OK` | A program left the console in host mode (for example the laptop tool was killed). Type `mode interactive` and press Enter, or press RESET |
| Linux: "Permission denied" on `/dev/ttyACM0` | Add yourself to the `dialout` group (above), then log out and in |
| Start-up report says `HSI 16 MHz (fallback)` | The board isn't receiving the 8 MHz clock from the ST-LINK (solder bridges changed, or a different board). The firmware still runs, but CAN timing may be unreliable. See [hardware.md](design/hardware.md#clock-configuration) |
