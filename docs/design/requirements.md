# Requirements

## In short

This document says what the device has to do, how well, and how each requirement is proven.

**The job.** Plug into a car's diagnostic port and show on a laptop two things: what the car's
computers are saying to each other, and what the engine computer reports (live values, trouble
codes, the VIN). And never disturb the car.

**The rule for features.** A feature belongs in the device only if one of two things is true:
- it does part of that job;
- it proves that the device works.

Anything else is left out, because every extra feature is something more that can fail.

**How to read the tables.** Each requirement has:
- a number;
- the result that counts as a pass;
- its status today.

A requirement counts as met only when its test has passed **on the hardware**, not just in
simulation. Today the device runs on a NUCLEO-F446RE development board with its CAN controller
in loopback, so CAN results are marked "in loopback". The project's own circuit board is
designed, not built yet ([circuit-board.md](circuit-board.md)).

| Term | Meaning |
|---|---|
| Loopback | The CAN controller receives its own frames, and nothing leaves the chip |
| Silent mode | The CAN controller listens but never drives the bus, not even to acknowledge a frame |
| Transceiver | The chip that turns the controller's logic signals into the bus's two-wire signal |
| ECU | Electronic control unit: one of the car's computers |
| Bus-off | The state a CAN controller enters after too many errors: it stops using the bus |

## Status values

| Status | Meaning |
|---|---|
| **Met** | Proven by its test on the Nucleo board (or in CI, for code checks) |
| **Met in loopback** | Proven on the Nucleo board with CAN in loopback; still to be proven on a real bus |
| **Partly met** | Some of it is built and proven; the table says which part |
| **Not built** | Not implemented yet |
| **Design** | Part of the circuit board design; proven only once the board exists |

## Safety

The reasons behind these are in [safety.md](safety.md).

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| SAF-1 | The device sends only OBD2 read requests: service 01 (live data), 03 (stored trouble codes) and 09 (vehicle information, such as the VIN), plus the flow-control frame that multi-frame replies need | A test build tries to send every other service and any other ID, and each attempt is refused | **Met in loopback.** The transmit guard (`lib/obd/tx_guard.c`, unit-tested to 100% MC/DC) checks every frame on the board's send path and refuses the rest. Only the test traffic and the simulated ECU bypass it, on a path that works in loopback only. Still to be proven on a real bus |
| SAF-2 | The device never transmits on its own initiative: every frame it sends answers a request from the laptop | A board left connected, with no laptop request, sends nothing (checked with a logic analyzer on the transmit pin) | **Met in loopback.** The tester sends one request per `obd ask` from the laptop; the board check stops asking and confirms that no request follows in the next second. The logic-analyzer check comes with the real bus |
| SAF-3 | A LISTEN/OBD switch disables the transmitter electrically in LISTEN. After reset, a crash, a brown-out or with a blank chip, the transmitter is disabled too | In LISTEN, a test build trying to transmit leaves the bus untouched | **Design** |
| SAF-4 | The CAN controller stays in silent mode except while it sends an allowed request | The bus never sees an acknowledge or error frame from the device in LISTEN mode | **Met in loopback.** CAN is off at start-up and runs only in silent loopback, where nothing leaves the chip; sending is refused in any other state |
| SAF-5 | Any bus error latches the device into silent mode until it is unplugged | Each bus fault from the test list latches silent mode and is reported | **Not built** |
| SAF-6 | The bit rate is found by listening only | Detection on a busy bus produces no frame, acknowledge or error frame from the device | **Not built.** The bit rate is set by command today |
| SAF-7 | On a CAN FD bus, the device stays silent and reports an unsupported bus | Connected to CAN FD traffic, it reports "unsupported bus" and never leaves silent mode | **Not built** |
| SAF-8 | One request at a time, at most 20 a second on a real bus | Measured request rate during a car session | **Met in loopback:** one request at a time, and the transmit guard refuses any request less than 50 ms after the last: 201 requests in 10 s on the board, 0 refused |
| SAF-9 | Nothing that clears trouble codes, writes to an ECU, resets an ECU or opens a diagnostic session exists in the firmware | Code review of every transmit path | **Met.** No such request is built in, and no command sends a frame typed by hand |

## Power

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| PWR-1 | Runs from the car's 12 V supply anywhere from 6 to 16 V | Swept 6 to 16 V on a bench supply, every function works | **Design** |
| PWR-2 | Survives a reversed battery connection (-14 V) | Reversed on a bench supply with a current limit; the board works afterwards | **Design** |
| PWR-3 | Survives a jump start (24 V for 1 minute) and load-dump spikes, clamped by its TVS diode | Bench test at 24 V; clamp voltage checked against the TVS diode's rating | **Design** |
| PWR-4 | Rides through a cranking dip, or restarts cleanly and reports a brown-out reset | Bench supply dipped to 6 V and below; the board keeps running or reports the reset | **Design** |
| PWR-5 | Runs from USB alone or from the car alone | Each source on its own powers every function | **Design** |
| PWR-6 | Draws under 1 mA from the car while asleep | Measured on the 3.3 V rail jumper and at the 12 V input | **Design** |
| PWR-7 | A fault on the board blows the board's own fuse, not the car's | Fuse rating below the car's OBD-port fuse; input shorted on the bench | **Design** |

