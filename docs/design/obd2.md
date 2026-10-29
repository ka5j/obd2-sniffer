# OBD2: asking the car for live data, trouble codes and its VIN

## In short

A sniffer listens; a diagnostic tool also asks. OBD2 is the standard set of questions every car
since 1996 (US) or 2001 (EU) must answer on its diagnostic port: "how fast is the engine
turning?", "which fault codes are stored?", "what is your vehicle identification number?".

- **The tester.** The board has an OBD2 tester built in. It works through a fixed plan, one
  question at a time: the VIN first, then the stored trouble codes and the warning-light status
  every 5 seconds, and in between seven live values (engine speed, vehicle speed, throttle,
  engine load, coolant and intake temperature, engine run time) in turn. About 100 questions a
  second.
- **The simulated car.** With only the development board available, there is no car to ask. In
  loopback the CAN controller receives every frame it sends, so the board plays the car as well:
  a simulated engine control unit (ECU) answers the tester, all through the real CAN controller.
  Its engine repeats a 30-second drive (idle, accelerate, cruise, slow down), warms up over
  90 seconds, has two stored trouble codes and a valid VIN.
- **Long answers.** A CAN frame carries at most 8 bytes, but the VIN answer is 20. ISO-TP, the
  transport layer OBD2 uses on CAN, splits it into numbered frames, and the receiver paces the
  sender with *flow control*.
- **Three ways to use it.** `obd` in a serial terminal, `obdsniff obd watch` on the laptop, or
  the desktop app's OBD2 tab.

Measured on the board: **838 questions in the first 9 seconds (about 95 a second), every one
answered, 0 timeouts, 0 errors**; the multi-frame VIN and both trouble codes read correctly.

| Term | Meaning |
|---|---|
| OBD2 | On-Board Diagnostics, second generation: the standard questions and the port they are asked on (SAE J1979, ISO 15031) |
| ECU | Electronic control unit: one of the car's computers. The engine ECU answers most OBD2 questions |
| PID | Parameter ID: which value a question is about, e.g. 0C for engine speed |
| DTC | Diagnostic trouble code, e.g. P0301 (cylinder 1 misfire): a fault the ECU has recorded |
| MIL | Malfunction indicator lamp: the "check engine" warning light |
| VIN | Vehicle identification number: 17 characters, unique to each car |
| ISO-TP | ISO 15765-2: splits messages longer than one CAN frame and reassembles them |

## The conversation

```
 tester (board)                       simulated engine ECU (board)
      │  0x7DF  02 09 02          "your VIN?"  (to every ECU)
      ├───────────────────────────────────────►│
      │  0x7E8  10 14 49 02 01 31 4D 38   first frame: 20 bytes coming, "1M8"
      │◄───────────────────────────────────────┤
      │  0x7E0  30 00 00          flow control: send the rest, no pauses
      ├───────────────────────────────────────►│
      │  0x7E8  21 47 44 4D 39 41 58 4B   consecutive frame 1: "GDM9AXK"
      │  0x7E8  22 50 30 34 32 37 38 38   consecutive frame 2: "P042788"
      │◄───────────────────────────────────────┤
      │  10 ms later: 0x7DF  01 03 (trouble codes?), and so on
```

Requests go to 0x7DF, which every emissions-related ECU listens to. The engine ECU replies from
0x7E8, and flow control for its long replies goes to its own ID, 0x7E0. In loopback all of these
frames are also captured and streamed, so `obdsniff monitor` shows the conversation.

## Engineering detail

### Layers

All the protocol logic is portable C in `lib/`, unit-tested on the laptop to 100% MC/DC:

| Module | Job |
|---|---|
| `isotp.c` | ISO-TP sender and receiver: single, first, consecutive and flow-control frames; block size, STmin, wait frames, timeouts |
| `obd2.c` | Reply formats: PID formulas, monitor status, trouble codes and their text form, VIN |
| `obd_client.c` | The tester: the request plan, reply timeout, the latest answers and counters |
| `ecu_sim.c` | The simulated engine: a pure function of elapsed time, so tests check exact bytes |
| `ecu_node.c` | ISO-TP around `ecu_sim`, so the simulated ECU sits on the bus like a real one |

The firmware side is thin: `firmware/obd/obd.c` routes frames between the CAN driver and the two
nodes, and `commands.c` adds the `obd` command.

### State machines that never wait

The firmware is a single main loop with a watchdog, so nothing may block. Each ISO-TP sender and
receiver is a state machine with three entry points:

- `..._frame(data)`: a frame arrived;
- `..._poll(now)`: is there a frame to send, or has a timeout expired?
- `..._sent(now)`: the frame from the last poll was accepted by the CAN controller.

Poll keeps handing out the same frame until `sent` confirms it. If all three transmit mailboxes
are busy, the transfer is simply delayed by a pass of the main loop, never corrupted. This
mirrors the standard, where the data link layer confirms each transmission (`L_Data.confirm`),
and timers such as STmin start from that confirmation.

### Timing

| Limit | Value | Source |
|---|---|---|
| ECU reply must start within | 50 ms | ISO 15765-4 (P2) |
| Gap between consecutive frames of a reply (receiver gives up after) | 1 s | ISO 15765-2 default (N_Cr) |
| Wait for flow control (sender gives up after) | 1 s | ISO 15765-2 default (N_Bs) |
| "Wait" flow-control frames accepted in a row | 10 | N_WFTmax |
| Tester's pause between a reply and the next request | 10 ms | keeps the bus load modest |

Once the first frame of a reply has arrived, the 50 ms limit no longer applies: ISO-TP times the
rest. Times are microseconds from the 32-bit timebase and compared in a way that survives the
counter wrapping every 71 minutes.

### Getting frames to the tester

The capture path has one queue between the receive interrupt and the stream to the laptop. The
tester and the simulated ECU need the same frames, but must not wait while the serial link is
busy or while streaming is switched off. So the interrupt also copies OBD2 frames (to 0x7DF and
0x7E0, from 0x7E8) into a second, small queue. Each queue keeps exactly one producer (the
interrupt) and one consumer, which is what makes the lock-free queue safe. The extra copy
raises the longest receive interrupt from 7 to 10 µs, against 238 µs between frames at full
load.

### Values without floating point

Each PID's reply bytes map to a value by a formula from SAE J1979, e.g. engine speed is
(256·A + B) / 4 rpm and throttle is A·100/255 %. Values are kept as integers scaled by
10^decimals: 23.5% throttle is 235 with one decimal. The same table gives each value its key in
host mode and its label in the terminal.

### The simulated ECU

`ecu_sim.c` follows a table of keyframes (time, engine speed, vehicle speed, throttle) and
interpolates between them with integer arithmetic. Like a real ECU it stays silent for services
and PIDs it doesn't support. The laptop's simulator (`host/src/obdsniff/sim/obd.py`) follows the
same model, and a test reads the C sources to check that the keyframes, VIN, trouble codes and
PID table match.

### Safety

The tester only sends in loopback, like every other transmit command, so nothing reaches a car.
Clearing trouble codes (service 04) is not implemented; on a real car it will need an
interlock.

## Checks on the board

| Check | Result |
|---|---|
| `obd start` with CAN off | Refused |
| After 2 s | 220 requests, 220 replies; VIN, both trouble codes and the warning light right |
| After 10 s | 838 requests (about 95 a second), 0 timeouts, 0 errors; values follow the simulated drive |
| Bus | 1,775 frames sent and 1,775 received, 0 dropped, with streaming off |
| Longest receive interrupt | 10 µs with OBD2 traffic |
