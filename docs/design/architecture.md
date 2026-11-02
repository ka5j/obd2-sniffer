# How the firmware fits together

## In short

This is a one-page map of the firmware: what each part does, which parts react to the hardware
immediately and which run in turn, and the path a CAN frame takes to the laptop. The other
design documents go into each part in detail; this one shows how they connect.

**Two halves.**
- `lib/` is portable C with no hardware code: buffers, the link format, the console logic, CAN
  bit timing, ISO-TP and OBD2. Because it doesn't touch the chip, it runs on a laptop as well,
  where it is unit-tested to 100% MC/DC coverage.
- `firmware/` connects that logic to the STM32: clocks, interrupts, the serial port, the CAN
  controller. It is kept thin and is checked on the board itself.

**Two kinds of code.**
- **Interrupt handlers** react to the hardware within microseconds, do the smallest possible
  piece of work, such as copying a frame and stamping the time, and return.
- **Everything else runs in one main loop**, one task after another. No task ever waits, so no
  task can hold up the others. There is no operating system.

| Term | Meaning |
|---|---|
| Interrupt | The hardware pausing the main program to run a short handler, then resuming it |
| Priority | Which interrupt goes first when two are due; a lower number is more urgent here |
| Super-loop | A main loop that calls each task in turn, forever |
| DMA | Direct memory access: hardware that moves data (here, bytes to the serial port) without the CPU |
| Ring buffer | A fixed-size buffer that wraps around, used to pass data between an interrupt and the main loop |
| Lock-free | Shared between one writer and one reader without disabling interrupts, by design |

## The parts

| Folder | Files | What it does |
|---|---|---|
| `firmware/system` | `main.c` | Start-up, then the main loop |
| | `clock.c`, `timebase.c` | 168 MHz clock; the 1 µs timer used for timestamps |
| | `watchdog.c`, `reset_cause.c` | Restarts a stuck board; reports why the board last restarted |
| | `board.c`, `status_led.c` | Pin assignments, LED, button |
| | `syscalls.c` | C library hooks; `malloc` always fails, so nothing is allocated at run time |
| `firmware/console` | `uart_link.c` | Serial link: receive interrupt into a ring buffer, transmit by DMA |
| | `console.c`, `commands.c` | Connects the console logic to the link; the table of commands |
| `firmware/can` | `can_drv.c` | CAN controller: receive interrupt, timestamps, the board's send path through the transmit guard |
| | `can_stream.c` | Sends captured frames to the laptop |
| | `can_gen.c` | Test traffic for checking the capture path |
| `firmware/obd` | `obd.c` | Connects the OBD2 tester and, in loopback, the simulated engine ECU to the CAN driver |
| `firmware/fault` | `fault.c`, `fault_entry.S` | Crash capture on a separate stack, reported after the restart |
| `lib/buffers` | `ring_buffer.c`, `frame_queue.c` | The lock-free queues between interrupts and the main loop |
| `lib/link` | `packet.c`, `cobs.c`, `crc16.c` | The error-checked packet format for frames to the laptop |
| `lib/console` | `console_core.c`, `cli.c`, `line_reader.c` | Reading command lines, command parsing, replies |
| `lib/can` | `can_timing.c` | Works out the CAN bit timing for a bit rate |
| `lib/obd` | `isotp.c`, `obd2.c`, `obd_client.c`, `tx_guard.c` | Multi-frame transport, OBD2 messages, the tester, the transmit guard |
| | `ecu_sim.c`, `ecu_node.c` | The simulated engine ECU used in loopback |
| `lib/fault` | `fault_record.c` | The crash record format and its checksum |

## Interrupts

