# CAN capture: from the bus to the laptop

## In short

The heart of the project: catch every message on the car's network, note exactly when it
arrived, and get it to the laptop without losing any, even when the network is as busy as it
can be.

- **Catching messages.** The microcontroller's CAN controller receives each message (a *frame*)
  and interrupts the processor. The interrupt handler notes the time to the microsecond, gives
  the frame a sequence number and puts it in a queue. It takes at most 7 microseconds and does
  nothing else, so it never falls behind.
- **Sending them on.** The main program takes frames from the queue and sends each one to the
  laptop as a small binary packet, 25 bytes at most, with a checksum. Packets share the USB
  serial line with the text console. A person typing in a terminal sees each frame as a
  readable line instead.
- **Proving nothing is lost.** Each frame's sequence number is assigned the moment it arrives,
  even if the board then has to drop it, so a missing number on the laptop always means a lost
  frame. `obdsniff can test` sends numbered test frames and checks that every one arrives.

Measured on the board at 500 kbit/s, the speed of a car's diagnostic network: **100,000 frames
in a row at 100% bus load (4,230 frames per second), none lost, none damaged.**

All of this is tested with the board alone. The CAN controller has a *loopback* mode in which it
receives the frames it sends, and nothing reaches its pins. Listening to a real network uses the
same capture path.

| Term | Meaning |
|---|---|
| Frame | One CAN message: an identifier (ID), 0 to 8 data bytes, and some control bits |
| Bus load | The share of time the network is busy; at 100% a new frame starts as soon as the last one ends |
| Interrupt | A hardware signal that makes the processor pause, run a short handler, and resume |
| COBS | Consistent Overhead Byte Stuffing: rewrites a packet so it contains no zero byte |
| CRC | A checksum that detects damaged data |

## The path of a frame

```
 CAN controller ──► receive FIFO (3 frames, hardware)
                       │ interrupt, highest priority
                       ▼
          CAN1_RX0_IRQHandler: timestamp (TIM2, 1 µs) + capture number
                       │
                       ▼
          frame queue (lock-free, 8 KB = 409 frames)          board: firmware/src/
                       │ main loop
                       ▼
          can_stream_task: packet (host mode) or text line (interactive)
                       │ leaves 1 KB for command replies, never waits
                       ▼
          transmit ring (4 KB) ──► DMA ──► USART2 1.5 Mbaud ──► ST-LINK USB
                                                                 │
          StreamSplitter: text lines │ packets ──► CRC check ──► frames
                                                                  laptop: host/src/obdsniff/
```

## Engineering detail

### Receiving: `firmware/src/can_drv.c`