## CAN capture

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| CAN-1 | No frame lost at 500 kbit/s and 100% bus load | 100,000 frames, each checked by number and content | **Met in loopback:** 100,000 frames, 0 lost, 0 damaged |
| CAN-2 | The same at 250 kbit/s | 10,000 frames | **Met in loopback:** 0 lost, 0 damaged |
| CAN-3 | When the traffic outgrows the laptop link, every dropped frame is counted and visible | Overload at 1 Mbit/s: frames counted as dropped on the board equal the gaps the laptop sees | **Met in loopback:** 5,212 of 20,000 dropped (Release build), every one counted and seen |
| CAN-4 | Each frame is timestamped to 1 µs | Frame spacing matches the frame length on the bus | **Met in loopback** |
| CAN-5 | Recovers from every bus fault in the test list: no acknowledge, CANH shorted to CANL, a wire disconnected mid-stream, missing termination | Each fault injected on a desk bus; the device reports it and recovers or latches (SAF-5) | **Not built** (needs the desk bus) |
| CAN-6 | The device adds no termination to the car's bus | Measured 60 ohms across CANH and CANL on the car with and without the device | **Design** (termination off by default) |
| CAN-7 | A self-test proves both transceivers and the wiring before the device goes in a car | The self-test passes on a good board and fails with each transceiver disconnected | **Not built** |

## OBD2

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| OBD-1 | Reads 7 live values, the warning light, stored trouble codes and the VIN | All values right against a known ECU | **Met in loopback:** 201 requests in 10 s against the simulated ECU, 201 replies, 0 timeouts; VIN and codes right |
| OBD-2 | The same against a real car | Values match the car's dashboard and a commercial scanner | **Not built** (needs the desk bus first) |

## Link to the laptop

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| LNK-1 | Commands keep working while the CAN bus is full | Commands sent continuously during a full bus | **Met:** 2,709 commands while 42,126 frames arrived, 0 failed, no byte lost |
| LNK-2 | Every packet to the laptop is error-checked | CRC-16 on every packet; damaged packets rejected | **Met** (unit-tested) |
| LNK-3 | Appears as a serial port on macOS, Windows and Linux with no driver to install | Plugged into each | **Partly met.** Today the link is the Nucleo's ST-LINK serial port; the circuit board uses the microcontroller's own USB |
| LNK-4 | Unplugging USB mid-stream and plugging it back in loses nothing but the frames in between | Unplug and replug during a stream test; the board keeps running and reconnects | **Not built** |

## Reliability

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| REL-1 | A stuck program restarts the board in under 1 s and the restart is reported | Main loop stopped on purpose | **Met:** restarts after 535 ms, reported as a watchdog reset |
| REL-2 | A crash is recorded, the board restarts, and the crash is reported afterwards | Each kind of fault triggered on purpose | **Met:** bus, usage, divide-by-zero and hard faults, right type and address |
| REL-3 | The reason for every restart is reported | Power-on, reset pin, software and watchdog resets | **Met** |
| REL-4 | Worst-case stack use fits the reserved stack | Worked out from the program in CI | **Met:** 2,544 bytes (Release) and 2,772 (Debug) of 8,192 |
| REL-5 | No memory is allocated at run time | `malloc` always fails | **Met** |
| REL-6 | Runs for 24 hours streaming with nothing lost and no restart | Bench soak test | **Not built** |

## Ease of use

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| USE-1 | The laptop tool finds the board without being told the port | Plugged in, `obdsniff info` works | **Met** (finds the board by its USB IDs) |
| USE-2 | No settings: the bit rate is found automatically | Plugged into buses at 250 and 500 kbit/s, data appears with no setting | **Not built** |
| USE-3 | The board shows its state with no laptop: power, bus found or bus error, able to transmit | Each state shown by the PWR, BUS and TX lights | **Design** |
| USE-4 | Firmware can be updated from the laptop, with no debugger | Update with the laptop tool through the chip's built-in USB bootloader | **Not built** |

## Code quality

| ID | Requirement | Pass when | Status |
|---|---|---|---|
| QLT-1 | The portable C library is unit-tested to 100% MC/DC coverage | CI's coverage gate | **Met** |
| QLT-2 | All C code follows MISRA C:2012, every exception documented | CI's MISRA gate | **Met** |
| QLT-3 | Every release file is built by CI from its tag and checked on the board before it is published | The release process in [build-and-boot.md](build-and-boot.md#release-files) | **Met** |

## Deliberately left out

Each of these was considered and fails the rule for features, or would put the car at risk:
- clearing trouble codes, writing to an ECU, or any diagnostic session;
- wireless (Bluetooth or Wi-Fi), an SD card, a display, a buzzer;
- the older OBD2 protocols (K-line, J1850): every car sold in North America since 2008 uses CAN;
- any power output from the device.