| Interrupt | Priority | Deadline that sets the priority | What it does |
|---|---|---|---|
| Serial receive (`USART2_IRQHandler`) | 0, the highest | The serial port holds one byte; at 1.5 Mbaud the next arrives 6.7 µs later | Stores the byte in the receive ring buffer |
| CAN receive (`CAN1_RX0_IRQHandler`) | 1 | The CAN controller holds 3 frames, about 90 µs of a full bus at 1 Mbit/s | Stamps each frame with the time and a capture number; puts it in the frame queue |
| Serial transmit DMA done (`DMA1_Stream6_IRQHandler`) | 6 | None tight: the laptop just waits a little longer | Releases the bytes just sent; starts the next block |
| System tick (`SysTick_Handler`) | 15, the lowest | 1 ms tick | Counts milliseconds |
| Faults (`HardFault`, `BusFault`, `UsageFault`, `MemManage`) | HardFault is fixed above everything; the others are at 0 | They run when the processor stops an illegal operation | Switch to the 512-byte fault stack, record the crash, restart |

The longest CAN receive interrupt measured on the board is 14 µs (Debug build) and 9 µs
(Release), with OBD2 traffic. The time includes serial interrupts that preempt it.

## The main loop

Each pass does, in order:

1. `watchdog_kick()`: proves the loop is still running. If a pass ever stalls, the watchdog
   restarts the board after about half a second.
2. `console_poll()`: takes received bytes, and runs at most one command, and only once its reply
   fits in the transmit buffer.
3. `can_gen_task()`: sends test traffic, if a test is running.
4. `can_stream_task()`: moves captured frames from the frame queue to the laptop.
5. `obd_task()`: passes received frames to the OBD2 tester and the simulated ECU; sends their
   next frame, if any. The tester has a frame only after the laptop asked a question.
6. `status_led_task()`, `button_task()`: LED heartbeat and button presses.

## The path of a CAN frame

```
 CAN controller (hardware, 3-frame FIFO)
      |  CAN receive interrupt: timestamp + capture number
      v
 frame queue (lock-free, 409 frames)
      |  main loop: can_stream_task
      v
 packet: COBS framing + CRC-16
      |
      v
 transmit ring buffer (4 KB) --> DMA --> serial port, 1.5 Mbaud --> ST-LINK USB --> laptop
```

When frames arrive faster than the serial link can carry them, the frame queue fills, and each
frame that doesn't fit is counted as dropped. The laptop sees the dropped frames as gaps in the
capture numbers. Command replies always have 1 KB of the transmit buffer kept free for them, so
the board stays controllable on a full bus. Detail: [can-capture.md](can-capture.md).

## Where the safety rules live today

- **CAN is off at start-up.** `main.c` starts the driver with the controller off. A command turns
  CAN on.
- **Every frame the board sends passes the transmit guard.** `can_drv_send()` in
  `firmware/can/can_drv.c` checks each frame with `lib/obd/tx_guard.c`: only OBD2 read requests
  (services 01, 03 and 09) to 0x7DF, at most 20 a second, and the flow control that lets a long
  reply in. Everything else is refused and counted (`can_tx_refused`).
- **Test traffic has its own path, in loopback only.** `can_drv_send_loopback()` carries the
  numbered test frames and the simulated engine ECU's replies, which the guard would refuse. It
  works only while the controller is in silent loopback, where nothing leaves the chip.
- **The tester asks only what the laptop asks, and only read-only questions.**
  `lib/obd/obd_client.c` sends one request per `obd ask`, never on its own. It builds only service
  01, 03 and 09 requests, and spaces them at least 50 ms apart, so it stays within the guard's
  limit. The plan of questions is on the laptop (`host/src/obdsniff/obdplan.py`).

The full safety design, including the layers the circuit board adds, is in
[safety.md](safety.md).

## Start-up

Reset, then the C runtime setup, then `main()`:
1. reads the reset cause;
2. sets up the clock and timers;
3. picks up any crash record;
4. starts the console and the CAN driver (CAN off);
5. prints the start-up report;
6. starts the watchdog last;
7. enters the main loop.

Detail: [build-and-boot.md](build-and-boot.md#boot-sequence).