- **Pins and clock.** CAN1 on PB8 (receive) and PB9 (transmit), clocked from the 45 MHz APB1 bus.
  Bit timing comes from the calculator in `lib/src/can_timing.c`: at 500 kbit/s, prescaler 6 and
  15 time quanta per bit, sampling at 86.7% (see [hardware.md](hardware.md#can-bit-timing)).
- **Filter.** One filter bank in 32-bit mask mode with an all-zero mask, so every frame is
  accepted into FIFO 0. A sniffer wants everything.
- **Interrupt.** The FIFO-0 "message pending" interrupt runs at the highest priority in the
  system (1). For each frame waiting in the 3-deep hardware FIFO it:
  1. reads the frame (`HAL_CAN_GetRxMessage`);
  2. reads the microsecond timer as its timestamp;
  3. gives it the next 16-bit capture number;
  4. pushes it into the frame queue, or counts it as dropped if the queue is full.

  A FIFO overrun (a frame lost inside the controller, before the interrupt could run) is
  counted too. The handler measures its own duration with the CPU's cycle counter: 7 µs at
  most, against 238 µs between frames at full load.
- **Off at start-up.** CAN is only started by a command, and sending is only allowed in loopback.
  A tool on a car's network must never transmit by accident.

### Timestamps: `firmware/src/timebase.c`

TIM2 is a 32-bit timer. Clocked at 90 MHz with a prescaler of 90, it counts microseconds and wraps
after 71.6 minutes; the laptop tool unwraps it. The timestamp is taken when the interrupt
handler runs, a few microseconds after the frame ends. The measured spacing of back-to-back
8-byte frames at 500 kbit/s is 234 to 252 µs. That matches the theory: about 119 bits including
stuff bits and the gap between frames, which is 238 µs.

An alternative is the controller's time-triggered mode, which stamps each frame in hardware at
its start, but only with a 16-bit counter of bit times. The interrupt timestamp is precise enough
for traffic analysis and works the same at every bit rate.

### The frame queue: `lib/src/frame_queue.c`

One producer (the interrupt) and one consumer (the main loop) share it without locks or
switching interrupts off. It is built on the project's lock-free ring buffer:

- A frame is pushed only if there is room for all of it.
- The frame is published with a single update of the write position, so the consumer can never
  see half a frame.

The queue holds 409 frames, about 100 ms of a fully loaded bus, to absorb bursts while the main
loop is busy. It is unit-tested, including a two-thread stress test of 200,000 frames under
ThreadSanitizer.

### Capture numbers

Each frame is numbered when it is captured, and the counter advances even for a frame that is
then dropped. A gap in the numbers on the laptop therefore means a lost frame, whether it was
lost on the board (queue full) or on the way (a damaged packet). The number starts again at 0
whenever CAN is started.

The first version numbered packets when they were *sent*. The overload test at 1 Mbit/s showed no
gaps at all while the board was dropping thousands of frames, because dropped frames never
became packets. Moving the numbering into the interrupt fixed that; now the laptop's gap count
equals the board's drop count exactly.

### Sending: `firmware/src/can_stream.c`, `lib/src/packet.c`, `lib/src/can_text.c`

- **Host mode:** each frame becomes a packet (format in [protocol.md](../protocol.md#can-frame-packets)):
  `0x00 | COBS(type, capture number, timestamp, ID, length, data, CRC-16) | 0x00`.
  - Binary because a frame is 25 bytes this way and about 55 as text: at 1.5 Mbaud that is about
    5,900 frames per second against 2,700. A fully loaded 500 kbit/s bus produces up to about
    4,500.
  - COBS removes every zero byte from the packet and text never contains one, so zeros mark
    packet boundaries unambiguously in the mixed stream. A receiver that loses a byte finds the
    next packet at the next zero.
- **Interactive mode:** each frame becomes a readable line, built by hand rather than with
  `printf`.
- **Never waiting, never starving commands.** A frame is taken from the queue only once its output
  fits in the transmit buffer *beyond* the 1 KB reserved for command replies. If the serial link
  can't keep up, frames wait in the queue rather than blocking the main loop, and commands still
  get an immediate reply.
- **Streaming can be switched off** (`can stream off`). Frames are then still received, counted
  and taken out of the queue (so the queue never fills and nothing is counted as dropped), but
  not sent. A person in a serial terminal can keep the console readable while the board is busy
  with other traffic. `obdsniff monitor` and `obdsniff can test` switch it back on.

### On the laptop: `host/src/obdsniff/packets.py`

`StreamSplitter` separates packets from text as bytes arrive. If a byte is lost, it gets back in
step:

- two zeros in a row mean the second starts a packet;
- a byte that console text never contains, outside a packet, means a packet whose opening zero
  was lost;
- a "packet" longer than any real one was text after all.

Damaged packets are counted and never mistaken for text. The decoder is tested against the same
golden byte sequences as the firmware's encoder.

### Test traffic: `firmware/src/can_gen.c`

`can test <count> [per_s]` sends frames with ID `123` whose data is a 32-bit sequence number and
its bitwise inverse. That is enough for the laptop to check content, order and completeness of
every frame. Frames are sent as fast as the three transmit mailboxes allow, or paced to a rate.

## Measured results

On the NUCLEO-F446RE in silent loopback, every frame checked by number and content:

| Bit rate | Frames | Received | Lost | Bus rate | Frame spacing |
|---|---|---|---|---|---|
| 500 kbit/s | 100,000 | 100,000 | 0 | 4,230/s (100% load) | 234-252 µs |
| 500 kbit/s | 50,000 | 50,000 | 0 | 4,212/s | 234-252 µs |
| 250 kbit/s | 10,000 | 10,000 | 0 | 2,097/s | 468-500 µs |
| 500 kbit/s, paced at 1,000/s | 2,000 | 2,000 | 0 | 1,001/s | 988-1,010 µs |
| 1 Mbit/s | 20,000 | about 17,140 | about 2,860 | about 8,400/s | 117-483 µs |

- Damaged packets: 0 in every run.
- Longest receive interrupt: 7 µs.
- At 1 Mbit/s the bus produces more frames than the 1.5 Mbaud serial link can carry (about
  5,900 per second). The queue fills and frames are dropped. That is the expected limit, and it
  is reported, not hidden: the board counts the drops, and the laptop sees the same number of
  gaps in the capture numbers.
